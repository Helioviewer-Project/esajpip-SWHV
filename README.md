
# esajpip

esajpip serves JP2 and JPX files over JPIP for JHelioviewer. The repository
includes the required log4cpp and libconfig sources. It uses the system GLib and
zlib libraries.

## Build and install

On Debian 12, install the build dependencies with:

```sh
sudo apt-get install build-essential cmake pkg-config libglib2.0-dev zlib1g-dev
```

Configure an out-of-source build. The image and log directories are written to
the installed `server.cfg`:

```sh
cmake -S . -B build \
    -DCMAKE_INSTALL_PREFIX="$HOME/esajpip" \
    -DSWHV_PORT_JPIP=8090 \
    -DSWHV_DIR_IMAGE="$HOME/esajpip/images" \
    -DSWHV_DIR_LOG="$HOME/esajpip/log"
cmake --build build
cmake --install build
mkdir -p "$HOME/esajpip/images" "$HOME/esajpip/log"
```

Run the tests before installation with:

```sh
ctest --test-dir build --output-on-failure
```

## Operation

The executable reads `server.cfg` from its current directory. Run it from the
installed server directory, either in a terminal or under the process supervisor
used on the host:

```sh
cd "$HOME/esajpip/server/esajpip"
./esajpip start
```

The management commands must be run from the same directory:

```sh
./esajpip status
./esajpip record
./esajpip stop
```

`record` prints a sample every five seconds until interrupted. Passing a file
name records the samples through the configured logger. `stop child` restarts
only the serving child while leaving the listening parent running.
