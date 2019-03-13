from conans import ConanFile, CMake, tools
import os


class TconcurrentTestConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"

    def configure(self):
        if tools.cross_building(self.settings):
            del self.settings.compiler.libcxx

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def imports(self):
        self.copy("*.dll", dst="bin", src="bin")
        self.copy("*.dylib", dst="bin", src="lib")
        self.copy("*.so*", dst="bin", src="lib")

    def test(self):
        if tools.cross_building(self.settings):
            return
        env = ""
        if self.settings.os == "Macos":
            env = "DYLD_FALLBACK_LIBRARY_PATH= DYLD_LIBRARY_PATH=./bin"
        elif self.settings.os == "Linux":
            env = "LD_LIBRARY_PATH=./bin"
        self.run("%s .%sbin%stconcurrent_test" % (env, os.sep, os.sep))
