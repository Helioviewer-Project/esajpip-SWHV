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

### Added

- A formal description of the accepted files in ASN.1/ACN (`spec/`), with a
  generated, labeled test corpus that the server tests check against. The
  generator checks every vector's label and the rule that rejects it, and
  `spec/check-model.sh --static` checks, without a compiler, that the code
  restating the model agrees with it.
- A JPEG 2000 reader/writer library (`lib/`), built on code generated from
  that description and sharing its rules, and the `hv_walk` tool. It decodes
  and encodes one header or one list element at a time (SIZ components, PLT
  packet lengths, Reader Requirements features, palette column depths), up
  to the end the segment or box length gives, so it holds no struct sized by
  the standard's bound on a list; the header box checks keep where a `bpcc`
  box's entries are rather than copying them.
- `hv_transcode` (`transcode/`), a C port of hvJP2K's transcoder that
  rejects files it cannot make servable.
- `hv_merge` (`merge/`), a C port of hvJP2K's JPX merger (`hv_jpx_merge`),
  writing the same bytes, from inputs within the served profile only. It
  also rejects a later input that lacks a `cdef` or `res` box the first
  input has, and a first input with two `colr` boxes of the same method
  (`colr.one-method` in the JPX file), both of which hvJP2K accepts. Its
  command line is parsed as hvJP2K's argparse does (`-s -` reads standard
  input). Both tools write their output through a temporary file, keeping
  a symbolic link and, for `hv_merge`, an existing output's permissions.
- The JP2 header boxes (T.800 I.5.3, T.801 M.11.5 to M.11.7) in the model,
  with their rules shared by the corpus and the reader (`hv_check_jp2h`,
  `hv_check_jpx_headers`, `hv_walk -H`). `hv_transcode` and `hv_merge` reject
  inputs whose header boxes are invalid or disagree with the codestream; the
  server, which does not read them, is unchanged. The JPX Reader
  Requirements box too, which `hv_merge` writes with the model's encoder.
  `lib/generated` is made with the pinned ASN1SCC and the local patches in
  `spec/asn1scc-patches/`.

### Changed

- Patch the pinned ASN1SCC (`spec/asn1scc-patches/`) to validate constrained
  subtypes inside `CONTAINING` fields. Regenerated corpus labels now report
  those profile failures at decode time. Keep the reader's named C errors for
  the same constraints.
- Model: VERS and FLAG of a Data Entry URL box must be 0 at the standard
  layer too (T.800 I.7.3.2), so `jpx-linked-rule-url.version-flags-7.jpx`
  is invalid at both layers. The Fragment List box takes the ranges of
  T.801 Table M.17 (NF and LEN from 0, OFF from 12), with three new
  vectors at those edges. No SOP markers is a constraint of the profile's
  COD type, as the other profile narrowings are.
- Model: the reader and writer share the whole-file model's types instead
  of copies (`CodSegment`, `QcdSegment`, `FtypHeader`, `UrlHeader`,
  `DataReferenceCount`, `FragmentCount`), and the profile framing uses the
  standard QCD, PLT and COM segments; the `-Std` types, the parameterized
  bodies and the profile aliases that narrowed nothing are gone.
  `spec/check-model.sh` compares `SotSegment` with `TilePart`.
- Model, JPX header boxes at the standard layer: a `colr` with METH 1 may
  carry the CIELab or CIEJab parameters after EnumCS (T.801 M.11.7.4), and
  has APPROX 1 to 4 (`colr.approx`); MinV is 1 (`file.ftyp-minor`); an IPR
  box in a `jpch` needs IPR 1 in its `ihdr` (`jpch.ipr`); `creg` is in
  every `jplh` or none (`jpx.creg`). JP2: the `ihdr`'s IPR is 1 exactly
  when the file has an IPR box (`ihdr.ipr`). The profile, and so what the
  server accepts, is unchanged; the JPX base vectors carry APPROX 1.
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
- Validate JPEG 2000 boxes, markers, tile-parts, PLT coverage, packet locations,
  codestream bounds, and linked-JPX references. Invalid or unsupported sources
  are rejected instead of failing later or producing inconsistent JPP data.
  Deployed trailing zero PLT entries remain accepted as padding; nonzero extras
  are rejected. QCD and COM lengths and the COM registration value are checked
  against T.800 (Lqcd 4 to 197, Lcom at least 5, Rcom 0 or 1). COC, POC,
  PPM, markers T.800 places elsewhere, 0xFF00 and the segment-less 0xFF30 to
  0xFF3F are rejected in the main header, and PLT entries longer than ten
  bytes are rejected.
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
