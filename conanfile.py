from conans import tools, CMake, ConanFile


class TconcurrentConan(ConanFile):
    name = "tconcurrent"
    version = "dev"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "sanitizer": ["address", None],
        "coroutinests": [True, False],
        "coverage": [True, False],
        "mingw_stdmutex_recursion_checks": [True, False],
    }
    default_options = (
        "shared=False",
        "fPIC=True",
        "sanitizer=None",
        "coroutinests=False",
        "coverage=False",
        "mingw_stdmutex_recursion_checks=False",
    )
    exports_sources = "CMakeLists.txt", "src/*", "include/*", "test/*"
    generators = "cmake"

    @property
    def sanitizer_flag(self):
        if self.options.sanitizer:
            return "-fsanitize=%s" % self.options.sanitizer
        return None

    @property
    def is_mingw(self):
        return self.settings.os == "Windows" and self.settings.compiler == "gcc"

    @property
    def should_build_tests(self):
        develop = self.develop
        cross_building = tools.cross_building(self.settings)
        emscripten = self.settings.os == "Emscripten"
        return develop and (not cross_building) and (not emscripten)

    def config_options(self):
        if not self.is_mingw:
            del self.options.mingw_stdmutex_recursion_checks

    def requirements(self):
        self.requires("Boost/1.71.0@tanker/testing")
        self.requires("enum-flags/0.1a@tanker/testing")
        if self.is_mingw:
            self.requires("mingw-threads/1.0.0@tanker/testing")

    def build_requirements(self):
        if self.should_build_tests:
            self.build_requires("doctest/2.2.3@tanker/testing")

    def configure(self):
        if self.settings.os != "Emscripten" and self.options["Boost"].without_context:
            raise Exception("tconcurrent requires Boost.Context")
        if self.options.coroutinests and self.settings.compiler != "clang":
            raise Exception("Coroutines TS is only supported by clang at the moment")

    def imports(self):
        # We have to copy dependencies DLLs for unit tests
        self.copy("*.dll", dst="bin", src="bin")
        self.copy("*.dylib", dst="bin", src="lib")
        self.copy("*.so*", dst="bin", src="lib")

    def build(self):
        cmake = CMake(self)
        if self.options.sanitizer:
            cmake.definitions["CONAN_CXX_FLAGS"] = self.sanitizer_flag
        if self.is_mingw:
            cmake.definitions["CMAKE_STDMUTEX_RECURSION_CHECKS"] = self.options.mingw_stdmutex_recursion_checks
        cmake.definitions["BUILD_SHARED_LIBS"] = self.options.shared
        cmake.definitions["CMAKE_POSITION_INDEPENDENT_CODE"] = self.options.fPIC
        cmake.definitions["BUILD_TESTING"] = self.should_build_tests
        cmake.definitions["WITH_COVERAGE"] = self.options.coverage
        cmake.configure()
        cmake.build()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["tconcurrent"]
        if self.options.sanitizer:
            self.cpp_info.exelinkflags = [self.sanitizer_flag]
            self.cpp_info.sharedlinkflags = [self.sanitizer_flag]
        if self.options.coroutinests:
            self.cpp_info.defines.append("TCONCURRENT_COROUTINES_TS=1")
            self.cpp_info.cppflags.append("-fcoroutines-ts")

    def package_id(self):
        del self.info.options.coroutinests
