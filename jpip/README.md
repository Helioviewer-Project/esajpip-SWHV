# JPIP requests, indexed targets and responses

`jpip` is a C++ library built alongside `jpeg2000`. It has no server,
HTTP runtime, libuv, gzip, GLib, or logging dependency. Its public operations are:

- `jpip::Request::ParseTarget`: parse the request target extracted by the HTTP
  caller into the supported JPIP fields and channel-operation values. Parsing
  returns errors to the caller; it does not log, open files, or manage channels.
  `HasRoutingParameter` classifies a target without allocating a parsed query.
- `jpip::ImageIndex::Open`: parse a borrowed source and resolve linked
  sources through `jpip::SourceProvider`. The existing profile reader supplies
  structure and decoded fields; PLT entries remain deferred until indexed.
- `jpip::DataBinServer::SetRequest`: validate a typed `ResponseRequest` against
  the target, then update the session and cache. The typed `ResponseRequest`
  can be constructed directly or supplied as the response part of `Request`.
- `jpip::DataBinServer::GenerateChunk`: write JPP bytes into the caller's buffer
  using source access supplied by the caller. A successful call produces bytes
  or completes the response. Every failure supplies a nonempty `GetError()`
  diagnostic. Source-acquisition failures name the source;
  deferred parsing failures retain the index diagnostic and source identity.
  Failure terminates use of that session. Buffers smaller than an EOR are rejected.
  Placeholders can span responses. Tiny request budgets (`len=0..60`) return
  byte-limit EOR alone and leave the session usable. A smaller caller buffer
  that prevents progress still reports an error.

Build selections with `ResponseRequest::AddStream(first, last, step)` and
`AddContext(first, last)`. They preserve insertion order and record the first
explicit stream used by unqualified cache-model updates. Context selection
alone leaves that default at stream 0. An explicit first stream outside the
target is not silently replaced by a later available stream. `SetRequest`
rejects descending/zero-step ranges, negative response budgets, and invalid
cache-bin identifiers before changing session state. Other request fields
retain their presence flags. The parsed `Request::has_metareq` flag is available
to the server for its gzip policy. It does not affect the fixed metadata selection or appear in
`ResponseRequest`.

`Source::Read(offset, destination, length)` is a bounded, const range read;
there is no shared seek cursor. `SourceProvider::GetSource` returns a borrowed
const source, not ownership of it. Sessions cannot be copied.

The target must outlive its response session. Calls for one target/session are
serialized; different targets may be used concurrently. Sources are borrowed
and remain alive through a response. They may be unmapped afterward: the index
retains offsets, and the response releases its source pointers on completion.
Sources are immutable for the indexed target's lifetime, including across
unmapping and reacquisition. Mutation is a configuration/operational error
outside this contract. During opening,
the index releases each linked source immediately after extracting its index
data, so file adapters need not keep the entire movie mapped.

`ImageIndex::GetPacket` argument errors are recoverable. Actual indexing/format
failures are terminal. Unsupported cache classes and progression orders return
`-1`; they do not throw `logic_error`.

## Responsibilities

| Directory | Responsibility |
| --- | --- |
| `source/` | Borrowed bytes, source-provider contract and byte ranges |
| `index/` | Indexed targets, packet geometry, deferred packet index and JPIP metadata layout |
| `request/` | Supported JPIP parameter syntax, channel-operation values and typed response inputs |
| `response/` | Window traversal, session cache, response continuation and JPP output |

All library types are in `jpip`; server adapters are in `server`. Include public
headers from another directory with their library prefix, such as
`"jpip/request/request.h"`. Use local names for headers in the same directory.
Generated ASN.1 headers retain their existing names and configured include path.

The shared data-bin classes and end-of-response codes remain in `jpip.h`.
The server extracts the target from HTTP and uses the library's `Request`.
It owns channel identifiers and lookup, target opening, admission limits,
scheduling, cancellation and connection lifetime. Its worker and response
adapter accept only `ResponseRequest`; routing fields stay with the caller.
`server/storage/` supplies mapped sources, while `server/http/` owns HTTP
request-head parsing and connections. The C format tools continue to link
only `jpeg2000`.

The parser preserves the supported profile, including `tid=0`, the existing
cache-model subset, and rejection of `cclose=*`. It is not a general
implementation of every Part 9 request field. Error details formerly logged
inside cache-model parsing are returned to the caller instead. The server logs
the diagnostic and returns it in its HTTP error response.

The server formerly accepted an `asoc` inside `dtbl`. The shared reader rejects
it as `dtbl.non-url`: T.801 M.11.2 defines NDR followed by Data Entry URL boxes.
The former malformed fixture is retained as a rejection test; a separate valid
fixture still verifies nested association/fragment-table metadata layout.

`tests/jpip/request/test_request.cc` links only `jpip` and checks the supported request
syntax, JHV requests, route classification, selectors and cache-model diagnostics.
The HTTP integration tests still cover extraction, malformed requests and
channel handling through the server.

`tests/jpip/response/test_session.cc` links only `jpip` and exercises memory-backed
sources, remapping, exact packet ranges, independently reconstructed metadata,
partial caches, chunk and response limits, repeated windows, metadata-only and
empty windows, and zoom against constructed expectations. Synthetic JP2 and
embedded JPX cases cover all progressions, multiple precincts/components/layers,
tile-parts and padding. Linked JPX and source-failure cases remain covered.
`test_index` compares compact geometry with `hv_geometry_packets` and ordered
with shuffled indexing across the current corpus, including deferred failures.
Cache, window, geometry, byte-range and writer tests are grouped by responsibility
under `tests/jpip/` and also link only the library. See the
[test guide](../tests/jpip/README.md) for selection and extension instructions.
Server tests retain file-mapping and channel/HTTP integration checks.

C++ headers use `#pragma once`. C headers retain their existing include guards.
