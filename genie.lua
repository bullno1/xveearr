function compileShader(input, output, varyingdef, type)
	local profiles = {
		["v"] = "vs_3_0",
		["f"] = "ps_3_0"
	}
	return {
		input,
		output,
		{ "shaderc", varyingdef },
		{ table.concat({
			"..\\bin\\shaderc.exe",
			"--type", type,
			"-f", "..\\"..input,
			"-o", "..\\"..output,
			"--bin2c",
			"-i", "../deps/bgfx/src",
			"--varyingdef", "..\\"..varyingdef,
			"--platform", "windows",
			"-O", "3",
			"-p", profiles[type]
		}, " ")}
	}
end

solution "xveearr"
	location(_ACTION)
	configurations {"Develop"}
	platforms {"x64"}
	targetdir "bin"
	startproject "xveearr"

	project "xveearr"
		kind "ConsoleApp"
		language "C++"

		includedirs {
			"deps/bx/include",
			"deps/bgfx/include"
		}

		configuration { "vs*" }
			includedirs {
				"deps/bx/include/compat/msvc",
				"gen"
			}

		configuration { "vs*" }
			custombuildtask {
				{ "config.win", "gen/config.h", {"busybox.exe"}, {
					"..\\busybox.exe sh -c ./$(<) > $(@)"
				}},
				compileShader(
					"src/shaders/quad.vsh",
					"gen/shaders/quad.vsh.h",
					"src/shaders/varying.def.sc",
					"v"
				),
				compileShader(
					"src/shaders/quad.fsh",
					"gen/shaders/quad.fsh.h",
					"src/shaders/varying.def.sc",
					"f"
				)
			}

		links {
			"bgfx",
			"SDL2"
		}

		files {
			"src/*.hpp",
			"src/*.inl",
			"src/*.cpp",
			"shaders/*"
		}

		flags {
			"ExtraWarnings",
			"FatalWarnings",
			"OptimizeSpeed",
			"StaticRuntime",
			"Symbols",
			"WinMain",
			"NoEditAndContinue",
			"NoNativeWChar"
		}

	project "bgfx"
		kind "StaticLib"
		language "C++"

		includedirs {
			"deps/bx/include",
			"deps/bgfx/include",
			"deps/bgfx/3rdparty",
			"deps/bgfx/3rdparty/dxsdk/include",
			"deps/bgfx/3rdparty/khronos"
		}

		configuration { "vs*" }
			links {
				"gdi32",
				"psapi",
			}

			includedirs {
				"deps/bx/include/compat/msvc"
			}

		configuration { "vs*" }
			defines {
				"_CRT_SECURE_NO_WARNINGS=1"
			}

		flags {
			"OptimizeSpeed",
			"StaticRuntime",
			"Symbols"
		}

		files {
			"deps/bgfx/src/amalgamated.cpp"
		}

	project "shaderc"
		kind "ConsoleApp"
		language "C++"

		local BGFX_DIR = "deps/bgfx"
		local BX_DIR = "deps/bx"
		local GLSL_OPTIMIZER = path.join(BGFX_DIR, "3rdparty/glsl-optimizer")
		local FCPP_DIR = path.join(BGFX_DIR, "3rdparty/fcpp")

		includedirs {
			path.join(GLSL_OPTIMIZER, "src"),
		}

		removeflags {
			"OptimizeSpeed",
		}

		configuration { "vs*" }
			includedirs {
				path.join(GLSL_OPTIMIZER, "src/glsl/msvc"),
			}

			defines { -- glsl-optimizer
				"__STDC__",
				"__STDC_VERSION__=199901L",
				"strdup=_strdup",
				"alloca=_alloca",
				"isascii=__isascii",
			}

			buildoptions {
				"/wd4996" -- warning C4996: 'strdup': The POSIX name for this item is deprecated. Instead, use the ISO C++ conformant name: _strdup.
			}

		configuration { "vs*" }
			includedirs {
				"deps/bx/include/compat/msvc"
			}

		defines { -- fcpp
			"NINCLUDE=64",
			"NWORK=65536",
			"NBUFF=65536",
			"OLD_PREPROCESSOR=0",
		}

		includedirs {
			path.join(BX_DIR, "include"),
			path.join(BGFX_DIR, "include"),

			path.join(BGFX_DIR, "3rdparty/dxsdk/include"),
			FCPP_DIR,

			path.join(GLSL_OPTIMIZER, "include"),
			path.join(GLSL_OPTIMIZER, "src/mesa"),
			path.join(GLSL_OPTIMIZER, "src/mapi"),
			path.join(GLSL_OPTIMIZER, "src/glsl"),
		}

		files {
			path.join(BGFX_DIR, "tools/shaderc/**.cpp"),
			path.join(BGFX_DIR, "tools/shaderc/**.h"),
			path.join(BGFX_DIR, "src/vertexdecl.**"),

			path.join(FCPP_DIR, "**.h"),
			path.join(FCPP_DIR, "cpp1.c"),
			path.join(FCPP_DIR, "cpp2.c"),
			path.join(FCPP_DIR, "cpp3.c"),
			path.join(FCPP_DIR, "cpp4.c"),
			path.join(FCPP_DIR, "cpp5.c"),
			path.join(FCPP_DIR, "cpp6.c"),
			path.join(FCPP_DIR, "cpp6.c"),

			path.join(GLSL_OPTIMIZER, "src/mesa/**.c"),
			path.join(GLSL_OPTIMIZER, "src/glsl/**.cpp"),
			path.join(GLSL_OPTIMIZER, "src/mesa/**.h"),
			path.join(GLSL_OPTIMIZER, "src/glsl/**.c"),
			path.join(GLSL_OPTIMIZER, "src/glsl/**.cpp"),
			path.join(GLSL_OPTIMIZER, "src/glsl/**.h"),
			path.join(GLSL_OPTIMIZER, "src/util/**.c"),
			path.join(GLSL_OPTIMIZER, "src/util/**.h"),
		}

		removefiles {
			path.join(GLSL_OPTIMIZER, "src/glsl/glcpp/glcpp.c"),
			path.join(GLSL_OPTIMIZER, "src/glsl/glcpp/tests/**"),
			path.join(GLSL_OPTIMIZER, "src/glsl/glcpp/**.l"),
			path.join(GLSL_OPTIMIZER, "src/glsl/glcpp/**.y"),
			path.join(GLSL_OPTIMIZER, "src/glsl/ir_set_program_inouts.cpp"),
			path.join(GLSL_OPTIMIZER, "src/glsl/main.cpp"),
			path.join(GLSL_OPTIMIZER, "src/glsl/builtin_stubs.cpp"),
		}
