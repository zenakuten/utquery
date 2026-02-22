#include "master.h"
#include "md5.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
using socket_t = SOCKET;
static constexpr socket_t SOCKET_INVALID = INVALID_SOCKET;
#else
using socket_t = int;
static constexpr socket_t SOCKET_INVALID = -1;
#endif

static void close_socket(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

// ---------------------------------------------------------------------------
// Buffer helpers for reading/writing UE2 FArchive-style serialization
// ---------------------------------------------------------------------------

class WriteBuffer {
    std::vector<uint8_t> data_;
public:
    const uint8_t* data() const { return data_.data(); }
    size_t size() const { return data_.size(); }
    void clear() { data_.clear(); }

    void write_bytes(const void* p, size_t n) {
        auto* b = static_cast<const uint8_t*>(p);
        data_.insert(data_.end(), b, b + n);
    }
    void write_byte(uint8_t v) { data_.push_back(v); }
    void write_int32(int32_t v) { write_bytes(&v, 4); }
    void write_word(uint16_t v) { write_bytes(&v, 2); }

    // UE2 compact index encoding
    void write_compact_index(int32_t value) {
        uint32_t v = static_cast<uint32_t>(value < 0 ? -value : value);
        uint8_t b0 = (value < 0 ? 0x80 : 0) |
                     (v < 0x40 ? static_cast<uint8_t>(v) : static_cast<uint8_t>((v & 0x3f) + 0x40));
        write_byte(b0);
        if (b0 & 0x40) {
            v >>= 6;
            uint8_t b1 = v < 0x80 ? static_cast<uint8_t>(v) : static_cast<uint8_t>((v & 0x7f) + 0x80);
            write_byte(b1);
            if (b1 & 0x80) {
                v >>= 7;
                uint8_t b2 = v < 0x80 ? static_cast<uint8_t>(v) : static_cast<uint8_t>((v & 0x7f) + 0x80);
                write_byte(b2);
                if (b2 & 0x80) {
                    v >>= 7;
                    uint8_t b3 = v < 0x80 ? static_cast<uint8_t>(v) : static_cast<uint8_t>((v & 0x7f) + 0x80);
                    write_byte(b3);
                    if (b3 & 0x80) {
                        v >>= 7;
                        write_byte(static_cast<uint8_t>(v));
                    }
                }
            }
        }
    }

    // UE2 FString: compact index length (including null), then ANSI bytes
    void write_fstring(const std::string& s) {
        if (s.empty()) {
            write_compact_index(0);
        } else {
            int32_t len = static_cast<int32_t>(s.size()) + 1; // +1 for null
            write_compact_index(len);
            write_bytes(s.data(), s.size());
            write_byte(0); // null terminator
        }
    }
};

class ReadBuffer {
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    bool error_ = false;
public:
    ReadBuffer(const uint8_t* d, size_t s) : data_(d), size_(s) {}
    bool error() const { return error_; }
    size_t remaining() const { return error_ ? 0 : size_ - pos_; }

    bool read_bytes(void* out, size_t n) {
        if (pos_ + n > size_) { error_ = true; return false; }
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
        return true;
    }
    uint8_t read_byte() {
        uint8_t v = 0;
        read_bytes(&v, 1);
        return v;
    }
    int32_t read_int32() {
        int32_t v = 0;
        read_bytes(&v, 4);
        return v;
    }
    uint16_t read_word() {
        uint16_t v = 0;
        read_bytes(&v, 2);
        return v;
    }
    uint32_t read_dword() {
        uint32_t v = 0;
        read_bytes(&v, 4);
        return v;
    }

    int32_t read_compact_index() {
        uint8_t b0 = read_byte();
        if (error_) return 0;
        int32_t value = 0;
        if (b0 & 0x40) {
            uint8_t b1 = read_byte();
            if (error_) return 0;
            if (b1 & 0x80) {
                uint8_t b2 = read_byte();
                if (error_) return 0;
                if (b2 & 0x80) {
                    uint8_t b3 = read_byte();
                    if (error_) return 0;
                    if (b3 & 0x80) {
                        uint8_t b4 = read_byte();
                        if (error_) return 0;
                        value = b4;
                    }
                    value = (value << 7) + (b3 & 0x7f);
                }
                value = (value << 7) + (b2 & 0x7f);
            }
            value = (value << 7) + (b1 & 0x7f);
        }
        value = (value << 6) + (b0 & 0x3f);
        if (b0 & 0x80)
            value = -value;
        return value;
    }

    std::string read_fstring() {
        int32_t save_num = read_compact_index();
        if (error_ || save_num == 0) return {};
        int32_t count = save_num < 0 ? -save_num : save_num;
        if (count > 10000) { error_ = true; return {}; }
        if (save_num > 0) {
            // ANSI (Latin-1)
            std::string raw(count, '\0');
            if (!read_bytes(raw.data(), count)) return {};
            if (!raw.empty() && raw.back() == '\0')
                raw.pop_back();
            // Convert Latin-1 to UTF-8
            std::string result;
            result.reserve(raw.size());
            for (unsigned char c : raw) {
                if (c >= 0x80) {
                    result.push_back(static_cast<char>(0xC0 | (c >> 6)));
                    result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                } else {
                    result.push_back(static_cast<char>(c));
                }
            }
            return result;
        } else {
            // Unicode (UTF-16 LE) — convert to UTF-8
            std::string result;
            result.reserve(count);
            for (int32_t i = 0; i < count; ++i) {
                uint16_t ch = read_word();
                if (error_) return {};
                if (ch == 0) continue;
                if (ch < 0x80) {
                    result.push_back(static_cast<char>(ch));
                } else if (ch < 0x800) {
                    result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                    result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
                } else {
                    result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
                    result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                    result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
                }
            }
            return result;
        }
    }
};

// ---------------------------------------------------------------------------
// TCP helpers
// ---------------------------------------------------------------------------

// Connect TCP with timeout. Returns socket or SOCKET_INVALID.
static socket_t tcp_connect(const std::string& host, uint16_t port, int timeout_sec) {
    // Resolve hostname
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host.c_str(), port_str, &hints, &res) != 0 || !res)
        return SOCKET_INVALID;

    socket_t sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == SOCKET_INVALID) {
        freeaddrinfo(res);
        return SOCKET_INVALID;
    }

    // Set non-blocking for connect timeout
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif

    int rc = connect(sock, res->ai_addr, static_cast<int>(res->ai_addrlen));
    freeaddrinfo(res);

