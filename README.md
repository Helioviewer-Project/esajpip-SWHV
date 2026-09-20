# ESA JPIP server

`esajpip` is an open-source JPIP server for Unix-like systems. It serves solar
imagery from JP2 and JPX files to JHelioviewer over HTTP, transmitting only the
part of an image the client currently needs rather than the whole file.

## Concepts

These terms recur here and in the other documents.

- **Codestream** — the compressed data of one image. A `.jp2` file contains
  one; a `.jpx` movie contains one per frame, either embedded or in separate
  `.jp2` files that it links to.
- **Target** — the file a request names, through the request path or the
  `target` field.
- **Window** — the region, resolution, and quality a client currently wants.
  JHelioviewer changes it as the user pans, zooms, and moves through time.
- **JPIP channel** — the server-side state for one client and one target: a
  record of what that client has already received, so each response carries
  only what the current window still lacks. The server identifies a channel by
  an opaque random `cid` that the client repeats on every later request.
- **Data-bin** — the unit a JPP stream addresses: a numbered byte stream
  holding one codestream main header, one tile header, one precinct, or one
  group of metadata boxes.
- **JPP stream** — the response media type, `image/jpp-stream`: a sequence of
  messages, each delivering a byte range of one data-bin. A client accumulates
  data-bins across responses.

## Build and install

The build requires a C++11 compiler, CMake, pkg-config, GLib, libuv, llhttp,
zlib, and POSIX threads. Debian 13 is the minimum supported Debian release;
install the packages there with:

```sh
sudo apt-get install \
    build-essential cmake pkg-config libglib2.0-dev libllhttp-dev \
    libuv1-dev zlib1g-dev
```

Build outside the source tree. This example installs the server under
`$HOME/esajpip` and writes the chosen image and log directories into the
generated `server.ini`:

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

The executable and `server.ini` land in `$HOME/esajpip/bin`; any other prefix
works.

For a separate test build, run `./tests/run.sh`, or `./tests/run.sh sanitize`
to build with AddressSanitizer and UndefinedBehaviorSanitizer. The [test guide](tests/README.md) describes the coverage,
focused runs, and failure diagnosis.

## Configure the server

The server reads `server.ini` from its working directory. The file uses
ordinary INI syntax and ignores blank lines and lines beginning with `#`. Every
section and setting below is required. Restart the server after changing it.

| Setting | Generated value | Accepted values | Meaning |
| --- | --- | --- | --- |
| `listen.port` | `8900` | 1 to 65535 | TCP port on which the server listens. |
| `listen.address` | empty | IPv4 address or hostname | Local address to listen on. Empty listens on every IPv4 interface. |
| `jpip.image_directory` | `images` | non-empty path | Base directory from which requested JP2 and JPX paths are opened. The server resolves a relative path from its working directory. |
| `jpip.chunk_size` | `64000` | 128 to 262144 | Response working-buffer size, and the maximum HTTP chunk payload, in bytes. The final chunk of a response may be smaller. |
| `connections.initial_timeout` | `3` | positive seconds | Time a newly accepted connection has to send a request the server recognizes as JPIP. Sending part of one does not extend the deadline. |
| `connections.timeout` | `60` | positive seconds | How long a channel may sit idle, and how long a stalled read or write may last, before the server gives up on it. |
| `connections.limit` | `128` | positive | Maximum number of HTTP connections open at once. |
| `channels.limit` | `256` | positive | Maximum number of active JPIP channels. It may exceed the connection limit, because pooled connections can serve several channels. |
| `logging.directory` | empty | existing writable path | Directory for log files when file logging is enabled. CMake fills this in and enables file logging when `ESAJPIP_LOG_DIRECTORY` is set. |
| `logging.file_enabled` | `false` | `true` or `false` | Log to a file rather than to standard output. |
| `logging.requests` | `false` | `true` or `false` | Include request lines in the log. It does not affect other server messages. |

Directory paths may contain spaces, and the server adds a trailing slash where
one is needed. `logging.directory` may be empty only when file logging is
disabled.

## Prepare image data

File names must end in lowercase `.jp2` or `.jpx`. Every codestream must carry
the `PLT` markers that record each packet length; the server relies on them to
locate the packets a window needs without decoding the image. The server accepts linked JPX movies produced
by `hv_jpx_merge` and the compatible `kdu_merge` form. It preserves the order
stored in the JPX, so pass source frames to the merge tool in timestamp order.

Never modify a JP2, JPX, or linked source while a channel may be using it. The
server indexes a target once and memory-maps its sources as responses need
them. Truncating a mapped file can terminate the server process, and replacing
the contents at an existing path can leave a live index addressing the wrong
bytes. Publish finished sources under new names, and only then expose the JPX
that refers to them.

See [JPIP support profile](JPIP_PROFILE.md) for the accepted JP2 and JPX
structures, request fields, response behavior, and known limits.

## Start and stop the server

Run the installed executable from the directory holding its configuration:

```sh
cd "$HOME/esajpip/bin"
./esajpip
```

`esajpip` runs as a single foreground process and takes no command-line
options. Stop it with `SIGINT` or `SIGTERM`, directly or through the host
process manager: it stops accepting connections, closes active connections and
channels, drains the log, and exits. For automatic restart, use a process
manager or a container runtime. A restart discards every active JPIP channel.

Once the server is running, a JHelioviewer target has this form:

```text
jpip://server.example:8900/movie.jpx
```

The path is resolved relative to `jpip.image_directory`.

## Reverse proxy and security

