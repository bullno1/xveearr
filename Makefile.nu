CPP_FLAGS ?= -g -Wall -Wextra -Werror -pedantic -pthread

-import cpp.nu

all: bin/xveearr ! live

bin/xveearr: << CPP_FLAGS
	${NUMAKE} exe:$@ \
		sources="`find src -name '*.cpp' -or -name '*.c'`" \
		cpp_flags="${CPP_FLAGS} -isystem deps/bgfx/include -isystem deps/bx/include `pkg-config --cflags x11 sdl2`" \
		link_flags=" `pkg-config --libs x11 sdl2` -ldl -pthread -lGL" \
		libs="bin/libbgfx.a"

bin/libbgfx.a:
	${NUMAKE} static-lib:$@ \
		cpp_flags="-Ideps/bgfx/include -Ideps/bgfx/3rdparty -Ideps/bgfx/3rdparty/khronos -Ideps/bx/include" \
		sources="deps/bgfx/src/amalgamated.cpp"
