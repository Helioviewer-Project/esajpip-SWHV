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
| `esa_jpip_server.cc` | Listening parent, serving child, descriptor transfer, channel routing and process recovery |
| `server/connection_admission.cc` | Bounded recognition of a new socket as `cnew`, `cid` or `cclose` traffic |
| `server/channel_inbox.cc` | Capacity-one connection handoff from the child dispatcher to one channel thread |
| `server/channel.cc` | HTTP request handling and all state retained for one JPIP channel |

## Ownership

The listening parent owns its copy of every accepted socket and a small
`Connection` record containing its stable identifier, admission state, and
initial deadline. Passing the descriptor to the serving child with
`SCM_RIGHTS` creates another descriptor for the same socket. The child-side
descriptor is then owned by the dispatcher, a `ChannelInbox`, or the channel
thread. Every transfer leaves exactly one owner.

One channel thread exclusively owns its `FileManager`, `ImageIndex`, linked JPX
graph, `DataBinServer`, cache model, traversal state, and response buffer.
None of this JPEG 2000 state is shared with another channel thread. The child
dispatcher and channel thread share only the inbox: one connection identifier,
one descriptor, two wake-up sockets, and their small synchronization state.

## Connection admission

```text
TCP accept
    |
    v
parent: pending Connection + absolute identification deadline
    |
    +-- no complete request before deadline ----------> close
    +-- unsupported or unrelated traffic --------------> close
    |
    v
bounded cnew/cid/cclose recognition
    |
    v
SCM_RIGHTS descriptor transfer to serving child
```

Admission peeks at no more than 2 KiB and does not consume the request. It
requires HTTP/1.0 or HTTP/1.1 `GET` traffic containing `cnew`, `cid` or
`cclose`. The fixed `identification_time_out` deadline is not extended by a
client sending request bytes slowly. Rejected and expired connections never
create a channel thread or allocate JPEG 2000 state.

After receiving the descriptor, the child repeats the same bounded recognition.
It derives the routing fields from its socket instead of receiving parsed
request state from the parent.

## Channel creation and reuse

For `cnew`, the child creates a channel ID and a capacity-one inbox, queues the
first connection, and then starts one detached channel thread. The response
selects `transport=http`, returns the channel ID, and serves the first JPIP
request on the same connection.

For `cid` or `cclose`, the dispatcher looks up the existing channel and hands
the new connection to its inbox. An unknown channel is closed immediately. If
a connection is already waiting, a second concurrent replacement is also
closed. The supported profile therefore allows one active response and at most
one waiting connection per channel.

The channel thread processes requests serially. Buffered requests on its
current persistent connection take precedence. Otherwise, a queued replacement
connection wakes the thread. The thread closes its current child-side
descriptor and adopts the replacement. This supports both JHelioviewer's
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

`identification_time_out` applies only before admission. After admission,
`time_out` is the inactivity limit for the channel:

- With a connection attached, the channel waits on both the TCP socket and its
  inbox for at most `time_out` seconds.
- Without a connection, it waits on the inbox for the same interval.
- Socket receive and send operations use the same configured timeout.
- Values of `0` and `-1` disable the communication timeout.

Receiving request data or transferring response data is activity. The timeout
is not an absolute maximum channel lifetime.

A valid `cclose` response ends the channel. A malformed request, socket setup
failure, incomplete response, or timeout also ends it. A response failure cannot
safely preserve the channel because its cache model may already record bytes
that the client did not receive completely.

On termination, the channel thread closes its current descriptor and any queued
descriptor, notifies the parent for each physical connection, marks the inbox
closed, and notifies the child dispatcher to remove the route. The parent then
closes its corresponding descriptor copy and removes the `Connection` record.
If the parent observed the socket close first, the later notification is a
harmless no-op keyed by the stable connection identifier.

If the serving child restarts, its per-channel state cannot be recovered. The
new child closes inherited descriptor copies and shuts down admitted connections
so clients can establish new channels. The parent admission deadline continues
to govern pending, unidentified connections.

## Deliberate limits

- `max_number` limits physical connections in the parent and active channels in
  the child. These are separate counts.
- A channel has one active request/response and at most one queued replacement
  connection.
- A connection cannot carry simultaneous work for multiple channels in this
  implementation, although the general JPIP model permits broader use.
- Channel state is never reconstructed or shared between channel threads.

These limits preserve straightforward ownership, bounded work, and the current
JHelioviewer wire behavior while allowing browser-managed connection reuse and
replacement.
