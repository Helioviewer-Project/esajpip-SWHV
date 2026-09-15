# ESA JPIP server

`esajpip` is an open-source JPIP server for Unix-like systems. It was developed
for the Helioviewer project and streams solar imagery stored in JP2 and JPX
files to JHelioviewer.

The repository includes the required log4cpp and libconfig sources and links
against the system GLib and zlib libraries.

See [Connections and JPIP channels](CHANNELS.md) for the server's connection
ownership, channel routing, timeout, and cleanup model.

See [JPIP support profile](JPIP_PROFILE.md) for the supported transport,
requests, responses, and JPEG 2000 source formats, including deliberate
limitations.

## Build and install

On Debian 12, install the build dependencies with:

```sh
sudo apt-get install build-essential cmake pkg-config libglib2.0-dev zlib1g-dev
```

Configure a separate build directory. CMake writes the selected image and log
paths to the installed `server.cfg`:

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
installed server directory, either in a terminal or under the host's process
supervisor:

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

`identification_time_out` limits how long a new connection has to send a valid
initial JPIP request. Until the request is recognized, the parent retains only
the socket. It does not create a serving thread or allocate JPEG 2000 state.
After admission, `time_out` limits channel inactivity. It closes an idle channel
whether an HTTP connection is attached or the channel is waiting for a
replacement. Values of `0` and `-1` disable this timeout.

`record` prints a status sample every five seconds until interrupted. Pass a
file name to send the samples to the configured logger. `stop child` restarts
the serving child without stopping the listening parent.

## License

The ESA JPIP server is licensed under the Common Development and Distribution
License 1.0. See [LICENSE](LICENSE).