#ifdef _WIN32
    bool in_progress = (rc == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK);
#else
    bool in_progress = (rc == -1 && errno == EINPROGRESS);
#endif

    if (rc != 0 && !in_progress) {
        close_socket(sock);
        return SOCKET_INVALID;
    }

    if (in_progress) {
#ifdef _WIN32
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(sock, &wset);
        timeval tv;
        tv.tv_sec = timeout_sec;
        tv.tv_usec = 0;
        if (select(0, nullptr, &wset, nullptr, &tv) <= 0) {
            close_socket(sock);
            return SOCKET_INVALID;
        }
#else
        pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLOUT;
        if (poll(&pfd, 1, timeout_sec * 1000) <= 0) {
            close_socket(sock);
            return SOCKET_INVALID;
        }
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
        if (err != 0) {
            close_socket(sock);
            return SOCKET_INVALID;
        }
#endif
    }

    // Set back to blocking
#ifdef _WIN32
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    fcntl(sock, F_SETFL, flags);
#endif

    return sock;
}

// Send all bytes. Returns false on error.
static bool tcp_send_all(socket_t sock, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, reinterpret_cast<const char*>(data + sent),
                     static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

// Receive exactly n bytes with timeout. Returns false on error/timeout.
static bool tcp_recv_exact(socket_t sock, uint8_t* buf, size_t n, int timeout_sec) {
    size_t got = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
    while (got < n) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) return false;

#ifdef _WIN32
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(sock, &rset);
        timeval tv;
        tv.tv_sec = static_cast<long>(remaining.count() / 1000);
        tv.tv_usec = static_cast<long>((remaining.count() % 1000) * 1000);
        if (select(0, &rset, nullptr, nullptr, &tv) <= 0) return false;
#else
        pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, static_cast<int>(remaining.count())) <= 0) return false;
#endif

        int r = recv(sock, reinterpret_cast<char*>(buf + got),
                     static_cast<int>(n - got), 0);
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

