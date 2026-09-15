#include <filesystem>
#include <log4cpp/Category.hh>

#include "log4cpp/PropertyConfigurator.hh"

// Specify the directory that contains log4cpp’s CMake configuration files when running CMake configuration:
// cmake <...> -Dlog4cpp_DIR=<log4cpp_installation_path>/lib/cmake/log4cpp
int main() {
    std::filesystem::path initFile = std::filesystem::current_path() / "resources" / "log4cpp.properties";
    std::string initFileName = initFile.string();

    log4cpp::PropertyConfigurator::configure(initFileName);
    log4cpp::Category &root = log4cpp::Category::getRoot();

    root.info("Hello, %s %s library as imported cmake package!",
        LOG4CPP_PACKAGE_NAME, LOG4CPP_PACKAGE_VERSION);
    return 0;
}
