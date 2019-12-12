from conans import ConanFile, tools

class dhcp_service(ConanFile):
    name = "dhcp_service"
    version = "0.1"
    settings = None
    description = "dhcp service"
    url = "None"
    license = "None"
    author = "None"
    topics = None

    def package(self):
        self.copy("*")

    def package_info(self):
        self.cpp_info.libs = tools.collect_libs(self)