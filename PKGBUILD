# Maintainer: zenakuten
pkgname=utquery-git
pkgver=r15.fc3965e
pkgrel=1
pkgdesc='UT2004 Server Browser'
arch=('x86_64')
url='https://github.com/zenakuten/utquery'
license=('custom')
depends=('sdl3')
makedepends=('git' 'cmake' 'ninja' 'curl' 'zip' 'unzip' 'tar' 'pkgconf')
provides=('utquery')
conflicts=('utquery')
source=("git+https://github.com/zenakuten/utquery.git")
sha256sums=('SKIP')

pkgver() {
    cd utquery
    printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

prepare() {
    [[ -d vcpkg ]] || git clone https://github.com/microsoft/vcpkg.git
    vcpkg/bootstrap-vcpkg.sh -disableMetrics
}

build() {
    cmake -S utquery -B build \
        -DCMAKE_TOOLCHAIN_FILE="$srcdir/vcpkg/scripts/buildsystems/vcpkg.cmake" \
        -DCMAKE_BUILD_TYPE=Release
    cmake --build build
}

package() {
    install -Dm755 build/utquery "$pkgdir/usr/bin/utquery"
}