// ---------------------------------------------------------------------------
// UE2 FArchive TCP packet framing
// ---------------------------------------------------------------------------

// Send a framed packet: 4-byte LE length prefix + payload in a single send.
// OpenSpy's server lacks TCP stream reassembly, so the length and payload
// MUST arrive in the same read callback — combine them into one buffer.
static bool send_packet(socket_t sock, const WriteBuffer& buf) {
    int32_t len = static_cast<int32_t>(buf.size());
    std::vector<uint8_t> frame(4 + buf.size());
    std::memcpy(frame.data(), &len, 4);
    std::memcpy(frame.data() + 4, buf.data(), buf.size());
    return tcp_send_all(sock, frame.data(), frame.size());
}

// Receive a framed packet: read 4-byte LE length, then payload
static bool recv_packet(socket_t sock, std::vector<uint8_t>& out, int timeout_sec = 10) {
    uint8_t len_buf[4];
    if (!tcp_recv_exact(sock, len_buf, 4, timeout_sec))
        return false;
    int32_t len;
    std::memcpy(&len, len_buf, 4);
    if (len <= 0 || len > 1024 * 1024) return false; // sanity check

    out.resize(len);
    return tcp_recv_exact(sock, out.data(), len, timeout_sec);
}

// ---------------------------------------------------------------------------
// IP address conversion
// ---------------------------------------------------------------------------

static std::string ip_from_dword(uint32_t ip) {
    // The IP comes in network byte order from the master server,
    // then INTEL_ORDER32 converts it to host order.
    // On little-endian (x86), INTEL_ORDER32 is a no-op.
    // The IP is stored as host-order after ntohl.
    // We need to convert it to a dotted string.
    uint8_t a = (ip >> 24) & 0xff;
    uint8_t b = (ip >> 16) & 0xff;
    uint8_t c = (ip >> 8) & 0xff;
    uint8_t d = ip & 0xff;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d.%d.%d.%d", a, b, c, d);
    return buf;
}

// ---------------------------------------------------------------------------
// Debug helpers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Master server query implementation
// ---------------------------------------------------------------------------

#define FAIL(msg) do { \
    result.error = msg; \
    close_socket(sock); \
    return result; \
} while(0)

