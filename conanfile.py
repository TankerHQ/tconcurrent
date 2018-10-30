from conans import tools, CMake, ConanFile


class TconcurrentConan(ConanFile):
    name = "tconcurrent"
    version = "0.15"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False], "sanitizer": ["address", None]}
    default_options = "shared=False", "fPIC=True", "sanitizer=None"
    repo_url = "ssh://git@10.100.0.1/Tanker/tconcurrent"
    generators = "cmake"
    exports_sources = "CMakeLists.txt", "include/*", "src/*", "test/*"

    @property
    def sanitizer_flag(self):
        if self.options.sanitizer:
            return "-fsanitize=%s" % self.options.sanitizer
        return None

    def requirements(self):
        self.requires("Boost/1.66.0-r5@tanker/stable")
        self.requires("enum-flags/6df748a@tanker/stable")

    def build_requirements(self):
        self.build_requires("doctest/1.2.6@tanker/stable")

    def configure(self):
        if tools.cross_building(self.settings):
            del self.settings.compiler.libcxx

        if self.settings.os == "Emscripten":
            self.options["Boost"].with_ssl = False
            self.options["Boost"].header_only = True
        else:
            self.options["Boost"].shared = self.options.shared
            if self.options["Boost"].without_context:
                raise Exception("tconcurrent requires Boost.Context")


    def imports(self):
        # We have to copy dependencies DLLs for unit tests
        self.copy("*.dll", dst="bin", src="bin")
        self.copy("*.dylib", dst="bin", src="lib")
        self.copy("*.so*", dst="bin", src="lib")

    def build(self):
        cmake = CMake(self)
        cmake.verbose=True
        if self.options.sanitizer:
            cmake.definitions["CONAN_CXX_FLAGS"] = self.sanitizer_flag
        if self.options.shared:
            cmake.definitions["BUILD_SHARED_LIBS"] = "ON"
        if self.options.fPIC:
            cmake.definitions["CMAKE_POSITION_INDEPENDENT_CODE"] = "ON"
        cmake.configure()
        cmake.build()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["tconcurrent"]
        if self.options.sanitizer:
            self.cpp_info.exelinkflags = [self.sanitizer_flag]
            self.cpp_info.sharedlinkflags = [self.sanitizer_flag]
