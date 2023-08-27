from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps

class VOGConan(ConanFile):
    name = "VOG"
    author = "Vladyslav Odobesku (positivcheg94@gmail.com)"
    topics = ("Graphics", "Vulkan")

    settings = ["os", "compiler", "arch", "build_type"]

    def requirements(self):
        self.requires("boost/1.78.0")
        self.requires("glm/0.9.9.8")

        self.requires("vulkan-headers/1.3.236.0")
        self.requires("vulkan-memory-allocator/3.0.1@#5ccb73a6a1f2aefccfe9c586c5e77662")
        self.requires("sdl/2.26.5")
        self.requires("spdlog/1.10.0")

        # Private dependencies
        self.requires("nlohmann_json/3.10.5", transitive_headers=False, transitive_libs=False)
        self.requires("spirv-cross/1.3.236.0", transitive_headers=False, transitive_libs=False)
        self.requires("spirv-tools/1.3.236.0", transitive_headers=False, transitive_libs=False)
        self.requires("glslang/1.3.236.0", transitive_headers=False, transitive_libs=False)

    def build_requirements(self):
        self.test_requires("gtest/1.11.0")

    def configure(self):
        self.settings.compiler.cppstd = 20

        self.options["spirv-tools"].build_executables = False
        self.options["spirv-cross"].build_executable = False
        self.options["glslang"].build_executables = False

    def imports(self):
        pass

    def generate(self):
        CMakeToolchain(self).generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build() 
