from conans import tools, CMake, ConanFile


class TconcurrentConan(ConanFile):
    name = "tconcurrent"
    version = "dev"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "coroutinests": [True, False],
        "coverage": [True, False],
        "with_sanitizer_support": [True, False],
    }
    default_options = (
        "shared=False",
        "fPIC=True",
        "coroutinests=False",
        "coverage=False",
        "with_sanitizer_support=False",
    )
    exports_sources = "CMakeLists.txt", "src/*", "include/*", "test/*"
    generators = "cmake"

    @property
    def should_build_tests(self):
        develop = self.develop
        cross_building = tools.cross_building(self.settings)
        emscripten = self.settings.os == "Emscripten"
        return develop and (not cross_building) and (not emscripten)

    def requirements(self):
        self.requires("boost/1.76.0")
        self.requires("enum-flags/0.1a")
        self.requires("function2/4.1.0")

    def build_requirements(self):
        if self.should_build_tests:
            self.build_requires("doctest/2.3.8")

    def configure(self):
        if self.options.coroutinests and self.settings.compiler != "clang":
            raise Exception("Coroutines TS is only supported by clang at the moment")

    def imports(self):
        # We have to copy dependencies DLLs for unit tests
        self.copy("*.dll", dst="bin", src="bin")
        self.copy("*.dylib", dst="bin", src="lib")
        self.copy("*.so*", dst="bin", src="lib")

    def build(self):
        cmake = CMake(self)
        cmake.definitions["TCONCURRENT_SANITIZER"] = self.options.with_sanitizer_support
        cmake.definitions["TCONCURRENT_COROUTINES_TS"] = self.options.coroutinests
        cmake.definitions["BUILD_SHARED_LIBS"] = self.options.shared
        cmake.definitions["CMAKE_POSITION_INDEPENDENT_CODE"] = self.options.fPIC
        cmake.definitions["BUILD_TESTING"] = self.should_build_tests
        cmake.definitions["WITH_COVERAGE"] = self.options.coverage
        cmake.configure()
        cmake.build()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["tconcurrent"]
        if self.options.with_sanitizer_support:
            self.cpp_info.defines.append("TCONCURRENT_SANITIZER=1")
        if self.options.coroutinests:
            self.cpp_info.defines.append("TCONCURRENT_COROUTINES_TS=1")
            self.cpp_info.cppflags.append("-fcoroutines-ts")

    def package_id(self):
        del self.info.options.coroutinests
