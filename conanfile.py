from conans import ConanFile, tools

class dhcp_serviceConan(ConanFile):
    name = "dhcp_service"
    version = "0.1"
    settings = None
    description = "DHCP service"
    url = "None"
    license = "None"
    author = "Hokku"
    topics = None

    def package(self):
        self.copy("*")

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)