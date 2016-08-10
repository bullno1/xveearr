CPP_FLAGS ?= -g -Wall -Wextra -Werror -std=c++11 -pedantic -Wno-switch -pthread -O2

-import cpp.nu
-import bgfx.nu

all: bin/xveearr ! live

bin/xveearr: shaders config << CPP_FLAGS BUILD_DIR
	SYS_LIBS="x11-xcb xcb xcb-composite xcb-util xcb-res xcb-ewmh xcb-keysyms sdl2 gl"
	FLAGS=" \
		-isystem deps/bgfx/include \
		-isystem deps/bx/include \
		$(pkg-config --cflags ${SYS_LIBS}) \
	"
	${NUMAKE} exe:$@ \
		sources="$(find src \( -name '*.cpp' -or -name '*.c' \))" \
		cpp_flags="${CPP_FLAGS} ${FLAGS} -I${BUILD_DIR}/src" \
		link_flags="$(pkg-config --libs ${SYS_LIBS}) -ldl -pthread" \
		libs="bin/libbgfx.a"

config: ${BUILD_DIR}/src/config.h << BUILD_DIR ! live

${BUILD_DIR}/src/config.h: ! live
	printf '#define XVEEARR_VERSION "%s"\n' "$(./version)" >> $@

shaders: ! live
	shaders=$(find src \( -name '*.vsh' -or -name '*.fsh' \) -exec echo shaderc:{} \;)
	${NUMAKE} --depend ${shaders}
