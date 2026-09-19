# ESA JPIP server

`esajpip` is an open-source JPIP server for Unix-like systems, designed
primarily to stream solar imagery from JP2 and JPX files to JHelioviewer. It
serves JPP streams over HTTP. Because it does not provide TLS or authentication,
use a suitable network boundary or reverse proxy when either is required.

## History

The original ESA JPIP server was developed for the ESA/NASA Helioviewer
project by Juan Pablo García Ortiz and collaborators at the University of
Almería. It was published on [Launchpad](https://code.launchpad.net/esajpip).
This repository contains subsequent maintenance and substantial
rearchitecting, documented in its Git history.

## Build and install

The build requires a C++11 compiler, CMake, pkg-config, GLib, libuv, llhttp,
zlib, and POSIX threads. Debian 13 is the minimum supported Debian release.
On Debian 13, install the required packages with:

```sh
sudo apt-get install \
    build-essential cmake pkg-config libglib2.0-dev libllhttp-dev \
    libuv1-dev zlib1g-dev
```

Build outside the source tree. This example installs the server under
`$HOME/esajpip` and writes the chosen image and log directories to the generated
`server.ini`:

```sh
mkdir -p "$HOME/esajpip/images" "$HOME/esajpip/log"
cmake -S . -B build \
    -DCMAKE_INSTALL_PREFIX="$HOME/esajpip" \
    -DESAJPIP_IMAGE_DIRECTORY="$HOME/esajpip/images" \
    -DESAJPIP_LOG_DIRECTORY="$HOME/esajpip/log"
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
```

For a separate test build, run `./tests/run.sh`. Use
`./tests/run.sh sanitize` for ASan and UBSan. The
[test guide](tests/README.md) describes the coverage, focused runs, and failure
diagnosis.

The executable and `server.ini` are installed in `$HOME/esajpip/bin`. Use any
other installation prefix that suits the host.

## Configure the server

The executable reads `server.ini` from its current working directory. The file
uses ordinary INI syntax. Blank lines and lines beginning with `#` are ignored.
All five sections and every setting below are required. Restart the server
after changing the file.

| Setting | Generated value | Meaning |
| --- | --- | --- |
| `listen.port` | `8900` | TCP port on which the server listens. The value must be from 1 through 65535. |
| `listen.address` | empty | Local IPv4 address or hostname on which to listen. An empty value listens on all IPv4 interfaces. |
| `jpip.image_directory` | `images` | Non-empty base directory from which requested JP2 and JPX paths are opened. Relative paths are resolved from the server's working directory. |
| `jpip.chunk_size` | `64000` | Response working-buffer size and maximum normal HTTP chunk payload, in bytes. It must be between 128 and 262144. The final chunk may be smaller. |
| `connections.initial_timeout` | `3` | Positive number of seconds allowed for a new socket to provide a recognizable JPIP request. Partial input does not extend the deadline. |
| `connections.timeout` | `60` | Channel inactivity and socket I/O timeout in seconds. It must be positive. |
| `connections.limit` | `128` | Maximum number of physical HTTP connections. |
| `channels.limit` | `256` | Maximum number of active JPIP channels. This may exceed the physical-connection limit because pooled connections can serve several channels. |
| `logging.directory` | empty | Directory for log files when file logging is enabled. It must exist and be writable. CMake fills this and enables file logging when `ESAJPIP_LOG_DIRECTORY` is set. |
| `logging.file_enabled` | `false` | Set to `true` to log to a file or `false` to log to standard output. |
| `logging.requests` | `false` | Set to `true` to include request lines in the log. Request lines contain bearer channel IDs, so protect the logs as client credentials. This setting does not affect other server messages. |

Directory paths may contain spaces. The server adds a trailing slash when
needed. `logging.directory` may be empty only when file logging is disabled.

## Prepare image data

File names must end in lowercase `.jp2` or `.jpx`. Every codestream must contain
`PLT` packet-length markers. The server accepts linked JPX movies produced by
`hv_jpx_merge` and the compatible `kdu_merge` form. It preserves the order
stored in the JPX, so pass source frames to the merge tool in timestamp order.

Do not modify a JP2, JPX, or linked source while an active channel may use it.
The server indexes a target once and maps its source files as responses need
them. Truncating a mapped file can terminate the server process. Replacing the
contents at an existing path can leave an active index pointing at the wrong
bytes. Publish completed sources under new names, then expose the referring JPX
only after every source is ready.

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

`esajpip` runs as a single foreground process and has no command-line options.
Stop it with `SIGINT` or `SIGTERM`, either directly or through the host process
manager. The server stops accepting connections, closes active connections and
channels, drains the log, and exits. Use the process manager or container
runtime when automatic restart is required. Active JPIP channels cannot be
recovered after a restart.

Once running, a JHelioviewer target has this form:

```text
jpip://server.example:8900/movie.jpx
```

The path is resolved relative to `jpip.image_directory`.

## Reverse proxies

A conventional HTTP reverse proxy may terminate TLS and pool or replace its
upstream connections. JPIP channels are identified by `cid`, not by their TCP
connections, so affinity to a particular upstream connection is neither
required nor desirable.

Configure the proxy as follows:

- Use HTTP/1.1 to esajpip and preserve the request path and query string.
- Pass the JPIP response headers and stream chunked bodies without caching,
  response buffering, recompression, or any other content transformation.
- Disable automatic retries and failover for JPIP requests. Although they use
  `GET`, successful requests advance the channel's cache state.
- Route every request for one `cid`, including `cclose`, to the same esajpip
  process. Independent backend processes do not share channel state.
- Allow enough time for image opening and streaming, and propagate a
  downstream disconnect to the origin promptly.

The server handles one active and one waiting request per channel, in arrival
order. Requests sent sequentially over one HTTP/1.1 connection retain that
order. Concurrent requests over different connections or HTTP/2 streams have
no useful common ordering. A client must therefore avoid issuing dependent
requests for one channel concurrently. The proxy cannot repair their order.

TLS, HSTS, authentication, public rate limiting, and public access logging
belong at the proxy. A `cid` is a bearer credential carried in the URL. Protect
or redact complete query strings in public logs. A single backend process is
the simplest deployment. Multiple backends require explicit channel-aware
affinity.

## Timeouts and capacity

A new connection remains cheap until it sends a recognizable JPIP request.
Before then, the server keeps only the socket. `connections.initial_timeout`
closes silent or unrelated connections without allocating JPEG 2000 state.

After channel creation, `connections.timeout` applies to request input,
response progress, channel waits, and idle channels. Each channel owns its JPEG
2000 index, cache model, and traversal state. It lazily allocates up to four
`jpip.chunk_size` output buffers and retains them until the channel closes.
These buffers form a bounded pipeline. JPEG 2000 generation can continue while
earlier chunks wait for non-blocking socket writes, then pauses when all four
buffers are occupied. Gzip responses use one additional buffer of the same
size, retained after its first use. At the shipped settings, the buffers use up
to 256,000 bytes per plain channel and 320,000 bytes per channel that has used
gzip. At the maximum chunk size, those bounds become 1 MiB and 1.25 MiB.

Window traversal retains one 4-byte cumulative offset for each selected
precinct bin until the window changes or the channel closes. Source-file
mappings are response-scoped: completing a response unmaps every codestream it
touched. This bounds virtual address space and the number of VMAs, at the cost
of reopening a source when a later response needs it. File descriptors close
immediately after mapping.

Set `channels.limit` according to the largest movies and the memory available
on the deployment host. Set `connections.limit` according to expected browser
or proxy connection use.

JPEG 2000 work runs in libuv's worker pool. esajpip sets
`UV_THREADPOOL_SIZE=16` when the variable is absent. An explicit value from 2
through 1024 is honored. A malformed value or one outside that range prevents
startup. At most half of the workers may open new images concurrently, leaving
the others available to established channels. This is an operational tuning
variable, not an INI setting.

See [Connections and JPIP channels](CHANNELS.md) for request examples, HTTP
responses, connection replacement, timeouts, recovery, and server ownership.

## Logging

With file logging enabled, the server creates
`esajpip.<address>.<port>.<timestamp>.log` under `logging.directory`. The active
file rolls at 1 GiB, and one `.1` backup is retained. Different listening
addresses or ports use distinct names.

The event loop and worker threads send records through a bounded, nonblocking
queue, so they never wait for log-file I/O. If logging falls behind, records
are dropped. The next accepted record reports how many were lost. If the active
log can no longer be written or rotated, file logging stops instead of silently
switching to standard output. The server does not start when it cannot open the
configured log file.

## Troubleshooting

Configuration and startup failures are written to standard error. If the
listen socket cannot be initialized, the configured address may be unavailable
or the port may already be in use. Image parsing and response failures are
written to the server log and close the affected channel. Temporarily enable
`logging.requests` when diagnosis requires the request associated with a
failure.

For reproducible integration, performance, sanitizer, and Debian procedures,
see [Testing esajpip](TESTING.md). The [development notes](DEVELOPMENT.md) record
the measurements and compatibility decisions behind the current design.
User-visible release changes are summarized in the [changelog](CHANGELOG.md).

## License

The ESA JPIP server is licensed under the Common Development and Distribution
License 1.0. See [LICENSE](LICENSE).