MasterQueryResult query_master_server(
    const std::string& master_host, uint16_t master_port,
    const std::string& cdkey,
    const std::string& gametype_filter)
{
    MasterQueryResult result;

    socket_t sock = tcp_connect(master_host, master_port, 10);
    if (sock == SOCKET_INVALID) {
        result.error = "failed to connect to " + master_host;
        return result;
    }

    std::vector<uint8_t> pkt;

    // ---- Step 1: Receive challenge ----
    if (!recv_packet(sock, pkt, 10))
        FAIL("failed to receive challenge");

    ReadBuffer challenge_buf(pkt.data(), pkt.size());
    std::string challenge = challenge_buf.read_fstring();

    // ---- Step 2: Send credentials ----
    {
        WriteBuffer wb;
        // CDKeyHash = MD5(cdkey)
        std::string cdkey_hash = md5_hex(cdkey);
        // CDKeyResponse = MD5(cdkey + challenge)
        std::string cdkey_response = md5_hex(cdkey + challenge);
        wb.write_fstring(cdkey_hash);
        wb.write_fstring(cdkey_response);
        wb.write_fstring("UT2K4CLIENT");
        wb.write_int32(3369);
        wb.write_byte(0);
        wb.write_fstring("int");
        wb.write_int32(0);
        wb.write_int32(0);
        wb.write_int32(30);
        wb.write_byte(0);

        if (!send_packet(sock, wb))
            FAIL("failed to send credentials");
    }

    // ---- Step 3: Receive review result ----
    if (!recv_packet(sock, pkt, 10))
        FAIL("failed to receive review");

    ReadBuffer review_buf(pkt.data(), pkt.size());
    std::string review_result = review_buf.read_fstring();

    if (review_result != "APPROVED") {
        result.error = "rejected: " + review_result;
        close_socket(sock);
        return result;
    }

    int32_t mod_rev_level = review_buf.read_int32();
    (void)mod_rev_level;

    // ---- Step 4: Send GlobalMD5 ----
    {
        WriteBuffer wb;
        wb.write_fstring("00000000000000000000000000000000");
        if (!send_packet(sock, wb))
            FAIL("failed to send MD5");
    }

    // ---- Step 5: Receive approval ----
    if (!recv_packet(sock, pkt, 10))
        FAIL("failed to receive approval");

    ReadBuffer approval_buf(pkt.data(), pkt.size());
    std::string approval = approval_buf.read_fstring();

    if (approval != "VERIFIED") {
        result.error = "not verified: " + approval;
        close_socket(sock);
        return result;
    }

    // ---- Step 6: Send query ----
    {
        WriteBuffer wb;
        wb.write_byte(0); // CTM_Query
        if (gametype_filter.empty()) {
            wb.write_compact_index(0);
        } else {
            wb.write_compact_index(1);
            wb.write_fstring("gametype");
            wb.write_fstring(gametype_filter);
            wb.write_byte(0); // QT_Equals
        }
        if (!send_packet(sock, wb))
            FAIL("failed to send query");
    }

    // ---- Step 7: Receive result count ----
    if (!recv_packet(sock, pkt, 15))
        FAIL("failed to receive result count");

    ReadBuffer count_buf(pkt.data(), pkt.size());
    int32_t result_count = count_buf.read_int32();
    uint8_t results_compressed = count_buf.read_byte();

    if (result_count <= 0) {
        result.error = "master returned 0 servers";
        close_socket(sock);
        return result;
    }

    // ---- Step 8: Receive server entries ----
    for (int32_t i = 0; i < result_count; ++i) {
        if (!recv_packet(sock, pkt, 15))
            break;

        ReadBuffer srv_buf(pkt.data(), pkt.size());
        MasterServerEntry entry;

        if (results_compressed) {
            uint32_t ip_raw = srv_buf.read_dword();
            uint16_t port = srv_buf.read_word();
            uint16_t query_port = srv_buf.read_word();

            if (srv_buf.error()) continue;

            entry.ip = ip_from_dword(ntohl(ip_raw));
            entry.port = port;
            entry.query_port = query_port;

            entry.name = srv_buf.read_fstring();
            entry.map_name = srv_buf.read_fstring();
            entry.game_type = srv_buf.read_fstring();
            entry.current_players = srv_buf.read_byte();
            entry.max_players = srv_buf.read_byte();
            entry.flags = srv_buf.read_int32();
            std::string skill = srv_buf.read_fstring();
            (void)skill;

            // Decode gametype index to class name and fix map prefix
            auto dash_pos = entry.map_name.find('-');
            bool has_prefix = (dash_pos == 2 || dash_pos == 3);
            if (entry.game_type == "0") {
                entry.game_type = "xDeathMatch";
                if (!has_prefix) entry.map_name = "DM-" + entry.map_name;
            } else if (entry.game_type == "1") {
                entry.game_type = "xCTFGame";
                if (!has_prefix) entry.map_name = "CTF-" + entry.map_name;
            } else if (entry.game_type == "2") {
                entry.game_type = "xBombingRun";
                if (!has_prefix) entry.map_name = "BR-" + entry.map_name;
            } else if (entry.game_type == "3") {
                entry.game_type = "xTeamGame";
                if (!has_prefix) entry.map_name = "DM-" + entry.map_name;
            } else if (entry.game_type == "4") {
                entry.game_type = "xDoubleDom";
                if (!has_prefix) entry.map_name = "DOM-" + entry.map_name;
            }

        } else {
            entry.ip = srv_buf.read_fstring();
            entry.port = static_cast<uint16_t>(srv_buf.read_int32());
            entry.query_port = static_cast<uint16_t>(srv_buf.read_int32());
            entry.name = srv_buf.read_fstring();
            entry.map_name = srv_buf.read_fstring();
            entry.game_type = srv_buf.read_fstring();
            entry.current_players = srv_buf.read_int32();
            entry.max_players = srv_buf.read_int32();
            srv_buf.read_int32(); // ping
            entry.flags = srv_buf.read_int32();
            srv_buf.read_fstring(); // skill level
        }

        if (!srv_buf.error() && !entry.ip.empty()) {
            result.servers.push_back(std::move(entry));
        }
    }

    close_socket(sock);
    return result;
}

#undef FAIL
