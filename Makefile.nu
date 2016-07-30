CPP_FLAGS ?= -g -Wall -Wextra -Werror -pedantic -pthread -O2

-import cpp.nu
-import bgfx.nu

all: bin/xveearr ! live

bin/xveearr: shaders << CPP_FLAGS BUILD_DIR
	SYS_LIBS="x11 xcomposite sdl2 gl"
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

shaders: ! live
	shaders=$(find src \( -name '*.vsh' -or -name '*.fsh' \) -exec echo shaderc:{} \;)
	${NUMAKE} --depend ${shaders}
