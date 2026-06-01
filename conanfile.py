from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeConfigDeps, cmake_layout
from conan.tools.files import copy
from conan.tools.build import check_min_cppstd


class VOGConan(ConanFile):
    name = "VOG"
    author = "Vladyslav Odobesku (odobesku.vladislav@gmail.com)"
    topics = ("Graphics", "Vulkan")

    settings = ["os", "compiler", "arch", "build_type"]
    vulkan_version = "1.4.350.0"

    def export_sources(self):
        copy(self, "CMakeLists.txt", self.recipe_folder, self.export_sources_folder)
        copy(self, "src/*", self.recipe_folder, self.export_sources_folder)

    def requirements(self):
        self.requires("boost/1.91.0")
        self.requires("glm/0.9.9.8")

        self.requires("sdl/3.4.8")
        self.requires("spdlog/1.15.3")

        # Private dependencies
        self.requires("nlohmann_json/3.10.5", transitive_headers=False, transitive_libs=False)
        self.requires(f"vulkan-headers/{self.vulkan_version}", transitive_headers=False, transitive_libs=False)
        self.requires("vulkan-memory-allocator/3.3.0", transitive_headers=False, transitive_libs=False)
        self.requires(f"spirv-cross/{self.vulkan_version}", transitive_headers=False, transitive_libs=False)
        self.requires(f"spirv-tools/{self.vulkan_version}", transitive_headers=False, transitive_libs=False)
        self.requires(f"glslang/{self.vulkan_version}", transitive_headers=False, transitive_libs=False)

    def build_requirements(self):
        self.tool_requires("cmake/4.3.2")
        self.tool_requires("ninja/1.13.2")

        self.test_requires("gtest/1.17.0")

    def validate(self):
        check_min_cppstd(self, 20, gnu_extensions=False)

    def configure(self):
        self.options["spirv-tools"].build_executables = False
        self.options["spirv-cross"].build_executable = False
        self.options["glslang"].build_executables = False

        self.options["sdl"].opengl = False
        self.options["sdl"].opengles = False

        self.options["boost"].without_cobalt = True

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.user_presets_path = 'ConanPresets.json'
        tc.generate()

        cd = CMakeConfigDeps(self)
        cd.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

        if not self.conf.get("tools.build:skip_test", default=False):
            cmake.test()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.name = self.name

        self.cpp_info.set_property("cmake_file_name", "VOG")
        self.cpp_info.filenames["cmake_find_package"] = "VOG"
        self.cpp_info.filenames["cmake_find_package_multi"] = "VOG"
        self.cpp_info.names["cmake_find_package"] = "VOG"
        self.cpp_info.names["cmake_find_package_multi"] = "VOG"

        self.cpp_info.components["Graphics"].libs = ["VOG.Graphics"]
        self.cpp_info.components["Graphics"].set_property("cmake_target_name", "VOG::Graphics")

        self.cpp_info.components["Engine"].libs = ["VOG.Engine"]
        self.cpp_info.components["Engine"].set_property("cmake_target_name", "VOG::Engine")
        self.cpp_info.components["Engine"].requires = ["Graphics"]
