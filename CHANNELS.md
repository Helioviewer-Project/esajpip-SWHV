# Connections and JPIP channels

This document explains how esajpip accepts HTTP connections, attaches them to
JPIP channels, and releases their resources. It describes the server as it
exists today, not every transport and session model allowed by JPEG 2000 Part 9.

## Terms and source layout

A **connection** is one accepted TCP socket. A **channel** is the JPIP state
identified by the `cid` returned in `JPIP-cnew`. For the profile supported here,
the channel is the session. It may move between HTTP connections during its
lifetime.

The responsibilities are separated as follows:

| Source | Responsibility |
| --- | --- |
| `main.cc` | Configuration and listening-socket setup |
| `server/supervisor.cc` | Serving-process lifetime and restart |
| `server/server.cc` | Connection admission, channel routing, and log output |
| `server/initial_request.cc` | Bounded inspection for `cnew`, `cid`, or `cclose` traffic |
| `server/connection_queue.cc` | Capacity-one queue shared with one channel thread |
| `server/channel.cc` | HTTP request handling and all state retained for one JPIP channel |

## Ownership

The supervisor keeps the listening socket open but does not accept clients. The
serving process accepts each connection and keeps a small `PendingConnection`
record until it can identify the request. That record contains the connection's
stable identifier, descriptor, and absolute deadline. Once identified, the
descriptor goes directly to the selected `ConnectionQueue` and the pending
record is removed. The serving loop keeps only the total connection count.

Each channel thread owns its `FileManager`, `ImageIndex`, linked JPX graph,
`DataBinServer`, cache model, traversal state, and response buffer. No JPEG 2000
state is shared between channels. The serving loop and channel thread share only
the connection queue and its small synchronization state. Channel threads report
connection and channel completion through an unnamed datagram socket pair.

## Initial request

```text
TCP accept
    |
    v
serving process: pending Connection + absolute identification deadline
    |
    +-- no complete request before deadline ----------> close
    +-- unsupported or unrelated traffic -------------> close
    |
    v
bounded cnew/cid/cclose recognition
    |
    v
direct transfer to the selected channel queue
```

Initial request inspection peeks at no more than 2 KiB and does not consume the
request. It requires HTTP/1.1 `GET` traffic containing `cnew`, `cid`,
or `cclose`. The fixed `connections.initial_timeout` deadline is not extended
by a client sending request bytes slowly. Rejected and expired connections
never create a channel thread or allocate JPEG 2000 state.

## Channel creation and reuse

For `cnew`, the serving loop creates a channel ID and a one-entry connection
queue, queues the first connection, and starts a detached channel thread. The
response selects `transport=http`, returns the channel ID, and serves the first
JPIP request on that connection.

For `cid` or `cclose`, the serving loop finds the existing channel and places the
new connection in its queue. The server returns `503 Service Unavailable` with
`JPIP channel unavailable` when the channel is unknown, has ended, or already
has a replacement connection waiting, then closes the new connection. A
channel has one active response and at most one waiting connection.

The channel thread handles one request at a time. Buffered requests on its
current persistent connection come first. Otherwise, a queued replacement wakes
the thread, which closes the old descriptor and adopts the new one. This works
for JHelioviewer's persistent socket and for clients whose HTTP library opens a
new connection for a later request.

[JPEG 2000 Part 9](https://www.itu.int/rec/T-REC-T.808/en) explicitly separates
an HTTP connection from a JPIP session. Persistent HTTP is useful, but it
neither establishes nor identifies the session. A later session request may
arrive on another HTTP connection. Keeping state with the channel follows that
protocol model. esajpip implements the smaller profile it needs: HTTP `GET`,
one target, one channel thread, and one request and response at a time.

## Timeouts and termination

`connections.initial_timeout` applies only before the initial request is
identified. `connections.timeout` is then the inactivity limit for the channel:

- With a connection attached, the channel waits on both the TCP socket and its
  connection queue for at most `connections.timeout` seconds.
- Without a connection, it waits on the queue for the same interval.
- Socket receive and send operations use the same configured timeout.
- Values of `0` and `-1` disable the communication timeout.

Receiving request data or transferring response data is activity. The timeout
is not an absolute maximum channel lifetime.

A valid `cclose` ends the channel. A malformed request, socket setup failure,
incomplete response, or timeout ends it as well. After a response failure, the
cache model may already include bytes that never reached the client, so the
channel cannot be reused safely.

On termination, the channel thread closes its current descriptor and any queued
descriptor, reports each physical connection as complete, closes the queue, and
reports that the channel has ended. The serving loop keeps table entries only
for sockets awaiting identification. Connection-completion notifications update
the total open connection count without retaining an active-socket record.

If the serving process restarts, accepted sockets and all per-channel state are
lost. The supervisor retains the listening socket, so new connections remain in
the listen backlog during the one-second restart delay. Clients establish new
channels after the restart. If the serving process is killed, the replacement
records that fact in its log. Other unexpected exits are recorded separately.

The serving process ignores `SIGINT` and `SIGTERM` and polls one end of a
private Unix socket pair. The supervisor handles those signals, closes the
other endpoint, and gives the serving process one orderly shutdown path on
every supported platform. Each replacement process receives a fresh pair.

## Deliberate limits

- `connections.limit` independently limits physical connections and active
  channels in the serving process.
- A channel has one active request/response and at most one queued replacement
  connection.
- A connection cannot carry simultaneous work for multiple channels in this
  implementation, although the general JPIP model permits broader use.
- Channel state is never reconstructed or shared between channel threads.

These limits keep ownership clear and work bounded while preserving the current
JHelioviewer wire behavior. They also allow an HTTP library or browser to replace
the underlying connection without losing the JPIP channel.