esajpip implements neither TLS nor authentication, so any deployment reachable
from an untrusted network requires a reverse proxy. TLS, HSTS, rate limiting,
and public access logging belong there.

The client's HTTPS connection ends at the proxy, which forwards plain HTTP to
esajpip and opens, reuses, and drops its own connections to the server as it
sees fit. That is safe here: a JPIP channel is identified by its `cid`, not by
the connection a request arrives on, so no client need be pinned to one
connection.

Configure the proxy as follows:

- Use HTTP/1.1 to esajpip and preserve the request path and query string.
- Treat JPIP requests as ordered, state-changing operations despite their use
  of `GET`. Never reorder, duplicate, retry, hedge, coalesce, or mirror them.
  Requests for the same `cid` received on one client connection must reach
  esajpip in exactly that order. Forward them over one ordered upstream
  HTTP/1.1 connection or serialize their dispatch; do not fan them out where a
  later request can arrive first.
- Preserve the association and byte order of every response. Pass the JPIP
  response headers through and stream each body to its requesting client
  without interleaving, caching, response buffering, recompression, or content
  transformation.
- Do not fail a JPIP request over to a second server. A successful request
  advances the channel's cache model, and separate server processes do not
  share that state.
- Route every request bearing one `cid`, including `cclose`, to the same
  esajpip process. Separate processes share no channel state.
- Allow enough time for target opening and streaming, and close the
  corresponding connection to esajpip promptly when a client disconnects.

The server handles one active and one waiting request per channel, in arrival
order. An intermediary that changes arrival order changes JPIP semantics and
can make the client and server disagree about the channel cache. Requests
issued sequentially over a single HTTP/1.1 connection have an order to
preserve. Requests spread across several connections or HTTP/2 streams have no
common order for the proxy to recover, so a client must not issue dependent
requests for one channel concurrently.

A single esajpip process is the simplest deployment. Running several requires
the proxy to route by `cid`.

Three properties of the server itself also matter for deployment:

- A `cid` is a bearer credential — possession is sufficient to use the channel.
  It travels in the URL, so redact whole query strings from public logs, and
  protect the server's own logs the same way whenever `logging.requests` is
  enabled.
- Client-supplied paths are constrained but the image directory is trusted. The
  server rejects any client path containing a `..` segment and does not
  percent-decode paths or `target` values, so clients must send the literal
  file name. A `file://` reference stored inside a JPX is trusted, however, and
  may address anything the server's account can read.
- The server follows filesystem symbolic links. Run it under an account whose
  read permission covers only the intended image data.

## Timeouts and capacity

A new connection stays cheap until it sends a request the server recognizes as
JPIP. Until then the server holds only the socket, and
`connections.initial_timeout` closes silent or unrelated connections before any
image state is allocated. Once a channel exists, `connections.timeout` governs
waiting for a request, progress on a response, queuing behind another request,
and idle time.

Each channel owns its index of the target, its cache model of what the client
holds, and its position in the current window. It allocates up to
four `jpip.chunk_size` output buffers as it needs them and keeps them until the
channel closes; a channel that has served a compressed response keeps one more
buffer of the same size. At the shipped settings that is at most 256,000
bytes per channel, or 320,000 once gzip has been used. At the maximum chunk
size those bounds become 1 MiB and 1.25 MiB. Size `channels.limit` from these
figures and the memory available on the host, and `connections.limit` from the
browser and proxy connection use you expect.

Those buffers form a bounded pipeline: the server generates the next chunk
while earlier ones await non-blocking socket writes, and stalls once all four
are occupied, so a slow client throttles generation rather than accumulating
memory. Completing a response unmaps every source it read, which keeps
address-space use predictable at the cost of remapping a source that a later
response needs. Descriptors close as soon as a file is mapped, so the server
holds few of them.

JPEG 2000 work runs on libuv's worker-thread pool, so one slow target cannot
stall the event loop. esajpip sets `UV_THREADPOOL_SIZE` to 16 when the variable
is absent and honors an explicit value from 2 through 1024; a malformed or
out-of-range value prevents startup. At most half the workers may open new
targets concurrently, leaving the rest for channels already serving. This is an
environment variable, not an INI setting.

See [Connections and JPIP channels](CHANNELS.md) for request examples, HTTP
responses, connection replacement, timeouts, recovery, and server ownership.

## Logging

With file logging enabled, the server creates
`esajpip.<address>.<port>.<timestamp>.log` under `logging.directory`. The active
file rolls at 1 GiB and one `.1` backup is kept. Different listening addresses
or ports use distinct names.

No thread in the server waits for log-file I/O: records go onto a bounded queue
that a dedicated thread drains. When that queue is full, records are dropped,
and the next record accepted reports how many were lost. If the active log can no longer be
written or rotated, file logging stops rather than silently falling back to
standard output. The server does not start when it cannot open the configured
log file.

## Troubleshooting

The server writes configuration and startup failures to standard error. If the
listen socket cannot be initialized, the configured address may be unavailable
or the port already in use. It logs image-parsing and response failures and
closes the affected channel. Enable `logging.requests` temporarily when
diagnosis needs the request behind a failure.

The [changelog](CHANGELOG.md) summarizes user-visible release changes.

## History

The original ESA JPIP server was developed for the ESA/NASA Helioviewer project
by Juan Pablo García Ortiz and collaborators at the University of Almería, and
published on [Launchpad](https://code.launchpad.net/esajpip). This repository
holds subsequent maintenance and substantial rearchitecting, documented in its
Git history.

## License

The ESA JPIP server is licensed under the Common Development and Distribution
License 1.0. See [LICENSE](LICENSE).
