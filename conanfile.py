from conan import ConanFile
from conan.tools.cmake import CMakeToolchain


class BacuriDependencies(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    options = {"with_tests": [True, False]}
    default_options = {"with_tests": True, "boost/*:header_only": True}
    requires = "boost/1.83.0", "nlohmann_json/3.11.3"
    generators = "CMakeDeps"

    def build_requirements(self):
        if self.options.with_tests:
            self.test_requires("gtest/1.16.0")

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.generate()
