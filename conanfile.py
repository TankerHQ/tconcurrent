from conans import tools, CMake, ConanFile
import platform
import os


class TconcurrentConan(ConanFile):
    name = "tconcurrent"
    version = "0.5"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = "shared=False", "fPIC=True"
    repo_url = "ssh://git@10.100.0.1/Tanker/tconcurrent"
    generators = "cmake"
    exports_sources = "CMakeLists.txt", "include/*", "src/*", "test/*"

    def requirements(self):
        self.requires("Boost/1.66.0@%s/%s" % (self.user, self.channel))
        self.requires("libcurl/7.53.0@%s/%s" % (self.user, self.channel))

    def build_requirements(self):
        self.build_requires("doctest/1.2.6@%s/%s" % (self.user, self.channel))

    def configure(self):
        if tools.cross_building(self.settings):
            del self.settings.compiler.libcxx

        if self.options["Boost"].without_context:
            raise Exception("tconcurrent requires Boost.Context")
        self.options["Boost"].shared = self.options.shared
        self.options["libcurl"].shared = self.options.shared

    def imports(self):
        # We have to copy dependencies DLLs for unit tests
        self.copy("*.dll", dst="bin", src="bin")
        self.copy("*.dylib", dst="bin", src="lib")
        self.copy("*.so*", dst="bin", src="lib")

    def build(self):
        cmake = CMake(self)
        if self.options.shared:
            cmake.definitions["BUILD_SHARED_LIBS"] = "ON"
        if self.options.fPIC:
            cmake.definitions["CMAKE_POSITION_INDEPENDENT_CODE"] = "ON"
        cmake.configure()
        cmake.build()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["tconcurrent"]
