# Connections and JPIP channels

This document describes the lifecycle of esajpip HTTP connections and JPIP
channels. It covers the implemented server model rather than every transport
and session form allowed by JPEG 2000 Part 9.

## Terms and source layout

A **connection** is one accepted TCP socket. A **channel** is the stateful JPIP
request sequence identified by the `cid` returned in `JPIP-cnew`. In esajpip's
supported profile, one channel constitutes the session and may use more than one
connection during its lifetime.

The responsibilities are separated as follows:

| Source | Responsibility |
| --- | --- |
| `esa_jpip_server.cc` | Configuration and listening-socket setup |
| `server/supervisor.cc` | Serving-process lifetime and restart |
| `server/server.cc` | Connection admission, channel routing, and log output |
| `server/initial_request.cc` | Bounded inspection for `cnew`, `cid`, or `cclose` traffic |
| `server/connection_queue.cc` | Capacity-one queue shared with one channel thread |
| `server/channel.cc` | HTTP request handling and all state retained for one JPIP channel |

## Ownership

The supervisor owns the listening socket but never accepts from it. The serving
process accepts each connection and retains a small `PendingConnection` record
containing its stable identifier, descriptor, and absolute deadline until the
request is identified. The descriptor then passes directly to a
`ConnectionQueue` in the same process, and the pending record is removed. The
serving loop retains only the total physical-connection count.

One channel thread exclusively owns its `FileManager`, `ImageIndex`, linked JPX
graph, `DataBinServer`, cache model, traversal state, and response buffer.
None of this JPEG 2000 state is shared with another channel thread. The serving
loop and channel thread share only the queue: at most one descriptor, two
wake-up sockets, and their small synchronization state. Channel threads report
connection and channel completion to the serving loop through an unnamed
datagram socket pair within the serving process.

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
request. It requires HTTP/1.0 or HTTP/1.1 `GET` traffic containing `cnew`, `cid`,
or `cclose`. The fixed `connections.initial_timeout` deadline is not extended
by a client sending request bytes slowly. Rejected and expired connections
never create a channel thread or allocate JPEG 2000 state.

## Channel creation and reuse

For `cnew`, the serving loop creates a channel ID and a capacity-one connection queue,
queues the first connection, and then starts one detached channel thread. The
response selects `transport=http`, returns the channel ID, and serves the first
JPIP request on the same connection.

For `cid` or `cclose`, the serving loop looks up the existing channel and places the
new connection in its queue. An unknown channel is closed immediately. If
a connection is already waiting, a second concurrent replacement is also
closed. The supported profile therefore allows one active response and at most
one waiting connection per channel.

The channel thread processes requests serially. Buffered requests on its
current persistent connection take precedence. Otherwise, a queued replacement
connection wakes the thread. The thread closes its current descriptor and
adopts the replacement. This supports both JHelioviewer's
persistent socket and clients whose HTTP implementation opens a new socket for
a later request.

[JPEG 2000 Part 9](https://www.itu.int/rec/T-REC-T.808/en) explicitly separates
an HTTP connection from a JPIP session. Persistent HTTP is useful but neither
establishes nor identifies the session, and a session request may arrive on a
different HTTP connection. Keeping state with the channel therefore follows
the protocol model. esajpip deliberately implements a smaller profile: HTTP
`GET`, one target, one channel thread, and serialized
requests and responses.

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

A valid `cclose` response ends the channel. A malformed request, socket setup
failure, incomplete response, or timeout also ends it. A response failure cannot
safely preserve the channel because its cache model may already record bytes
that the client did not receive completely.

On termination, the channel thread closes its current descriptor and any queued
descriptor, reports each physical connection as complete, closes the queue, and
reports that the channel has ended. The serving loop keeps table entries only
for sockets awaiting identification; connection-completion notifications update
the total open connection count without retaining an active-socket record.

If the serving process restarts, accepted sockets and all per-channel state are
lost. The supervisor retains the listening socket, so new connections remain in
the listen backlog while it creates the replacement process. Clients establish
new channels after the restart. If the serving process is killed, the
replacement records that fact in its log. Other unexpected exits are recorded
separately.

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

These limits preserve straightforward ownership, bounded work, and the current
JHelioviewer wire behavior while allowing browser-managed connection reuse and
replacement.
