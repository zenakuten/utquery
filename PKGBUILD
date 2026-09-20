# Maintainer: zenakuten
pkgname=utquery-git
pkgver=r27.6b53ec8
pkgrel=1
pkgdesc='UT2004 Server Browser'
arch=('x86_64')
url='https://github.com/zenakuten/utquery'
license=('custom')
depends=('sdl3')
makedepends=('git' 'cmake' 'ninja')
provides=('utquery')
conflicts=('utquery')
source=(
    "git+https://github.com/zenakuten/utquery.git"
    "imgui-1.91.9.tar.gz::https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.tar.gz"
    "nlohmann-json-3.12.0.tar.gz::https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz"
)
sha256sums=(
    'SKIP'
    '3872a5f90df78fced023c1945f4466b654fd74573370b77b17742149763a7a7c'
    '4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187'
)

pkgver() {
    cd utquery
    printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

build() {
    cmake --fresh -S utquery -B build -G Ninja \
        -DIMGUI_SOURCE_DIR="$srcdir/imgui-1.91.9" \
        -DNLOHMANN_JSON_INCLUDE_DIR="$srcdir/json-3.12.0/include" \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build build
}

package() {
    install -Dm755 build/utquery "$pkgdir/usr/bin/utquery"
}
