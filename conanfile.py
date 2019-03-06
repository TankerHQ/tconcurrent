from conans import tools, CMake, ConanFile
import os


class TconcurrentConan(ConanFile):
    name = "tconcurrent"
    version = "0.17"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "sanitizer": ["address", None],
        "coroutinests": [True, False],
    }
    default_options = (
        "shared=False",
        "fPIC=True",
        "sanitizer=None",
        "coroutinests=False",
    )
    exports_sources = "CMakeLists.txt", "src", "include", "test"
    generators = "cmake"

    @property
    def sanitizer_flag(self):
        if self.options.sanitizer:
            return "-fsanitize=%s" % self.options.sanitizer
        return None

    def requirements(self):
        self.requires("Boost/1.68.0@tanker/testing")
        self.requires("enum-flags/0.1a@tanker/testing")

    def configure(self):
        if self.settings.os == "Emscripten":
            self.options["Boost"].header_only = True
        else:
            self.options["Boost"].shared = self.options.shared
            if self.options["Boost"].without_context:
                raise Exception("tconcurrent requires Boost.Context")

    def source(self):
        self.run("git clone %s --branch v%s" % (self.repo_url, self.version))

    def imports(self):
        # We have to copy dependencies DLLs for unit tests
        self.copy("*.dll", dst="bin", src="bin")
        self.copy("*.dylib", dst="bin", src="lib")
        self.copy("*.so*", dst="bin", src="lib")

    def build(self):
        cmake = CMake(self)
        if self.options.sanitizer:
            cmake.definitions["CONAN_CXX_FLAGS"] = self.sanitizer_flag
        cmake.definitions["BUILD_SHARED_LIBS"] = self.options.shared
        cmake.definitions["CMAKE_POSITION_INDEPENDENT_CODE"] = self.options.fPIC
        cmake.definitions["BUILD_TESTING"] = "OFF"
        cmake.build()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["tconcurrent"]
        if self.options.sanitizer:
            self.cpp_info.exelinkflags = [self.sanitizer_flag]
            self.cpp_info.sharedlinkflags = [self.sanitizer_flag]
        if self.options.coroutinests:
            self.cpp_info.defines.append("TCONCURRENT_COROUTINES_TS=1")

    def package_id(self):
        self.info.options.coroutinests = "ANY"
