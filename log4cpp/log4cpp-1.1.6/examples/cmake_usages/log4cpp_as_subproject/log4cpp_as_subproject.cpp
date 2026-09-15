#include <filesystem>
#include <log4cpp/Category.hh>

#include "log4cpp/PropertyConfigurator.hh"

// The log4cpp CMake project is included as a subdirectory,
// and the entire log4cpp library is built as part of this Cmake project.
int main() {
    std::filesystem::path initFile = std::filesystem::current_path() / "resources" / "log4cpp.properties";
    std::string initFileName = initFile.string();

    log4cpp::PropertyConfigurator::configure(initFileName);
    log4cpp::Category &root = log4cpp::Category::getRoot();

    root.info("Hello, Hello, %s %s library as cmake subdirectory!!",
        LOG4CPP_PACKAGE_NAME, LOG4CPP_PACKAGE_VERSION);
    return 0;
}
