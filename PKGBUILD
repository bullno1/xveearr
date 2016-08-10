pkgname=xveearr
pkgrel=1
pkgver=r35.234acda
pkgdesc="Virtual Reality window manager"
arch=('i686' 'x86_64')
license=('BSD')
url="https://github.com/bullno1/xveearr"
depends=('libx11' 'libxcb' 'xcb-util' 'xcb-util-wm' 'xcb-util-keysyms' 'sdl2' 'mesa')
source=('git+https://github.com/bullno1/xveearr.git')
makedepdends=('git' 'python')
md5sums=('SKIP')

pkgver() {
	cd "${pkgname}"
	./version
}

prepare() {
	cd "${pkgname}"
	git submodule update --init --recursive
}

build() {
	cd "${srcdir}/${pkgname}"
	./numake --init
	./numake
}

package() {
	cd "${srcdir}/${pkgname}"
	install -Dm755 bin/xveearr "${pkgdir}/usr/bin/xveearr"
}
