from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps

class VOGConan(ConanFile):
    name = "VOG"
    author = "Vladyslav Odobesku (positivcheg94@gmail.com)"
    topics = ("Graphics", "Vulkan")

    build_policy = "always"
    shared = False
    settings = ["os", "compiler", "arch", "build_type"]
    requires = [
        # generic c++
        "boost/1.78.0",
        "glm/0.9.9.8",
        # graphics
        "vulkan-headers/1.3.204.0",
        "sdl/2.0.18",
        "gtest/1.11.0",
        # private dependencies
        ("nlohmann_json/3.10.5", "private"),
        ("shaderc/2021.1", "private"),
        ("spirv-cross/cci.20211113", "private"),
        ("vulkan-memory-allocator/2.3.0", "private"),
    ]

    def config_options(self):
        boost = self.options["boost"]
        boost.without_context = True
        boost.without_contract = True
        boost.without_coroutine = True
        boost.without_fiber = True
        boost.without_json = True
        boost.without_graph = True
        boost.without_graph_parallel = True
        boost.without_locale = True
        boost.without_math = True
        boost.without_nowide = True
        boost.without_python = True
        boost.without_serialization = True
        boost.without_test = True
        boost.without_type_erasure = True
        boost.without_wave = True
        boost.zlib = False
        boost.numa = False
        boost.bzip2 = False
        boost.lzma = False

        self.options["gtest"].build_gmock = False

        self.options["glslang"].build_executables = False

        spirv_cross = self.options["spirv-cross"]
        spirv_cross.build_executable = False
        spirv_cross.hlsl = False
        spirv_cross.msl = False
        spirv_cross.cpp = False
        spirv_cross.c_api = False
        spirv_cross.util = False

        sdl = self.options["sdl"]
        sdl.sdl2main = False
        sdl.opengl = False
        sdl.opengles = False
        sdl.vulkan = False
        if self.settings.os == "Windows":
            sdl.directx = False

    def configure(self):
        self.settings.compiler.cppstd = 20

    def imports(self):
        pass

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        if self.should_configure:
            cmake.configure()
        if self.should_build:
            cmake.build()
        if self.should_test:
            cmake.test()
        
        