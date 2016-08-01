BGFX_DIR ?= deps

bin/shaderc: << CPP_FLAGS
	GLSL_OPTIMIZER="deps/bgfx/3rdparty/glsl-optimizer"
	FCPP="deps/bgfx/3rdparty/fcpp"
	FLAGS=" \
		-Ideps/bx/include \
		-Ideps/bgfx/include \
		-I${FCPP} \
		-I${GLSL_OPTIMIZER}/src \
		-I${GLSL_OPTIMIZER}/include \
		-I${GLSL_OPTIMIZER}/src/mesa \
		-I${GLSL_OPTIMIZER}/src/mapi \
		-I${GLSL_OPTIMIZER}/src/glsl \
		-fno-strict-aliasing \
		-Wno-unused-parameter \
		-DNINCLUDE=64 \
		-DNWORK=65536 \
		-DNBUFF=65536 \
		-DOLD_PREPROCESSOR=0 \
	"
	SOURCES=" \
		$(find deps/bgfx/tools/shaderc \( -name '*.cpp' -or -name '*.c' \)) \
		$(find ${FCPP} -name 'cpp*.c') \
		$(find ${GLSL_OPTIMIZER}/src/mesa \( -name '*.cpp' -or -name '*.c' \)) \
		$(find ${GLSL_OPTIMIZER}/src/glsl \( -name '*.cpp' -or -name '*.c' \) -and -not -name 'main.cpp' ) \
		$(find ${GLSL_OPTIMIZER}/src/util \( -name '*.cpp' -or -name '*.c' \)) \
	"
	${NUMAKE} exe:$@ \
		sources="${SOURCES}" \
		cpp_flags="${FLAGS}" \
		c_flags="${FLAGS}" \
		stable=1

bin/libbgfx.a:
	FLAGS=" \
		-Ideps/bgfx/include \
		-Ideps/bgfx/3rdparty \
		-Ideps/bgfx/3rdparty/khronos \
		-Ideps/bx/include \
		-O2
	"
	${NUMAKE} static-lib:$@ \
		sources="deps/bgfx/src/amalgamated.cpp" \
		cpp_flags="${FLAGS}"
		stable=1

SHADERC = $(readlink -f shaderc)

shaderc:%: ${BUILD_DIR}/%.h << BUILD_DIR ! live

${BUILD_DIR}/%.fsh.h: %.fsh << SHADERC
	${NUMAKE} --depend ${SHADERC}
	${SHADERC} f ${deps} $@

${BUILD_DIR}/%.vsh.h: %.vsh << SHADERC
	${NUMAKE} --depend ${SHADERC}
	${SHADERC} v ${deps} $@
