# ESA JPIP server

`esajpip` is an open-source JPIP server for Unix-like systems. It was developed
for the Helioviewer project and streams solar imagery stored in JP2 and JPX
files to JHelioviewer.

The repository includes the required log4cpp sources and links against the
system GLib and zlib libraries.

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
paths to the installed `server.ini`:

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

`server.ini` uses INI syntax. Blank lines and lines beginning with `#` are
ignored.

### Configuration

All four sections must be present. CMake substitutes the three `SWHV_*` values
when it generates the installed file. Directory paths may contain spaces; the
server adds a trailing slash internally when needed.

| Section and setting | Installed value | Meaning |
| --- | --- | --- |
| `listen.port` | `SWHV_PORT_JPIP` | TCP port on which the server listens. The value must be from 1 through 65535. |
| `listen.address` | empty | Local IPv4 address or hostname on which to listen. An empty value listens on all IPv4 interfaces. |
| `jpip.image_directory` | `SWHV_DIR_IMAGE` | Non-empty directory from which requested JP2 and JPX files are opened. |
| `jpip.chunk_size` | `64000` | Response working-buffer size and maximum normal HTTP chunk payload, in bytes. It must be at least 128; the final chunk may be smaller. |
| `connections.admission_timeout` | `3` | Positive number of seconds allowed for a new socket to provide a recognizable JPIP request. This is an absolute admission deadline and is not extended by partial input. |
| `connections.timeout` | `60` | Channel inactivity and socket I/O timeout in seconds. `0` and `-1` disable it; values below `-1` are invalid. |
| `connections.limit` | `500` | Positive connection limit. It limits physical connections in the listening parent and active channels in the serving child independently. |
| `logging.directory` | `SWHV_DIR_LOG` | Directory in which the timestamped server log is created when `logging.file_enabled` is `1`. |
| `logging.file_enabled` | `1` | Set to `1` to write the server log under `logging.directory`, or `0` to keep logging on the console only. |
| `logging.requests` | `0` | Set to `1` to include individual request lines in the log, or `0` to suppress them. Other server messages are unaffected. |

The executable reads `server.ini` from its current directory. Run it from the
installed server directory, either in a terminal or under the host's process
supervisor:

```sh
cd "$HOME/esajpip/server/esajpip"
./esajpip start
```

The management commands must be run from the same directory:

```sh
./esajpip status
./esajpip stop
```

`status` reports whether the server is running and, when active, its parent and
child process IDs and open connection count.

`connections.admission_timeout` limits how long a new connection has to send a valid
initial JPIP request. Until the request is recognized, the parent retains only
the socket. It does not create a serving thread or allocate JPEG 2000 state.
After admission, `connections.timeout` limits channel inactivity. It closes an idle channel
whether an HTTP connection is attached or the channel is waiting for a
replacement. Values of `0` and `-1` disable this timeout.

`stop child` restarts the serving child without stopping the listening parent.

## License

The ESA JPIP server is licensed under the Common Development and Distribution
License 1.0. See [LICENSE](LICENSE).
