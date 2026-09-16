# Changelog

## 2.0-rc1 - future

### Changed

- Reduce per-client indexing state. On a 4,014-link workload, resident memory
  at the same held point fell from 13,040 KiB to 8,976 KiB (31%), while CPU and
  elapsed time remained within measurement noise.
- Stream gzip output with zlib, bounding compressed-response buffering to one
  configured chunk instead of the complete response. This also removes libgsf
  and its dependencies.
- Identify JPIP traffic before creating a serving thread or JPEG 2000 state. In
  a test with 100 silent connections, the server retained its baseline eight
  descriptors and all connections expired at the configured three-second
  deadline.
- Preserve a JPIP channel across replacement HTTP connections, allowing clients
  without direct socket control to retain the channel's cache and JPEG 2000
  state across reconnections.
- Replace the libconfig format with `server.ini` parsed by GLib. This is an
  intentional configuration incompatibility.
- Remove the bundled log4cpp library. Logging is now nonblocking for serving
  threads while retaining timestamped output, optional request logging, and the
  1 GiB active log with one backup.
- Support PCRL and CPRL packet ordering for the verified origin-zero,
  single-tile geometry with unit component sampling.
- Reduce the parent process to supervising and restarting the serving process.
  Connection admission and channel routing now occur in one event loop, removing
  cross-process descriptor passing and duplicate client descriptors while
  retaining bounded pre-identification and channel-owned JPEG 2000 state.

### Fixed

- Reject unsupported multi-tile and PCRL/CPRL geometries instead of indexing
  them with incorrect single-tile assumptions.
- Validate JPEG 2000 markers, tile parts, packets, boxes, codestream bounds, and
  linked-JPX external references before indexing or copying them.
- Reject invalid JPIP cache lengths, response limits, and codestream selectors.
  Use codestream zero when a window request omits its selector.
- Accept valid HTTP header whitespace and exact-fit JPIP messages while
  rejecting incomplete headers and overfull messages.
- Track completion with stable connection identifiers so descriptor reuse cannot
  close an unrelated client. Expire inactive channels and terminate the child process
  whenever its parent exits.
- Close mappings and connections on parser and thread failures, and correct
  address-resolution and platform-specific alignment errors.

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
- Modernize the CMake build.

### Fixed

- Preserve linked-JPX codestream order when resolving frame URLs.
- Handle partial socket writes when sending chunked responses.
- Accept a JPIP cache-model descriptor at the end of a query string.
