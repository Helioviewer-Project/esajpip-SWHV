# Changelog

## 2.0-rc1 - Unreleased

Version 2.0 rearchitects the server. Where 1.x used a parent and a serving
child, passed accepted sockets between them, created a detached thread per
client, and tied a JPIP channel to one TCP connection, 2.0 is a single
foreground process: a libuv event loop owns connections, HTTP parsing, channel
routing, deadlines, signals, and non-blocking writes, and a bounded worker pool
does the JPEG 2000 work. Each channel keeps private image, cache, and traversal
state and is handled by one worker at a time, which isolates JPEG 2000 state
while letting HTTP connections be pooled or replaced independently of channels.

### Changed

- Keep channels alive across replacement connections, and allow one persistent
  connection to carry requests for different channels. This permits ordinary
  browser and reverse-proxy connection pooling. Random, opaque channel IDs
  prevent one client from guessing another client's channel.
- Identify JPIP traffic before allocating JPEG 2000 state. Silent or unrelated
  connections retain only their sockets and expire at the short initial
  timeout. Physical connections and JPIP channels have independent limits.
- Pipeline response generation through bounded chunk buffers, so workers can
  continue while earlier chunks await non-blocking socket writes.
- Reduce per-channel indexing memory. On the 4,014-frame linked-JPX fixture,
  resident memory at the same held point fell from 13,040 KiB to 8,976 KiB
  (31%), with CPU and elapsed time unchanged within measurement noise.
- Stream gzip output one chunk at a time instead of buffering the complete
  compressed response, removing libgsf and its dependencies.
- Replace `server.cfg` and libconfig with `server.ini`, parsed by GLib. Existing
  configurations must be rewritten for 2.0.
- Replace log4cpp with bounded asynchronous logging. Log-file I/O does not run
  on the event loop or worker pool. Logs retain timestamps, optional request
  lines, 1 GiB rotation, and one backup.
- Leave process restart to the host process manager or container runtime. The
  supervisor, `status` command, and shared-memory process registry are removed.
  Log names include the listening address and port.
- Add verified PCRL and CPRL packet traversal and allow one multi-codestream
  request to cover frames with different dimensions, decomposition levels,
  quality layers, or precinct layouts.
- Require HTTP/1.1, matching the chunked responses used by the server and the
  requests sent by JHelioviewer.

### Fixed

- Compute precinct data-bin offsets incrementally so response generation stays
  linear in packet count for sources with many quality layers.
- Keep parsed values local to one request, preventing omitted window, response,
  or rounding fields from inheriting values from an earlier request.
- Make response limits, cache models, and metadata continuation reliable across
  requests. An omitted `len` is unlimited, small limits still produce an EOR,
  and metadata resumes correctly inside JPX placeholders.
- Apply the standard window defaults and resolution mapping, including every
  precinct intersecting the adjusted window. An empty intersection returns
  `EOR WINDOW_DONE` instead of failing the channel.
- Parse standard JPIP codestream lists, closed and open ranges, sampling
  factors, cache-model qualifiers, and absolute request URIs consistently.
- Validate JPEG 2000 boxes, markers, tile parts, PLT coverage, packet locations,
  codestream bounds, and linked-JPX references. Invalid or unsupported sources
  are rejected instead of failing later or producing inconsistent JPP data.
  Deployed trailing zero PLT entries remain accepted as padding; nonzero extras
  are rejected. QCD and COM lengths and the COM registration value are checked
  against T.800 (Lqcd 4 to 197, Lcom at least 5, Rcom 0 or 1).
- Preserve separate coding parameters for embedded JPX codestreams, use the
  standard default precinct size, and number codestreams by physical box order.
- Bound client-controlled request values before allocation, including cache
  models, response limits, codestream selections, and HTTP request heads.
- Return useful `400`, `404`, `431`, `500`, and `503` responses for malformed
  requests, oversized heads, missing or invalid targets, server failures, and
  unavailable channels.
- Reject client paths containing a parent-directory segment and stop exposing
  resolved filesystem paths through `JPIP-tid`. Trusted links stored inside JPX
  files may still refer to sources outside the image directory.
- Improve cleanup and diagnosis. Logs identify a linked JP2 file that prevents
  its JPX from loading, parser and worker failures release owned resources, and
  orderly shutdown drains queued logs.

## 1.9.0-rc1 - 2026-09-14

### Changed

- Partition top-level JPX association contents into separate metadata bins using
  JPIP placeholders.
- Deliver linked-JPX metadata as larger contiguous data-bin messages and resume
  traversal where the previous response stopped. A representative 2 MiB
  response fell from 261 messages to 32, reducing initial loading of the
  4,014-frame JHelioviewer fixture from 77.27 seconds to 35.94 seconds. Avoiding
  repeated traversal also reduced median local server-and-transfer time from
  305.9 ms to 288.7 ms (5.7%). The 100-frame fixture remained effectively
  unchanged at 0.58 versus 0.56 seconds.
- Release linked-file mappings after indexing and after completing each
  response. For 4,014 links, warm-open peak RSS fell from about 275 MB to 81 MB.
  After 1,000 randomized requests, final RSS fell from about 787 MB to 25 MB.
- Add a conventional out-of-tree CMake build and installation.

### Fixed

- Preserve linked-JPX codestream order when resolving frame URLs.
- Handle partial socket writes when sending chunked responses.
- Accept a JPIP cache-model descriptor at the end of a query string.
