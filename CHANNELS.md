# Connections and JPIP channels

This document explains how a client creates, uses, replaces, and closes an
esajpip channel over HTTP. It also describes how the server admits connections,
owns channel state, and releases resources. For the complete list of supported
JPIP fields and JPEG 2000 source restrictions, see
[JPIP_PROFILE.md](JPIP_PROFILE.md).

## Connection model

A TCP connection carries HTTP requests and responses. A JPIP channel holds the
state for one target: its image index, cache model, response progress, and the
`cid` returned by the server. The channel can outlive a particular TCP
connection.

esajpip supports one target and one request/response at a time per channel. A
client may keep using the original HTTP connection or send a later request for
the same `cid` on a new connection. One connection may carry requests for
different channels, which allows normal browser and reverse-proxy connection
pooling. Requests must use HTTP/1.1 `GET`.

[JPEG 2000 Part 9](https://www.itu.int/rec/T-REC-T.808/en) separates HTTP
connections from JPIP sessions. A persistent HTTP connection neither establishes
nor identifies the session, and a later request may arrive on another
connection. esajpip implements the smaller model it needs: the channel is the
session, and it owns the state for one target and one serial request stream.

The examples below omit optional JPIP fields for clarity. Target paths are
relative to the configured image directory. They are not generally URI-decoded,
so clients should send the literal file name known to the server.

## Create a channel

The first request names a target and includes `cnew`. The target may be the URI
path:

```http
GET /movie.jpx?cnew=http&type=jpp-stream&stream=0&len=2097152 HTTP/1.1
Host: server.example:8900

```

The `cnew` value is a comma-separated list of acceptable transports. The server
selects `http`, so the list must contain `http`; JHelioviewer sends
`cnew=http`.

It may instead be supplied through `target`:

```http
GET /jpip?target=movie.jpx&cnew=http&type=jpp-stream&stream=0&len=2097152 HTTP/1.1
Host: server.example:8900

```

A successful response creates the channel and returns its opaque identifier:

```http
HTTP/1.1 200 OK
JPIP-cnew: cid=0123456789abcdef0123456789abcdef,path=jpip,transport=http
JPIP-tid: 0
Transfer-Encoding: chunked
Content-Type: image/jpp-stream

```

`JPIP-tid: 0` means that the server does not assign a stable target identifier.
The client must not use it to recover cache state in another channel.

Image responses contain a chunked JPP stream. Each completed response ends with
a JPIP end-of-response message, followed by the terminating HTTP chunk. If the
request contains `metareq` and `Accept-Encoding` contains `gzip`, the chunked
body is gzip encoded and the response includes `Content-Encoding: gzip`.

## Continue a channel

Every later image request includes the returned `cid`:

```http
GET /jpip?cid=0123456789abcdef0123456789abcdef&stream=7&fsiz=4096,4096,closest&rsiz=1024,1024&roff=0,0&len=2097152 HTTP/1.1
Host: server.example:8900

```

The request may use the existing persistent connection. It may also use a new
connection. This is useful for browser and HTTP-library clients that do not
control socket reuse.

The server does not run concurrent requests or cancel an active response within
one channel. One later request may wait for that channel. An additional request
for the busy channel receives `503 Service Unavailable`. Requests for other
channels remain independent, even when a browser sends them on the same
connection.

Closing a persistent connection between complete responses does not close the
channel immediately. The server waits for another connection carrying its
`cid`, up to `connections.timeout`.

If a request contains `Connection: close`, the server completes that response
with `Connection: close` and closes the connection. The JPIP channel remains
available for a later connection carrying its `cid`.

## Close a channel

Send `cclose` with the channel identifier:

```http
GET /jpip?cclose=0123456789abcdef0123456789abcdef HTTP/1.1
Host: server.example:8900

```

The server replies with `200 OK`, `Content-Length: 0`, and
`Connection: close`, then closes the connection and releases the channel state.
`cclose=*`, channel lists, and session-wide closure are not supported.

## HTTP responses

These are all HTTP status codes emitted by the server:

| Status | When it is returned | Effect on the channel |
| --- | --- | --- |
| `200 OK` | A channel is created, a channel request is served, or `cclose` succeeds. Image responses use `Transfer-Encoding: chunked` and `Content-Type: image/jpp-stream`; `cclose` has `Content-Length: 0`. | The channel remains available after an image response. A successful `cclose` ends it. |
| `400 Bad Request` | A request reaches a channel but its HTTP request line, body framing, supported JPIP fields, cache model, codestream selection, or window is invalid. The response body identifies the invalid field or constraint. | The connection and channel are closed. Create a new channel after correcting the request. |
| `404 Not Found` | A `cnew` request names a target that is missing, has an invalid client-supplied path, uses an unsupported file type, or is not accepted by the supported JPEG 2000 source profile. | No usable channel is created; the connection is closed. Correct the requested target or repository source. |
| `431 Request Header Fields Too Large` | An identified connection sends more than 4 KiB for one complete HTTP request head. | The connection and channel are closed. Create a new channel with a smaller request. |
| `501 Not Implemented` | A valid `cnew` request offers no supported transport. The response has no `JPIP-cnew` header. | No usable channel is created; the connection is closed. Retry with `http` in the transport list. |
| `500 Internal Server Error` | The selected source is unreadable, or the server encounters another internal failure such as being unable to generate a channel ID. The response body identifies the failure category. | The connection and channel are closed. An unreadable source normally requires correcting repository access or storage. |
| `503 Service Unavailable` | A request names an unknown or ended channel, both request slots for a channel are occupied, a channel wait expires, or the active-channel limit has been reached. The response body distinguishes these cases. | A routing rejection leaves an existing channel unchanged. Create a new channel after an ended or unknown-channel response. |

Every response that closes its connection includes `Connection: close`. Error
responses contain a short plain-text body. Responses include CORS and no-cache
headers. TLS and HSTS belong at the reverse proxy. `cnew` exposes `JPIP-cnew`
and `JPIP-tid` to browser clients.

Some failures close the socket without an HTTP response:

- The initial bytes are not a recognizable HTTP/1.1 JPIP `GET` request.
- The initial request does not arrive before `connections.initial_timeout`.
- The physical-connection limit has been reached.
- The socket fails while a request or response is in progress.
- The server process exits.

Requests must not contain a body. A nonzero `Content-Length`, an invalid
`Content-Length`, or any `Transfer-Encoding` receives `400 Bad Request`, and
the connection is closed after the response.

A response that ends before its HTTP chunk terminator or JPIP end-of-response
message is incomplete. The client must discard that response. Since all channel
state is lost when the server process exits, the safe recovery is to create a
new channel and rebuild the client cache from its responses.

## Request and connection limits

The parser limits a request line to 2 KiB and identifies only lines containing
`cnew`, `cid`, or a usable `cclose`. The identification deadline is absolute:
sending a few bytes does not extend `connections.initial_timeout`. Rejected
connections do not allocate a channel or JPEG 2000 state.

After identification, the complete HTTP request head is limited to 4 KiB.
`connections.timeout` limits each wait for request data, response writes, a
busy channel, and an idle channel. Successful I/O starts the next wait; the
setting is not an absolute channel lifetime. Values of `0` and `-1` disable
this timeout.

`connections.limit` limits physical HTTP connections.
`channels.limit` separately limits active JPIP channels. A channel
can have one active request and at most one waiting request. A connection may
be reused for multiple channels, but responses on that connection remain in
request order. Clients should close connections they no longer use.

## Channel lifetime

A channel ends after a successful `cclose`, a malformed request, an invalid
image request, a response failure, or an inactivity timeout. It also disappears
when the server process exits. Channels are not restored after a restart.

The server deliberately ends a channel after an incomplete response. Its cache
model may already include bytes that did not reach the client, so reusing that
state could make a later response incomplete from the client's point of view.

## Ownership

One libuv loop owns the listener, signals, every connection, parser, timer,
channel route, and pending write. Socket state is never accessed by workers.

Each channel owns its `FileManager`, `ImageIndex`, linked-JPX graph,
`DataBinServer`, cache model, and traversal state. The state is processed by at
most one libuv worker at a time and is never shared with another channel. Work
may move between pool threads only at chunk boundaries. Completed response
buffers return to the event loop, which queues the socket writes.

## Admission and dispatch

```text
TCP accept
    |
    v
libuv loop: connection + absolute identification deadline
    |
    +-- no complete request line before deadline -----> close
    +-- unsupported or unrelated traffic -------------> close
    |
    v
bounded HTTP parsing and cnew/cid/cclose routing
    |
    v
selected channel: active slot or one waiting slot
```

llhttp consumes each request head once. A 2 KiB request-line limit and 4 KiB
complete-head limit bound parser storage. Rejected and expired connections do
not allocate JPEG 2000 state.

For `cnew`, the loop reserves an opaque channel ID and places image opening in
a bounded FIFO. At most two opens run concurrently, leaving pool capacity for
established channels. For `cid` or `cclose`, the loop routes the parsed request
to the channel without transferring the socket. Each channel serializes its
worker items and has one active and one waiting request slot.

## Termination

When a channel terminates, the loop cancels queued work where possible, waits
for active work and writes to return, then releases the channel state. A waiting
request receives `503` immediately. An incomplete response closes its connection
without inventing an HTTP terminator or JPIP end-of-response marker.

Libuv handles `SIGINT` and `SIGTERM`. The server stops admission, ends its
channels, closes its connections, drains the log, and exits. A host process
manager or container runtime may restart it, but accepted sockets and channel
state are not preserved. Clients must create new channels after a restart.

## Deliberate limits

- `connections.limit` and `channels.limit` independently limit
  physical connections and JPIP channels.
- A channel has one active request/response and at most one waiting request.
- One connection can serve different channels, but has only one response in
  flight and one retained request.
- Channel JPEG 2000 state is never reconstructed or shared between channels.

These limits keep ownership clear and work bounded while preserving the current
JHelioviewer wire behavior. They still allow an HTTP library or browser to
replace its underlying connection without losing the JPIP channel.

## Source layout

The source responsibilities are:

| Source | Responsibility |
| --- | --- |
| `main.cc` | Configuration and server startup |
| `server/server.cc` | Listener, signals, event loop, admission, routing, channel lifetime, and response writes |
| `server/connection.cc` | Loop-owned libuv connection, deadlines, parsing, and ordered writes |
| `server/request_head.cc` | Bounded llhttp request-head parser |
| `server/channel_work.cc` | Serialized transfer between the loop and worker pool |
| `server/channel_engine.cc` | Socket-free JPIP and JPEG 2000 processing for one channel |
