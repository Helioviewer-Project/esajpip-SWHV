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
the same `cid` on a new connection. Requests must use HTTP/1.1 `GET`.

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

The client should send requests serially and consume each complete response
before sending the next one. The server does not run concurrent requests or
cancel an active response. It permits at most one replacement connection to
wait for a channel.

If a replacement arrives while the old connection is stalled in an incomplete
request head, the server abandons that partial request, closes the old
connection, and reads the request from the replacement. If a response is
already being sent, it is allowed to finish before the replacement is used.
An additional replacement receives `503 Service Unavailable`.

Closing a persistent connection between complete responses does not close the
channel immediately. The server waits for another connection carrying its
`cid`, up to `connections.timeout`.

If a request contains `Connection: close`, the server completes that response
and closes the connection. The JPIP channel remains available for a later
connection carrying its `cid`.

## Close a channel

Send `cclose` with the channel identifier:

```http
GET /jpip?cclose=0123456789abcdef0123456789abcdef HTTP/1.1
Host: server.example:8900

```

The server replies with `200 OK` and an empty body, then closes the connection
and releases the channel state. `cclose=*`, channel lists, and session-wide
closure are not supported.

## HTTP responses

These are all HTTP status codes emitted by the server:

| Status | When it is returned | Effect on the channel |
| --- | --- | --- |
| `200 OK` | A channel is created, a channel request is served, or `cclose` succeeds. Image responses use `Transfer-Encoding: chunked` and `Content-Type: image/jpp-stream`; `cclose` has `Content-Length: 0`. | The channel remains available after an image response. A successful `cclose` ends it. |
| `400 Bad Request` | A request reaches a channel but its HTTP request line, body framing, supported JPIP fields, cache model, codestream selection, or window is invalid. The response body identifies the invalid field or constraint. | The connection and channel are closed. Create a new channel after correcting the request. |
| `404 Not Found` | A `cnew` request names a target that does not exist below the configured image directory. | No usable channel is created; the connection is closed. |
| `431 Request Header Fields Too Large` | An identified connection sends more than 4 KiB for one complete HTTP request head. | The connection and channel are closed. Create a new channel with a smaller request. |
| `501 Not Implemented` | A valid `cnew` request offers no supported transport. The response has no `JPIP-cnew` header. | No usable channel is created; the connection is closed. Retry with `http` in the transport list. |
| `500 Internal Server Error` | The selected target path or file is invalid, unsupported, unreadable, or fails validation, or the server cannot generate a channel ID. The response body identifies the failure category. | The connection and channel are closed. A file failure normally requires correcting the path or source file. |
| `503 Service Unavailable` | A request names an unknown, ended, different, or otherwise unavailable channel; attempts another `cnew` on an open channel; the active-channel limit has been reached; or the channel already has a replacement connection waiting. The response body distinguishes these cases. | A channel-level failure closes that channel. A dispatcher-level rejection leaves an existing channel unchanged. Create a new channel unless the client knows that the referenced channel remains alive. |

Channel-level error responses contain a short plain-text body and
`Connection: close`. Successful responses and channel-level errors also include
CORS, HSTS, and no-cache headers. The dispatcher-level `503` includes the CORS
and no-cache headers but not HSTS. `cnew` exposes `JPIP-cnew` and `JPIP-tid` to
browser clients.

Some failures close the socket without an HTTP response:

- The initial bytes are not a recognizable HTTP/1.1 JPIP `GET` request.
- The initial request does not arrive before `connections.initial_timeout`.
- The physical-connection limit has been reached.
- A request head is incomplete or contains a malformed HTTP header.
- The socket fails while a request or response is in progress.
- The serving process exits or restarts.

Requests must not contain a body. A nonzero `Content-Length`, an invalid
`Content-Length`, or any `Transfer-Encoding` receives `400 Bad Request`, and
the connection is closed after the response.

A response that ends before its HTTP chunk terminator or JPIP end-of-response
message is incomplete. The client must discard that response. Since all channel
state is lost on a serving-process restart, the safe recovery is to create a
new channel and rebuild the client cache from its responses.

## Request and connection limits

Initial inspection examines at most 2 KiB and accepts only request lines
containing `cnew`, `cid`, or a usable `cclose`. The deadline is absolute:
sending a few bytes does not extend `connections.initial_timeout`. Rejected
connections do not allocate a channel or JPEG 2000 state.

After identification, the complete HTTP request head is limited to 4 KiB.
`connections.timeout` limits each wait for request data, response writes, and
the wait for a replacement connection. Successful I/O starts the next wait; the
setting is not an absolute channel lifetime. Values of `0` and `-1` disable this
timeout.

