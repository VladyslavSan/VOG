from conans import ConanFile, CMake

class VOGConan(ConanFile):
    build_policy = "always"
    shared = False
    settings = ["os", "compiler", "build_type", "arch"]
    requires = [
        "boost/1.78.0",
        "glm/0.9.9.8",
        "gtest/1.11.0",
        "shaderc/2021.1",
        "spirv-cross/cci.20211113",
        "vulkan-headers/1.2.198.0",
        "vulkan-memory-allocator/2.3.0",
        "sdl/2.0.18"
        ]
    
    generators = ["cmake", "cmake_find_package", "cmake_paths", "cmake_multi"]

    default_options = {
        "boost:without_context" : True,
        "boost:without_contract" : True,
        "boost:without_coroutine" : True,
        "boost:without_fiber" : True,
        "boost:without_json" : True,
        "boost:without_graph" : True,
        "boost:without_graph_parallel" : True,
        "boost:without_locale" : True,
        "boost:without_python" : True,
        "boost:without_test" : True,
        "boost:without_type_erasure" : True,
        "boost:without_wave" : True,
        "gtest:build_gmock" : False,
        "spirv-cross:build_executable" : False,
        "spirv-cross:hlsl" : False,
        "spirv-cross:msl" : False,
        "spirv-cross:cpp" : False,
        "spirv-cross:c_api" : False,
        "spirv-cross:util" : False,
        "sdl:sdl2main" : False,
        "sdl:opengl" : False,
        "sdl:opengles" : False,
        "sdl:vulkan" : False
        }

    def config_options(self):
        if self.settings.os == "Windows":
            self.options["sdl"].directx = False
    
    def imports(self):
        pass

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()