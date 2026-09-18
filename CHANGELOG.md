# Changelog

## 2.0-rc1 - Unreleased

### Changed

- Reduce per-client indexing memory. With the 4,014-frame linked-JPX fixture,
  resident memory at the same held point fell from 13,040 KiB to 8,976 KiB
  (31%). CPU time and elapsed time remained within measurement noise.
- Stream gzip output one configured chunk at a time instead of buffering the
  complete compressed response. This removes libgsf and its dependencies.
- Keep channels alive when a later request arrives on a replacement HTTP
  connection, even when the previous connection stopped partway through a
  request. This lets browser-managed clients retain their JPIP cache and JPEG
  2000 state without controlling the underlying socket.
- Identify JPIP traffic before starting a channel thread or allocating JPEG
  2000 state. Silent and unrelated connections now expire at the short initial
  timeout with only their sockets retained.
- Replace the old libconfig file with `server.ini`, parsed by GLib. Existing
  configuration files must be rewritten for 2.0.
- Replace log4cpp with bounded, nonblocking logging. Serving threads no longer
  wait for log-file I/O. Logs still include timestamps and optional request
  lines, roll at 1 GiB, and retain one backup.
- Simplify operation around a supervisor and one serving process. The
  supervisor keeps the listening socket open and restarts the server after an
  unexpected exit, with a short delay to prevent a persistent failure from
  causing a tight restart loop. The unused `status` command and its shared-memory
  registry are gone, and log names now include the listening address and port.
- Add verified PCRL and CPRL packet traversal and allow one multi-codestream
  request to cover frames with different dimensions, decomposition levels,
  quality layers, or precinct layouts.
- Require HTTP/1.1, matching the chunked responses used by the server and the
  requests sent by JHelioviewer.

### Fixed

- Keep parsed request values local to one HTTP request. A later request can no
  longer inherit a window size, offset, response limit, or rounding direction
  omitted by that request.
- Make response limits, cache-model updates, and metadata continuation reliable
  across requests. An omitted `len` is unlimited, small limits still produce a
  complete end-of-response message, and metadata resumes correctly even when a
  model ends inside a JPX placeholder.
- Apply the standard window defaults and resolution selection, map both window
  boundaries to the selected resolution, and include every precinct
  intersecting the result. An empty intersection now returns
  `EOR WINDOW_DONE` instead of failing the channel.
- Validate JPEG 2000 boxes, markers, tile parts, PLT coverage, packet counts
  and locations, codestream bounds, and linked-JPX references before serving
  data. Trailing zero `PLT` entries produced by the deployed OpenJPEG
  transcoder are tolerated without treating them as packets; nonzero trailing
  entries remain invalid. Tile-part coding overrides, JPX-to-JPX links,
  unsupported geometry, files of 2 GiB or more, and codestreams with more than
  64 packet segments are rejected during parsing instead of failing later or
  producing an inconsistent stream.
- Keep each embedded JPX codestream's coding parameters separate, use the
  standard default precinct size, number JPX codestreams by physical box order,
  and reject image origins the packet index cannot represent.
- Bound and validate client-controlled request values before allocation. This
  includes cache models, response limits, codestream selectors, and the 4 KiB
  HTTP request-head limit on established channels.
- Report malformed requests, missing targets, and unavailable channels with the
  corresponding `400`, `404`, and `503` status instead of a generic error or a
  silent disconnect.
- Reject client paths containing a parent-directory segment and stop exposing
  resolved filesystem paths through `JPIP-tid`. Trusted links stored inside JPX
  files may still refer to sources outside the image directory.
- Improve failure handling and diagnosis. Logs identify a linked JP2 file that
  prevents its JPX from loading, parser and thread failures release their files
  and connections, orderly shutdown drains queued logs, and unexpected serving
  process exits are recorded when the replacement starts.

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
