from conans import tools, ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps
from conan.tools.env import VirtualBuildEnv


class TconcurrentConan(ConanFile):
    name = "tconcurrent"
    version = "dev"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "with_coroutines_ts": [True, False],
        "coverage": [True, False],
        "with_sanitizer_support": [True, False],
    }
    default_options = (
        "shared=False",
        "fPIC=True",
        "with_coroutines_ts=False",
        "coverage=False",
        "with_sanitizer_support=False",
    )
    exports_sources = "CMakeLists.txt", "src/*", "include/*", "test/*"
    generators = "CMakeDeps", "VirtualBuildEnv"

    @property
    def should_build_tests(self):
        develop = self.develop
        cross_building = tools.cross_building(self.settings)
        emscripten = self.settings.os == "Emscripten"
        return develop and (not cross_building) and (not emscripten)

    def requirements(self):
        self.requires("boost/1.83.0-r2")
        self.requires("enum-flags/0.1a-r3")
        self.requires("function2/4.1.0-r2")

    def build_requirements(self):
        if self.should_build_tests:
            self.test_requires("doctest/2.4.6-r1")

    def configure(self):
        if self.options.with_coroutines_ts and self.settings.compiler != "clang":
            raise Exception("Coroutines TS is only supported by clang at the moment")

    def imports(self):
        # We have to copy dependencies DLLs for unit tests
        self.copy("*.dll", dst="bin", src="bin")
        self.copy("*.dylib", dst="bin", src="lib")
        self.copy("*.so*", dst="bin", src="lib")

    def generate(self):
        vbe = VirtualBuildEnv(self)
        vbe.generate()

        ct = CMakeToolchain(self)
        ct.variables["TCONCURRENT_SANITIZER"] = self.options.with_sanitizer_support
        ct.variables["TCONCURRENT_COROUTINES_TS"] = self.options.with_coroutines_ts
        ct.variables["BUILD_SHARED_LIBS"] = self.options.shared
        ct.variables["CMAKE_POSITION_INDEPENDENT_CODE"] = self.options.fPIC
        ct.variables["BUILD_TESTING"] = self.should_build_tests
        ct.variables["WITH_COVERAGE"] = self.options.coverage
        ct.generate()

        cd = CMakeDeps(self)
        cd.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["tconcurrent"]
        if self.options.with_sanitizer_support:
            self.cpp_info.defines.append("TCONCURRENT_SANITIZER=1")
        if self.options.with_coroutines_ts:
            self.cpp_info.defines.append("TCONCURRENT_COROUTINES_TS=1")

    def package_id(self):
        del self.info.options.with_coroutines_ts
