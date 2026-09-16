# JPIP support profile

esajpip implements the JPIP subset used to serve JP2 and JPX imagery to
JHelioviewer. It does not implement every facility defined by
[ITU-T T.808 / ISO/IEC 15444-9](https://www.itu.int/rec/T-REC-T.808/en).

This document describes the current wire and input-file profile. **Supported**
means that a behavior has a concrete implementation path and has been checked
against its callers and tests. **Reduced** means that esajpip implements only the
semantics described here. An unrecognized request field may be ignored for
compatibility. Acceptance alone does not indicate support.

## Wire profile

| Area | Support | Behavior and reason |
| --- | --- | --- |
| HTTP | Reduced | HTTP/1.0 and HTTP/1.1 `GET` requests are accepted. Responses use HTTP/1.1 chunked transfer encoding. Initial inspection rejects other methods and HTTP versions so unrelated Internet traffic consumes as few resources as possible. |
| Return type | JPP-stream only | Successful image responses use `image/jpp-stream`. JPT-stream, complete-file return types, and return-type negotiation are not implemented because JHelioviewer consumes precinct-based JPP-streams. |
| Transport | HTTP only | `JPIP-cnew` advertises `transport=http`. Auxiliary TCP, UDP, and upload transports are not implemented. |
| Sessions and channels | Stateful, reduced | `cnew`, `cid`, and `cclose` are supported. One channel owns one target and processes one request and response at a time. This matches JHelioviewer's access pattern and keeps cache and JPEG 2000 ownership explicit. |
| HTTP connection reuse | Supported | A channel normally remains on a persistent connection, but a later request for its `cid` may arrive on a new connection. At most one replacement connection may wait. JPIP sessions are therefore not incorrectly identified with individual HTTP connections. |
| Stateless requests | Not supported | Initial inspection requires `cnew`, `cid`, or a usable `cclose`. Keeping all cache and image state in one channel thread is the deliberately supported model. |
| Concurrent requests | Not supported | Responses are not preempted by a newer request and requests are not served concurrently within a channel. `qid`, `wait`, and window-change cancellation are not implemented. Serial service avoids shared JPEG 2000 state and is compatible with JHelioviewer. |
| Compression | Reduced | If a request contains `metareq` and `Accept-Encoding` contains `gzip`, the JPP response is gzip encoded. Other content codings and general HTTP content negotiation are not implemented. |
| Errors | Reduced | Valid requests receive `200`. Request and serving failures generally receive `500` and terminate the channel. The server does not implement the complete JPIP status and correction-header model. Termination is safer than retaining a cache model after an incomplete response. |

Initial inspection examines at most a 2 KiB request line. The JPIP parser uses at most
the first 1,023 characters of the URI. Request paths and `target` values are not
subject to general URI decoding, so clients should use the literal file names
known to the server.

The detailed connection ownership, timeout and cleanup rules are documented in
[Connections and JPIP channels](CHANNELS.md).

## Request fields

| Field | Support | Current interpretation |
| --- | --- | --- |
| `target` | Supported on `cnew` | Selects the file. If absent, the request URI path selects it. A channel cannot change target after creation. |
| `cnew` | Reduced | Creates one HTTP channel. The requested transport list and other negotiation details are not interpreted. The response always selects HTTP. |
| `cid` | Supported | Routes a request to an existing channel, including when it arrives on a replacement HTTP connection. |
| `cclose` | Supported | Closes the named channel. `cclose=*` is accepted when the request also supplies the channel to route. There is no multi-channel session to close. |
| `fsiz`, `roff`, `rsiz` | Supported profile | Select resolution and a rectangular window of interest. JHelioviewer supplies these fields together. `round-up`, `round-down`, and `closest` are recognized on `fsiz`. Server-adjusted window response headers are not generated. |
| `stream` | Reduced | Selects one codestream or one inclusive numeric range, with selector values capped at 100,000. The implementation also retains the historical descending-range extension used by existing clients. Lists and the complete standard sampled-range syntax are not supported. |
| `context` | Reduced | Recognizes `jpxl` with one layer number or one inclusive range, capped at 100,000, and treats the selected compositing-layer numbers as codestream numbers. JPX composition instructions, remapping, and the complete context syntax are not evaluated. This is sufficient for the one-codestream-per-frame JPX movies served to JHelioviewer. |
| `len` | Supported | Limits the number of JPP bytes generated for the response. A client can issue further requests using the same channel and cache model. Negative values are rejected. |
| `model` | Reduced | Accepts additive byte-prefix or complete-bin descriptors for metadata (`M`), main headers (`Hm`), tile headers (`H`), and precincts (`P`), with an optional codestream range. Subtractive descriptors, wildcard descriptors, and layer-count descriptors are rejected or unsupported. |
| `metareq` | Compatibility subset | Its presence is recognized, including JHelioviewer's `[*]!!` form, but the expression is not parsed. Metadata is sent according to the server's fixed JPX metadata-bin representation. The field also enables gzip when the HTTP client accepts it. |
| `type`, `tid` and unknown fields | Accepted but ignored | They do not affect serving. This tolerance preserves existing JHelioviewer requests, but it does not provide return-type negotiation or target-ID support. |
| Other standard fields | Not supported | `subtarget`, `qid`, `comps`, `srate`, `roi`, `layers`, `quality`, `align`, `wait`, `drate`, `tpmodel`, `need`, `tpneed`, `mset`, upload, capability and preference fields have no implemented semantics. |

The server does not reject every unsupported field. A standards-based client
must not infer support merely because a request containing one receives a
response.

## Responses and cache model

The response body contains a sequence of JPP data-bin messages followed by an
end-of-response message. The server emits:

- Metadata data-bins for the JP2 or JPX box structure.
- Codestream main-header data-bins.
- An empty, complete tile-header data-bin for tile 0.
- Precinct data-bins containing packets relevant to the requested window.
- `window done` or `byte limit reached` end-of-response reasons.

Data-bin offsets and completion flags allow a client to assemble contributions
across responses. The channel records the byte prefix sent for each metadata,
header and precinct bin, and an additive `model` request can extend that record.
The model is private to the channel. It is neither shared between clients nor
recovered after the serving process or channel is lost.

On channel creation, the response includes `JPIP-cnew` with the assigned `cid`,
`path=jpip` and `transport=http`. It also includes `JPIP-tid`, but the value is
the server-resolved file path rather than an independently managed opaque target
ID. The remaining JPIP response preference and correction headers are not
emitted.

## Supported JPEG 2000 sources

JPIP response correctness depends on the server being able to index packet
boundaries without decoding or transcoding the image. The accepted source
profile is therefore narrower than the JP2 and JPX file formats themselves.

### JP2

- The file name must end in `.jp2`.
- Exactly one embedded codestream is supported.
- The codestream must contain one tile and at least one valid `PLT` marker.
- All five Part 1 progression orders are indexed. PCRL and CPRL additionally
  require origin-zero, single-tile geometry and unit component sampling.
- The main `SIZ`, `COD`, and `QCD` information must describe the packet layout.
  Marker features that change that layout without being represented there,
  such as progression changes through `POC`, are outside the supported profile.
- Multiple tile-parts and multiple `PLT` markers may be indexed, subject to the
  validated marker, packet, and tile-part bounds.
- Packet locations must fit the current 32-bit packet-index representation.
  Source files of 4 GiB or more are outside the supported profile.

Files without `PLT` packet-length information are rejected. esajpip does not
decode packets to rediscover their boundaries and does not replace the separate
transcoding step required for such inputs.

### JPX

- The file name must end in `.jpx`.
- JPX files may contain embedded codestreams with common coding parameters, or
  every declared codestream may be linked through the supported
  fragment-table/data-reference form.
- A linked codestream uses one `flst` entry containing exactly one fragment.
  The fragment must describe the complete codestream indexed in the referenced
  file.
- Data references use version-zero `file://` URL boxes. Remote HTTP URLs,
  multiple-fragment codestreams, and mixed embedded/linked codestream sets are
  not supported.
- Each link must resolve to a file with an embedded codestream. Recursion through
  another linked JPX is not supported.
- Top-level association boxes are exposed as separate metadata bins. Other box
  contents are preserved in metadata, but esajpip does not interpret the full
  JPX composition model.

This linked profile matches the movies produced for JHelioviewer by
`hv_jpx_merge` and the compatible `kdu_merge` form used in deployment. It
preserves codestream order and validates link counts, reference indices,
fragment ranges, and source codestream structure before serving.

Linked JPX files are trusted server-side inputs. Their decoded `file://`
references are opened as local paths. JPIP protocol handling neither fetches
them over the network nor confines them to the configured image directory.

Raw codestream files and other JPEG 2000 family extensions are not accepted.

## Why this profile is deliberately narrow

The common production workload is a linked JPX movie in which each frame maps
directly to one codestream and JHelioviewer requests metadata followed by
rectangular precinct windows. Supporting that path directly avoids decoding or
rewriting image data and keeps memory proportional to the active channel and
response rather than to shared state across clients.

Each channel thread exclusively owns its file index, linked-JPX graph, cache
model, traversal state, and response buffer. Only connection routing is shared.
Broader standard support should preserve this ownership rule. New syntax or
file-model behavior belongs inside a channel and must not introduce shared
JPEG 2000 state between channel threads.

Unsupported features should be added when a real client or source file needs
them and when their data-bin semantics can be verified against T.808. Silent
acceptance of an unknown request field must not be turned into a conformance
claim.

## Source-to-document verification

The profile above was checked in both directions against the current source:

| Source area | Externally visible behavior covered here |
| --- | --- |
| `server/initial_request.cc` | Accepted method and HTTP versions, plus bounded `cnew`, `cid`, and `cclose` inspection |
| `server/server.cc` and `server/connection_queue.cc` | Channel selection and serialized replacement connections |
| `server/channel.cc` | Channel lifecycle, request serialization, response headers, chunking, gzip, errors, and timeouts |
| `jpip/request.cc` | Recognized fields, reduced grammars, ignored fields, and cache-model descriptors |
| `jpip/databin_server.cc` and `jpip/databin_writer.cc` | Cache updates, response length, emitted data-bin classes, message headers, and EOR reasons |
| `jpeg2000/file_manager.cc` | Accepted extensions, boxes, links, markers, PLT requirement, and rejected source structures |
| `jpeg2000/coding_parameters.h` | Progression-order indexing and precinct data-bin identifiers |
| `tests/protocol_test.cc` | JHelioviewer request forms, initial inspection, headers, cache descriptors, and message encoding |
| `tests/jpeg2000_test.cc` | JP2, embedded JPX, linked JPX, progression orders, and malformed-input rejection |

Conversely, the profile covers every request field parsed by `Request`, every
JPIP response header and data-bin class emitted by the channel, and every
JPEG 2000 marker or JPX box handled specially by `FileManager`. This is an audit
of the implementation profile, not a claim that the tests cover every
combination permitted by the standard.
