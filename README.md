# ESA JPIP server

`esajpip` is an open-source JPIP server for Unix-like systems. It was developed
for the Helioviewer project and streams solar imagery stored in JP2 and JPX
files to JHelioviewer.

The server uses HTTP and emits JPP streams. It does not provide TLS or
authentication. Deploy it behind an appropriate network boundary or reverse
proxy when those facilities are required.

## Build and install

The build requires a C++11 compiler, CMake, pkg-config, GLib, zlib, and POSIX
threads. On Debian 12, install the required packages with:

```sh
sudo apt-get install build-essential cmake pkg-config libglib2.0-dev zlib1g-dev
```

Use a separate build directory. The image and log directories are written into
the generated `server.ini`:

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

This installs the executable and `server.ini` in `$HOME/esajpip/bin` for the
example above. Choose another installation prefix if it better suits the host.

## Configure the server

The executable reads `server.ini` from its current working directory. The file
uses INI syntax; blank lines and lines beginning with `#` are ignored. All four
sections must be present. Changes take effect only after restarting the server.

| Setting | Generated value | Meaning |
| --- | --- | --- |
| `listen.port` | `8900` | TCP port on which the server listens. The value must be from 1 through 65535. |
| `listen.address` | empty | Local IPv4 address or hostname on which to listen. An empty value listens on all IPv4 interfaces. |
| `jpip.image_directory` | `SWHV_DIR_IMAGE` | Non-empty base directory from which requested JP2 and JPX paths are opened. |
| `jpip.chunk_size` | `64000` | Response working-buffer size and maximum normal HTTP chunk payload, in bytes. It must be at least 128; the final chunk may be smaller. |
| `connections.initial_timeout` | `3` | Positive number of seconds allowed for a new socket to provide a recognizable JPIP request. Partial input does not extend the deadline. |
| `connections.timeout` | `60` | Channel inactivity and socket I/O timeout in seconds. `0` and `-1` disable it; values below `-1` are invalid. |
| `connections.limit` | `500` | Positive limit applied independently to physical connections and active channels. |
| `logging.directory` | `SWHV_DIR_LOG` | Directory for log files when file logging is enabled. It must exist and be writable. |
| `logging.file_enabled` | `1` | Set to `1` to log to a file or `0` to log to standard output. |
| `logging.requests` | `0` | Set to `1` to include request lines in the log. Other server messages are unaffected by this setting. |

Directory paths may contain spaces. The server adds a trailing slash when
needed. `logging.directory` may be empty only when file logging is disabled.

## Prepare image data

Requested file names must end in lowercase `.jp2` or `.jpx`. Every JPEG 2000
codestream must contain `PLT` packet-length markers. Linked JPX movies produced by
`hv_jpx_merge` and the compatible `kdu_merge` form are supported. The server
preserves codestream order and does not sort movie frames, so construct the JPX
with its sources in the intended timestamp order.

Treat every JP2, JPX, and linked source file as immutable while it may be used
by an active channel. The server indexes a target once and maps its source files
again for later responses. Truncating a mapped file can terminate the serving
process, while replacing a path can make an existing index refer to different
content. Publish completed sources under new names and expose the referring JPX
only after all of its sources are ready.

Client-supplied URI paths and `target` values are rejected if any path segment
is `..`. They are not percent-decoded. Linked `file://` references are trusted
server-side data and may point outside the image directory. The server also
follows filesystem symbolic links. Run it under an account whose read
permissions are limited to the intended image data.

See [JPIP support profile](JPIP_PROFILE.md) for the complete accepted JP2 and
JPX structure, request fields, response behavior, and deliberate limitations.

## Start and stop the server

Run the installed executable from the directory containing its configuration:

```sh
cd "$HOME/esajpip/bin"
./esajpip
```

The process started by the operator is the supervisor. It owns the listening
socket and starts the serving process. The program does not detach, so a host
process supervisor may manage it directly. The executable accepts no
command-line options.

Stop the server by sending `SIGINT` or `SIGTERM` to the supervisor process, or
through the host process supervisor. Do not signal only the serving child: an
unexpected child exit causes the supervisor to start a replacement. A restart
is recorded in the log, but active JPIP channels are not recovered.

Once running, a JHelioviewer target has this form:

```text
jpip://server.example:8900/movie.jpx
```

The path is resolved relative to `jpip.image_directory`.

## Timeouts and capacity

Until a new connection supplies a recognizable JPIP request, the serving
process retains only its socket. `connections.initial_timeout` closes silent or
unrelated connections without creating a channel thread or allocating JPEG
2000 state.

After channel creation, `connections.timeout` applies both to an attached HTTP
connection and while the channel waits for a replacement connection. Each
active channel owns its JPEG 2000 index, cache model, response buffer, and any
source-file mappings needed by the current response. Set `connections.limit`
according to the largest movies and the memory available on the deployment
host, not only the expected socket count.

See [Connections and JPIP channels](CHANNELS.md) for the detailed ownership,
routing, timeout, and cleanup model.

## Logging

With file logging enabled, the server creates
`esajpip.<address>.<port>.<timestamp>.log` under `logging.directory`. The active
file rolls at 1 GiB, and one `.1` backup is retained. Different listening
addresses or ports therefore use distinct names.

Channel threads submit log records through a bounded nonblocking queue. When
logging cannot keep up, records are dropped instead of delaying an active
response; the next successfully queued record reports how many were lost. If
the active log can no longer be written or rotated, file logging is disabled
without switching to standard output. Startup fails if the configured log file
cannot be opened.

## Troubleshooting

Configuration and startup failures are written to standard error. A failure to
initialize the listen socket usually means that the configured address is not
available or the port is already in use. Image parsing and response failures are
written to the server log and close the affected channel. Enable
`logging.requests` temporarily when the request associated with a failure is
needed for diagnosis.

## License

The ESA JPIP server is licensed under the Common Development and Distribution
License 1.0. See [LICENSE](LICENSE).
