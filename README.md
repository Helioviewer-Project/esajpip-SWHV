# ESA JPIP server

`esajpip` is an open-source JPIP server for Unix-like systems. It streams solar
imagery from JP2 and JPX files and is designed primarily for JHelioviewer.

It serves JPP streams over HTTP. It does not provide TLS or authentication, so
put it behind a suitable network boundary or reverse proxy if either is needed.

## Build and install

The build requires a C++11 compiler, CMake, pkg-config, GLib, zlib, and POSIX
threads. On Debian 12, install the required packages with:

```sh
sudo apt-get install build-essential cmake pkg-config libglib2.0-dev zlib1g-dev
```

Build outside the source tree. This example installs the server under
`$HOME/esajpip` and writes the chosen image and log directories to the generated
`server.ini`:

```sh
mkdir -p "$HOME/esajpip/images" "$HOME/esajpip/log"
cmake -S . -B build \
    -DCMAKE_INSTALL_PREFIX="$HOME/esajpip" \
    -DSWHV_DIR_IMAGE="$HOME/esajpip/images" \
    -DSWHV_DIR_LOG="$HOME/esajpip/log"
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
```

The executable and `server.ini` are installed in `$HOME/esajpip/bin`. Use any
other installation prefix that suits the host.

## Configure the server

The executable reads `server.ini` from its current working directory. It uses
ordinary INI syntax. Blank lines and lines beginning with `#` are ignored, and
all four sections must be present. Restart the server after changing it.

| Setting | Generated value | Meaning |
| --- | --- | --- |
| `listen.port` | `8900` | TCP port on which the server listens. The value must be from 1 through 65535. |
| `listen.address` | empty | Local IPv4 address or hostname on which to listen. An empty value listens on all IPv4 interfaces. |
| `jpip.image_directory` | `SWHV_DIR_IMAGE` | Non-empty base directory from which requested JP2 and JPX paths are opened. |
| `jpip.chunk_size` | `64000` | Response working-buffer size and maximum normal HTTP chunk payload, in bytes. It must be at least 128. The final chunk may be smaller. |
| `connections.initial_timeout` | `3` | Positive number of seconds allowed for a new socket to provide a recognizable JPIP request. Partial input does not extend the deadline. |
| `connections.timeout` | `60` | Channel inactivity and socket I/O timeout in seconds. `0` and `-1` disable it. Values below `-1` are invalid. |
| `connections.limit` | `500` | Positive limit applied independently to physical connections and active channels. |
| `logging.directory` | `SWHV_DIR_LOG` | Directory for log files when file logging is enabled. It must exist and be writable. |
| `logging.file_enabled` | `1` | Set to `1` to log to a file or `0` to log to standard output. |
| `logging.requests` | `0` | Set to `1` to include request lines in the log. Other server messages are unaffected by this setting. |

Directory paths may contain spaces. The server adds a trailing slash when
needed. `logging.directory` may be empty only when file logging is disabled.

## Prepare image data

File names must end in lowercase `.jp2` or `.jpx`, and every codestream must
contain `PLT` packet-length markers. The server accepts linked JPX movies made
by `hv_jpx_merge` and the compatible `kdu_merge` form. It preserves the order
stored in the JPX, so give the merge tool source frames in timestamp order.

Do not modify a JP2, JPX, or linked source while an active channel may use it.
The server indexes a target once and maps source files again for later
responses. Truncating a mapped file can terminate the serving process. Reusing
a path for different content can leave an existing index pointing at the wrong
bytes. Publish completed sources under new names, then expose the referring JPX
after every source is ready.

Client-supplied URI paths and `target` values are rejected if any path segment
is `..`. They are not percent-decoded. Linked `file://` references are trusted
server-side data and may point outside the image directory. The server also
follows filesystem symbolic links. Run it under an account whose read
permissions are limited to the intended image data.

See [JPIP support profile](JPIP_PROFILE.md) for the accepted JP2 and JPX
structures, request fields, response behavior, and known limits.

## Start and stop the server

Run the installed executable from the directory containing its configuration:

```sh
cd "$HOME/esajpip/bin"
./esajpip
```

The process you start is the supervisor. It keeps the listening socket open and
starts the process that serves clients. `esajpip` stays in the foreground, so
it can be managed directly by your preferred process supervisor. The executable
has no command-line options.

Stop the server by sending `SIGINT` or `SIGTERM` to the supervisor process, or
through the host process supervisor. Do not signal only the serving child: an
unexpected child exit causes the supervisor to start a replacement. A restart
is recorded in the log and delayed by one second to prevent a persistent
failure from causing a tight restart loop. Active JPIP channels are not
recovered.

Once running, a JHelioviewer target has this form:

```text
jpip://server.example:8900/movie.jpx
```

The path is resolved relative to `jpip.image_directory`.

## Timeouts and capacity

A new connection remains cheap until it sends a recognizable JPIP request.
Before then, the server keeps only the socket. `connections.initial_timeout`
closes silent or unrelated connections without starting a channel thread or
allocating JPEG 2000 state.

After channel creation, `connections.timeout` applies both to an attached HTTP
connection and while the channel waits for a replacement connection. Each
active channel owns its JPEG 2000 index, cache model, response buffer, and any
source-file mappings needed by the current response. Set `connections.limit`
according to the largest movies and the memory available on the deployment
host, not only the expected socket count.

See [Connections and JPIP channels](CHANNELS.md) for request examples, HTTP
responses, connection replacement, timeouts, recovery, and server ownership.

## Logging

With file logging enabled, the server creates
`esajpip.<address>.<port>.<timestamp>.log` under `logging.directory`. The active
file rolls at 1 GiB, and one `.1` backup is retained. Different listening
addresses or ports use distinct names.

Serving threads send log records through a bounded, nonblocking queue and never
wait for log-file I/O. If logging falls behind, records are dropped and the next
record reports how many were lost. If the active log can no longer be written
or rotated, file logging stops rather than silently switching to standard
output. The server does not start if it cannot open the configured log file.

## Troubleshooting

Configuration and startup failures are written to standard error. A failure to
initialize the listen socket usually means that the configured address is not
available or the port is already in use. Image parsing and response failures are
written to the server log and close the affected channel. Enable
`logging.requests` temporarily when the request associated with a failure is
needed for diagnosis.

For reproducible integration, performance, sanitizer, and Debian procedures,
see [Testing esajpip](TESTING.md). The [development notes](DEVELOPMENT.md) record
the measurements and compatibility decisions behind the current design.
User-visible release changes are summarized in the [changelog](CHANGELOG.md).

## License

The ESA JPIP server is licensed under the Common Development and Distribution
License 1.0. See [LICENSE](LICENSE).