`connections.limit` applies independently to physical connections and active
channels. A channel can have one active connection and at most one queued
replacement. Clients should close connections they no longer use.

## Channel lifetime

A channel ends after a successful `cclose`, a malformed request, an invalid
image request, a response failure, or an inactivity timeout. It also disappears
when the serving process restarts. Channels are not restored after a restart.

The server deliberately ends a channel after an incomplete response. Its cache
model may already include bytes that did not reach the client, so reusing that
state could make a later response incomplete from the client's point of view.

## Ownership

The supervisor keeps the listening socket open but does not accept clients. The
serving process accepts each connection and keeps a small `PendingConnection`
record until it can identify the request. That record contains the connection's
stable identifier, descriptor, and absolute deadline. Once identified, the
descriptor goes directly to the selected `ConnectionQueue` and the pending
record is removed. The serving loop retains the total connection count but no
record for the active socket.

Each channel thread owns its `FileManager`, `ImageIndex`, linked-JPX graph,
`DataBinServer`, cache model, traversal state, and response buffer. No JPEG 2000
state is shared between channels. The serving loop and channel thread share only
one mutable per-channel object, the connection queue. Configuration is shared
read-only, and channel threads report connection and channel completion through
an unnamed datagram socket pair.

## Admission and dispatch

```text
TCP accept
    |
    v
serving process: pending connection + absolute identification deadline
    |
    +-- no complete request line before deadline -----> close
    +-- unsupported or unrelated traffic -------------> close
    |
    v
bounded cnew/cid/cclose recognition
    |
    v
direct transfer to the selected channel queue
```

Initial inspection peeks at no more than 2 KiB and does not consume the request.
The serving loop retains a pending connection only until it recognizes `cnew`,
`cid`, or `cclose`. Rejected and expired connections never create a channel
thread or allocate JPEG 2000 state.

For `cnew`, the serving loop creates a channel ID and a capacity-one connection
queue, places the first connection in that queue, and starts a detached channel
thread. For `cid` or `cclose`, it finds the existing channel and places the new
connection in the same queue. The pending record is removed as soon as ownership
passes to the channel.

The channel thread processes one request at a time. Buffered requests on its
current persistent connection take precedence. When it needs more bytes, it
waits on both the TCP connection and the connection queue. A queued replacement
therefore interrupts an incomplete request, closes the old descriptor, and
wakes the thread on the new one.

## Termination and restart

When a channel terminates, its thread closes the active descriptor and any
queued descriptor, reports each physical connection as complete, closes its
connection queue, and reports that the channel has ended. The serving loop keeps
socket table entries only while identifying new connections. Completion
notifications update its total connection count without retaining records for
active channel sockets.

If the serving process restarts, accepted sockets and all channel state are
lost. The supervisor retains the listening socket, so new connections can wait
in the listen backlog during the one-second restart delay. Clients must create
new channels after the restart. If the serving process was killed, the
replacement records that fact in its log; other unexpected exits are recorded
separately.

The serving process ignores `SIGINT` and `SIGTERM` and polls one end of a private
Unix socket pair. The supervisor handles those signals and closes the other
endpoint. This wakes the serving loop, which records the shutdown, drains the
log, and exits the process without waiting for channel threads. Each replacement
serving process receives a fresh socket pair.

## Deliberate limits

- `connections.limit` independently limits physical connections and active
  channels in the serving process.
- A channel has one active request/response and at most one queued replacement
  connection.
- A connection cannot carry simultaneous work for multiple channels, although
  the general JPIP model permits it.
- Channel state is never reconstructed or shared between channel threads.

These limits keep ownership clear and work bounded while preserving the current
JHelioviewer wire behavior. They still allow an HTTP library or browser to
replace its underlying connection without losing the JPIP channel.

## Source layout

The source responsibilities are:

| Source | Responsibility |
| --- | --- |
| `main.cc` | Configuration and listening-socket setup |
| `server/supervisor.cc` | Serving-process lifetime and restart |
| `server/server.cc` | Connection admission, channel routing, and log output |
| `server/initial_request.cc` | Bounded inspection for `cnew`, `cid`, or `cclose` traffic |
| `server/connection_queue.cc` | Capacity-one queue shared with one channel thread |
| `http/connection.cc` | Socket setup, bounded HTTP request-head I/O, and chunk framing |
| `server/channel.cc` | JPIP request handling and all state retained for one channel |
