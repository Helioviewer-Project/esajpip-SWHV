# JPIP support profile

esajpip implements the part of JPIP needed to serve JP2 and JPX imagery to
JHelioviewer. It is not a complete implementation of
[ITU-T T.808 / ISO/IEC 15444-9](https://www.itu.int/rec/T-REC-T.808/en).

This document defines the wire behavior and source-file structures that the
server supports. **Reduced** means that esajpip implements only the behavior
described here. Unknown request fields may be ignored for compatibility, so an
accepted request does not by itself prove that every field was honored.

## Wire profile

| Area | Support | Behavior and reason |
| --- | --- | --- |
| HTTP | Reduced | HTTP/1.0 and HTTP/1.1 `GET` requests are accepted. Responses use HTTP/1.1 chunked transfer encoding. Initial inspection rejects other methods and HTTP versions so unrelated Internet traffic consumes as few resources as possible. |
| Return type | JPP-stream only | Successful image responses use `image/jpp-stream`. JPT-stream, complete-file return types, and return-type negotiation are not implemented because JHelioviewer consumes precinct-based JPP-streams. |
| Transport | HTTP only | `JPIP-cnew` advertises `transport=http`. Auxiliary TCP, UDP, and upload transports are not implemented. |
| Sessions and channels | Stateful, reduced | `cnew`, `cid`, and `cclose` are supported. One channel owns one target and processes one request and response at a time. This matches JHelioviewer's access pattern and keeps cache and JPEG 2000 ownership explicit. |
| HTTP connection reuse | Supported | A channel normally remains on a persistent connection, but a later request for its `cid` may arrive on a new connection. At most one replacement connection may wait. The channel, not the HTTP connection, owns the JPIP session state. |
| Stateless requests | Not supported | Initial inspection requires `cnew`, `cid`, or a usable `cclose`. All cache and image state belongs to one channel thread. |
| Concurrent requests | Not supported | Responses are not preempted by a newer request and requests are not served concurrently within a channel. `qid`, `wait`, and window-change cancellation are not implemented. Serial service avoids shared JPEG 2000 state and is compatible with JHelioviewer. |
| Compression | Reduced | If a request contains `metareq` and `Accept-Encoding` contains `gzip`, the JPP response is gzip encoded. Other content codings and general HTTP content negotiation are not implemented. |
| Errors | Reduced | Valid requests receive `200`. Request and serving failures generally receive `500` and terminate the channel. The server does not implement the complete JPIP status and correction-header model. Termination is safer than retaining a cache model after an incomplete response. |

Initial inspection examines at most a 2 KiB request line. The JPIP parser uses
at most the first 1,023 characters of the URI. Request paths and `target` values
are not subject to general URI decoding, so clients should use the literal file
names known to the server. Percent escapes are decoded only within the supported
`model` and `context` grammars. Initial routing and full request parsing use the
same query-field splitter and URI limit. If a non-conforming request repeats a
routing field, both stages use its last value. Client-supplied URI paths and
`target` values containing a path segment equal to `..` are rejected. An
established channel accepts at most 4 KiB for the complete HTTP request head,
including the request line and all headers.

The detailed connection ownership, timeout and cleanup rules are documented in
[Connections and JPIP channels](CHANNELS.md).

## Request fields

| Field | Support | Current interpretation |
| --- | --- | --- |
| `target` | Supported on `cnew` | Selects the file. If absent, the request URI path selects it. A channel cannot change target after creation. Parent-directory path segments are rejected. |
| `cnew` | Reduced | Creates one HTTP channel. The requested transport list and other negotiation details are not interpreted. The response always selects HTTP. |
| `cid` | Supported | Routes a request to an existing channel, including when it arrives on a replacement HTTP connection. |
| `cclose` | Supported | Closes the named channel. `cclose=*` is accepted when the request also supplies the channel to route. There is no multi-channel session to close. |
| `fsiz`, `roff`, `rsiz` | Supported profile | Select resolution and a rectangular window of interest. JHelioviewer supplies these fields together. When omitted, `roff` defaults to `(0,0)` and `rsiz` extends to the lower-right corner. `round-up`, `round-down`, and `closest` are recognized on `fsiz`. The region is cropped to the selected image resolution. An empty intersection returns only `EOR WINDOW_DONE`. Server-adjusted window response headers are not generated. |
| `stream` | Reduced | Selects one codestream or one inclusive numeric range, with selector values capped at 100,000. At most 100,001 codestreams may be selected across all `stream` and `context` fields in one request. Each selected codestream maps the requested window through its own dimensions and packet geometry, and their packets are delivered in interleaved order. The implementation also retains the historical descending-range extension used by existing clients. Lists and the complete standard sampled-range syntax are not supported. |
| `context` | Reduced | Recognizes `jpxl` with one layer number or one inclusive range, capped at 100,000, and treats the selected compositing-layer numbers as codestream numbers. The combined selection limit described for `stream` applies. JPX composition instructions, remapping, and the complete context syntax are not evaluated. This is sufficient for the one-codestream-per-frame JPX movies served to JHelioviewer. |
| `len` | Supported | Limits the number of JPP data-bin bytes generated for the response. The three-byte EOR does not count toward the limit. When omitted, the server sends all data relevant to the request. A client can issue further requests using the same channel and cache model. Negative values are rejected. |
| `model` | Reduced | Accepts additive byte-prefix or complete-bin descriptors for metadata (`M`), main headers (`Hm`), tile headers (`H`), and precincts (`P`), with an optional codestream range. Descriptors are validated against the selected image before they update channel state. Since the source profile has one tile, only tile-header bin zero is valid. Subtractive descriptors, wildcard descriptors, and layer-count descriptors are rejected or unsupported. |
| `metareq` | Compatibility subset | Its presence is recognized, including JHelioviewer's `[*]!!` form, but the expression is not parsed. Metadata is sent according to the server's fixed JPX metadata-bin representation. The field also enables gzip when the HTTP client accepts it. |
| `type`, `tid` and unknown fields | Accepted but ignored | They do not affect serving. This tolerance preserves existing JHelioviewer requests, but it does not provide return-type negotiation or target-ID recovery. |
| Other standard fields | Not supported | `subtarget`, `qid`, `comps`, `srate`, `roi`, `layers`, `quality`, `align`, `wait`, `drate`, `tpmodel`, `need`, `tpneed`, `mset`, upload, capability and preference fields have no implemented semantics. |

The server does not reject every unsupported field. Clients should rely on the
table above rather than treating a successful response as proof that every
field was understood.

## Responses and cache model

The response body contains a sequence of JPP data-bin messages followed by an
end-of-response message. The server emits:

- Metadata data-bins for the JP2 or JPX box structure.
- Codestream main-header data-bins.
- An empty, complete tile-header data-bin for tile 0.
- Precinct data-bins containing packets relevant to the requested window.
- `window done` or `byte limit reached` end-of-response reasons.

Data-bin offsets and completion flags let the client assemble data across
responses. The channel remembers how much of each metadata, header, and precinct
bin it has sent. An additive `model` request can extend that record. This state
belongs to one channel. It is not shared with another client and cannot be
recovered after the channel or serving process is lost.

On channel creation, the response includes `JPIP-cnew` with the assigned `cid`,
`path=jpip` and `transport=http`. It also includes `JPIP-tid: 0`, explicitly
indicating that the server does not assign stable target identifiers or
guarantee target identity across sessions. The remaining JPIP response
preference and correction headers are not emitted.

## Supported JPEG 2000 sources

The server must find packet boundaries without decoding or transcoding the
image. For that reason, it accepts a narrower set of files than the full JP2 and
JPX specifications allow.

### JP2

- The file name must end in `.jp2`.
- The first box must be the standard JPEG 2000 signature box, followed by the
  file-type box.
- Exactly one embedded codestream is supported.
- The codestream must contain one tile, use zero image and tile origins, and
  contain at least one valid `PLT` marker.
- All five Part 1 progression orders are indexed. PCRL and CPRL additionally
  require unit component sampling.
- Explicit precinct dimensions and the Part 1 default of 32,768 by 32,768
  samples are supported. An explicit zero exponent is accepted only at the
  lowest resolution.
- The main header must contain exactly one `SIZ`, one `COD`, and one `QCD`
  marker. They must describe the packet layout. Features that change that
  layout elsewhere, such as progression changes through `POC`, are outside the
  supported profile.
- Code-block style bits defined by Part 1 are accepted. Reserved bits, including
  the HTJ2K flag, are not supported.
- Up to 64 tile-parts and multiple `PLT` markers may be indexed, subject to the
  validated marker, packet, and tile-part bounds. `TPsot` must start at zero
  and increase by one. If `TNsot` is nonzero, it must equal the final
  tile-part count. After tile-part data, only the next `SOT` or the final `EOC`
  marker may follow. A tile-part with `Psot = 0` must be the last tile-part in
  the codestream.
- Packet locations and file-backed data-bin offsets must fit the signed 32-bit
  JPIP state. Source files of 2 GiB or more are outside the supported profile.

Files without `PLT` packet-length information are rejected. esajpip does not
decode packets to rediscover their boundaries and does not replace the separate
transcoding step required for such inputs.

### JPX

- The file name must end in `.jpx`.
- The first box must be the standard JPEG 2000 signature box, followed by the
  file-type box.
- A JPX with no links must contain every declared codestream. Each embedded
  codestream keeps its own coding parameters.
- If links are present, every declared codestream must have a link in the
  supported fragment-table/data-reference form. Any embedded codestreams in the
  same JPX are ignored, and the linked codestreams are served instead.
- A linked codestream uses one `flst` entry containing exactly one fragment.
  The fragment must describe the complete codestream indexed in the referenced
  file.
- Data references use version-zero `file://` URL boxes. Remote HTTP URLs,
  multiple-fragment codestreams, and serving a mixture of embedded and linked
  codestreams are not supported.
- Each link must resolve to a lowercase `.jp2` file with one embedded
  codestream. JPX-to-JPX links are not supported.
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

## Why this profile is narrow

The main production workload is a linked JPX movie with one codestream per
frame. JHelioviewer first requests metadata and then asks for rectangular
precinct windows. Serving that layout directly avoids decoding or rewriting the
source data and keeps each client's memory inside its own channel.

Each channel thread exclusively owns its file index, linked-JPX graph, cache
model, traversal state, and response buffer. Only connection routing is shared.
Broader standard support should preserve this ownership rule. New syntax or
file-model behavior belongs inside a channel and must not introduce shared
JPEG 2000 state between channel threads.

Add unsupported features when a real client or source file needs them and their
data-bin behavior can be checked against T.808. Ignoring an unknown field for
compatibility is not a conformance claim.
