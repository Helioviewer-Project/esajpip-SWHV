# log4cpp

[![Build latest master with CMake, run Ctest on Linux, Windows, MacOS](https://github.com/log4cpp/log4cpp/actions/workflows/sync-with-sf_net-simple.yml/badge.svg)](https://github.com/log4cpp/log4cpp/actions/workflows/sync-with-sf_net-simple.yml)

Log4cpp is library of C++ classes for flexible logging to files, syslog, IDSA and other destinations. It is modeled
after the [Log4j](http://jakarta.apache.org/log4j) Java library, staying as close to their API as is reasonable.

## Features

- Multiple log levels (DEBUG, INFO, WARN, ERROR, FATAL, ...)
- Thread-safe logging
- Customizable log format
- Console and file appenders, including:
    - Size-based rolling files
    - Daily rolling files
    - Syslog
    - IDSA
    - SMTP
    - Windows Event Log
- No external dependencies
- Cross-platform (Linux, macOS, Windows)
- Build support with Autotools, CMake

---

## Installation

### Download source code

#### Option 1 - Download a Release from [SourceForge](http://sourceforge.net/project/showfiles.php?group_id=15190)

Choose the latest stable version or a recent release candidate.

#### Option 2 - Clone the Git Repository [SourceForge](https://sourceforge.net/p/log4cpp/codegit/) or [GitHub](https://github.com/log4cpp/log4cpp)

Get the latest version of the source code:

```sh
git clone https://github.com/log4cpp/log4cpp.git
```

### Build with Autotools

```sh
./autogen.sh  # optional
./configure
make
make check
make install
```

### Build with CMake

```sh
cmake -S . -B build_release -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local -DLOG4CPP_BUILD_EXAMPLES=ON
cmake --build build_release
cd build_release/tests; ctest
cd ../..; cmake --install build_release
```

### Other Build Methods

Additional build methods are listed on the project website (http://log4cpp.sf.net/).

### Using in your CMake project

[CMake usage](examples/cmake_usages) examples are provided for two scenarios:

* importing a CMake-installed log4cpp library: [log4cpp_as_package](examples/cmake_usages/log4cpp_as_package)
* using log4cpp as a CMake subproject : [log4cpp_as_subproject](examples/cmake_usages/log4cpp_as_subproject)

Building examples is enabled via the `LOG4CPP_BUILD_EXAMPLES` configuration option (`-DLOG4CPP_BUILD_EXAMPLES=ON`).
Both shared and static library variants are demonstrated.
The CMake usage examples can be built either:

* as part of the `log4cpp` build (when LOG4CPP_BUILD_EXAMPLES is enabled), or
* as standalone CMake applications that import or include `log4cpp` from their own directories. To build an example as a
  standalone application, change into the example directory and build it separately with CMake.
  ```sh
  cd examples/cmake_usages/log4cpp_as_subproject 
  cmake -S . -B build
  cmake --build build
  ```
  ```sh
  cd examples/cmake_usages/log4cpp_as_package    
  cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/.local # possible after log4cpp was installed with CMake  
  cmake --build build
  ```