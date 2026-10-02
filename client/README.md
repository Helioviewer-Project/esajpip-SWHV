# client

A JPIP client for the JP2 images and JPX movies of an esajpip server. For each
frame (codestream) it gives 8-bit pixels at a chosen resolution level, the
frame's XML and its color table. It requests only the data it lacks, and can
fetch frames without decoding them.

It has two interfaces: `js/source.mjs`, over a WebAssembly build, for web
applications, and the C library `esajpip_client`, which does no I/O, for
native hosts. `hv_jpp2j2k` and a demonstration page exercise it against a
server.

## Semantics

These hold for both interfaces.

- **Frame.** The index of a codestream of the target, from 0: one for a JP2,
  one per image for a JPX, embedded or linked alike. The frames of a JPX may
  differ in size, components, resolution levels and color table.
- **`reduce`.** The number of highest resolution levels discarded, as with
  `opj_decompress -r` and `kdu_expand -reduce`. The size is the full size
  divided by 2^`reduce`, rounded up. A value beyond the frame's levels is
  clamped to its lowest resolution. JavaScript accepts nonnegative integers
  and `Infinity` (the lowest resolution), and rejects other values.
- **Requests.** The whole image at a resolution: `fsiz` alone, no `roff` or
  `rsiz`, no `len` or `layers`. There are no regions and no decoding of a
  frame that is still arriving.
- **Cache.** Every data-bin received on the channel is kept and none is
  evicted, as the server has no subtractive cache model. A resolution level of
  a frame is complete when all its precinct bins are; a request brings only
  the levels that are not. Memory includes the fetched compressed bytes, spare
  bin capacity, the hash table and the metadata index. Decoding also uses a reconstructed
  codestream, OpenJPEG working buffers and the last decoded image; the host
  owns another copy of the pixels.
- **Pixels.** 8 bits per sample, 1 component (gray) or 3 (the first three, as
  RGB), interleaved, top row first. Deeper samples keep their 8 most
  significant bits.
- **Color table.** The `pclr` that `cmap` applies to the frame's component,
  from `jp2h` or, in a JPX, from the frame's `jpch` where that has one, with
  values scaled to 8 bits. It is not applied: the samples of such a frame are
  indices into it, preserved without sample scaling. Indexed output supports
  one unsigned component of at most eight bits; wider or signed palette
  indices are rejected rather than truncated. Palette lookups clamp an index
  beyond the table to its last entry, as the demonstration page does.
- **XML.** In a JPX, the `xml` box of the `asoc` whose `nlst` names the
  codestream or its compositing layer, as `hv_merge` and hvJP2K write it; in a
  JP2, the first top-level `xml` box.
- **Metadata.** The first response of a channel carries the XML and color
  tables of all frames.
- **Channel lifetime.** The server ends a channel idle for
  `connections.timeout` (60 s by default). A request on an ended channel
  fails, and the client then makes no more on it. What is cached still
  decodes, but cannot be carried to a new channel.
- **Scope.** The files and responses of this server
  ([`JPIP_PROFILE.md`](../JPIP_PROFILE.md)), not JPIP in general.

## JavaScript

`source.mjs`, `worker.mjs`, `jpip.mjs` and `esajpip_client.wasm` are deployed
together in one directory ([build](#building-the-webassembly-module)); a page
imports `source.mjs`. The server allows any origin.

```js
import { JpipSource } from "./source.mjs";

const source = await JpipSource.open({
    wasm: new URL("esajpip_client.wasm", import.meta.url),
    server: "http://localhost:8900", image: "movie.jpx" });

const frame = await source.frame(3, 1);         // frame 3 at half size
gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, frame.width, frame.height, 0,
              gl.RED, gl.UNSIGNED_BYTE, frame.pixels);

const lut = await source.palette(3);
if (lut?.channels === 3)
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB8, lut.entries, 1, 0,
                  gl.RGB, gl.UNSIGNED_BYTE, lut.table);
const xml = await source.xml(3);

await source.close();
```

| `JpipSource` | Result | Requests |
| --- | --- | --- |
| `JpipSource.open({ wasm, server, image })` | A source on a new channel. `image` is a path below the server's image directory, `wasm` the URL of the module | 1 |
| `frames` | Number of frames | |
| `received` | Bytes of response bodies so far | |
| `frame(index, reduce = 0)` | The decoded frame, below | For the levels not cached |
| `fetch(index, reduce = 0)` | `cached(index)` after fetching; nothing is decoded | For the levels not cached |
| `cached(index)` | The least `reduce` that needs no request; `null` if no level is complete | 0 |
| `xml(index)` | A string, or `null` | 0 |
| `palette(index)` | `{ entries, channels, table }`, or `null` | 0 |
| `close()` | Closes the channel and ends the worker | 1 |

All but `frames` and `received` return promises.

| Field of a frame | |
| --- | --- |
| `index`, `reduce` | As asked for, `reduce` after clamping |
| `width`, `height`, `components` | Of `pixels`; `components` is 1 or 3 |
| `pixels` | `Uint8Array` of `width * height * components`, a copy the caller owns |
| `fullWidth`, `fullHeight`, `resolutions` | The size at `reduce` 0 and the number of resolution levels |

`table` of a palette is a `Uint8Array` of `entries * channels` values, also a
copy; `channels` is 3 for RGB.

A movie is fetched ahead of playing it with one `fetch` at a time, at the
`reduce` it plays at; `frame` then makes no request, and `cached` gives the
progress:

```js
for (let index = 0; index < source.frames; index++)
    await source.fetch(index, reduce);
```

- **Order.** The calls of a source that make requests (`frame`, `fetch`,
  `close`) run one at a time, in the order made. With one `fetch` pending at a
  time, as above, a `frame` call waits for one request at most; fetches issued
  all at once would all run before it.
- **Decoding.** Every `frame` call decodes; decoded frames are not cached.
- **Threads.** A source is one JPIP channel in one Web Worker; sources are
  independent. The module is compiled once per page.
- **Errors.** Promises reject with an `Error`. A bad `index` gives a
  `RangeError`. After a failed request the source makes no more: calls that
  need one reject with the same error, and calls served from the cache still
  succeed. For more data, the image has to be opened again. "the server did
  not send frame N whole" fails the call only.
- **Without a worker.** `jpip.mjs` exports `JpipChannel`, which `worker.mjs`
  wraps; it also runs under Node.js.

## C

The CMake target is `esajpip_client`, a static library with its headers in
`client/`. The host makes the requests and passes each response body to the
library.

### Requests

One channel per target, one request at a time.

```
GET /movie.jpx?cnew=http&type=jpp-stream&stream=3&fsiz=1024,1024,closest
GET /<path>?cid=<cid>&stream=3&fsiz=2048,2048,closest
GET /<path>?cid=<cid>&cclose=<cid>
```

- `<cid>` and `<path>` are the `cid` and `path` fields of the `JPIP-cnew`
  header of the first response.
- `stream` is the frame and `fsiz` its size at the `reduce` wanted.
- The size of a frame is known once its main header has arrived. Before that,
  request with another frame's size, check `hv_reconstruct_status`, and
  request again with its own size if a level is missing.
- [`CHANNELS.md`](../CHANNELS.md) describes the server's channels, status
  codes and timeouts.

### Calls

| Call | Header | Result |
| --- | --- | --- |
| `hv_cache_begin`, `hv_cache_release` | `hv_cache.h` | The store of one channel |
| `hv_jpp_begin`, `hv_jpp_next`, `hv_jpp_reason` | `hv_jpp.h` | The messages of a response body and its end-of-response reason |
| `hv_cache_apply` | `hv_cache.h` | A message added to the store |
| `hv_metadata_open`, `hv_metadata_close` | `hv_metadata.h` | Index complete metadata once; `metadata.count` is the number of frames |
| `hv_metadata_xml` | `hv_metadata.h` | A frame's XML, in place in the store |
| `hv_metadata_palette` | `hv_metadata.h` | A frame's color table, copied out |
| `hv_reconstruct_status` | `hv_reconstruct.h` | A frame's size, components and resolution levels, and how many levels are complete |
| `hv_reconstruct` | `hv_reconstruct.h` | A frame as a JPEG 2000 codestream |
| `hv_image_decode` | `hv_image.h` | A codestream decoded to pixels |

```c
hv_cache cache;
hv_cache_begin(&cache);

/* Each response body. */
hv_jpp_reader reader;
hv_jpp_message message;
int next;
hv_jpp_begin(&reader, body, body_size);
while ((next = hv_jpp_next(&reader, &message)) == HV_JPP_MESSAGE)
    if (!hv_cache_apply(&cache, &message))
        return fail(hv_cache_error(&cache));
if (next == HV_JPP_ERROR)
    return fail(hv_jpp_error(&reader));

/* Metadata, after the first response. */
char error[256];
hv_metadata metadata = {0};
if (hv_metadata_open(&cache, &metadata, error, sizeof error) != 0)
    return fail(error);
const uint8_t *xml;                     /* NULL if the frame has none */
size_t xml_size;
if (hv_metadata_xml(&metadata, frame, &xml, &xml_size, error, sizeof error) != 0)
    return fail(error);
uint8_t *table = malloc(HV_PALETTE_MAX);
int channels;                           /* entries is 0 if it has none */
int entries = hv_metadata_palette(&metadata, frame, &channels, table,
                                  HV_PALETTE_MAX, error, sizeof error);
if (entries < 0)
    return fail(error);
free(table);

/* A frame at its highest complete resolution level. */
hv_status status;
if (hv_reconstruct_status(&cache, frame, &status, error, sizeof error) != 0)
    return fail(error);
if (status.complete == 0)
    return 0;                           /* request it first */
size_t size = hv_reconstruct(&cache, frame, NULL, 0, error, sizeof error);
uint8_t *codestream = malloc(size);
hv_reconstruct(&cache, frame, codestream, size, error, sizeof error);
hv_image image;
int reduce = status.resolutions - status.complete;
hv_image_mode mode = entries ? HV_IMAGE_INDICES : HV_IMAGE_SAMPLES;
if (hv_image_decode(codestream, size, reduce, mode, &image,
                    error, sizeof error) != 0)
    return fail(error);
free(image.pixels);
free(codestream);

hv_metadata_close(&metadata);
hv_cache_release(&cache);
```

- **Results.** Each header documents its calls' results. Calls that take
  `error` and `error_size` write a message there on failure.
- **`hv_status`.** `complete` counts levels from the lowest, so the least
  usable `reduce` is `resolutions - complete`. All fields are 0 until the
  frame's main header is complete. The call parses the main header and looks
  the precinct bins up; it reads no packet.
- **`hv_reconstruct`.** The output is RPCL with empty packets above the
  complete levels, valid for any decoder that is told to discard `resolutions
  - complete` levels. It refuses a frame with a partial precinct bin.
- **Lifetimes.** The metadata index borrows complete bins, which never change
  as other bins arrive. Its XML pointers stay valid until `hv_cache_release`.
  Initialize the index to zero, close it before reopening it, and release it
  before the store.
  `HV_PALETTE_MAX` bytes hold any table; a call with capacity 0 returns
  `entries` and `channels` only. `image.pixels` is malloc'd.
- **Errors.** After a failure of `hv_jpp_next` or `hv_cache_apply` the store
  no longer matches the server's record of the channel: close the channel.
  What is complete in the store still decodes.
- **Threads.** A store has no locking.

`hv_wasm.c` and `js/jpip.mjs` together are a complete host.

## Building the WebAssembly module

The WebAssembly build uses the C compiler of [zig](https://ziglang.org/) (`brew install
zig`), which carries the C library OpenJPEG needs. Without zig the target does
not exist and everything else builds.

```sh
cmake --build build --target esajpip_client_wasm
```

`build/client/web/` then holds the module, the JavaScript files and the
demonstration page.

## Demonstration page

```sh
python3 -m http.server -d build/client/web 8000
```

With the server on its default port and `movie.jpx` in its image directory:
<http://localhost:8000/?server=http://localhost:8900&image=movie.jpx>.

The page is an example of `source.mjs`. It opens at the lowest resolution,
with a button per `reduce`; for a JPX, a frame slider and Play (10 frames a
second at most). The strip under the slider is dark for frames cached at the
`reduce` shown and pale for frames cached at a greater one. "Fetch ahead"
fetches the remaining frames at the `reduce` shown, one at a time, from the
frame after the current one. The page draws on a 2D canvas and applies the
color table itself.

## hv_jpp2j2k

Writes a frame as a `.j2k` file from response bodies saved with curl. With
`image.jp2` (4096x4096 here) on the server:

```sh
# The whole image at 1024x1024, on a new channel.
curl -s -D headers.txt -o r1.jpp \
  'http://localhost:8900/image.jp2?cnew=http&type=jpp-stream&stream=0&fsiz=1024,1024,closest'
build/client/hv_jpp2j2k -o frame1024.j2k r1.jpp
opj_decompress -i frame1024.j2k -o frame1024.pgm -r 2

# Then at full size on the same channel: only what is missing is sent.
cid=$(sed -n 's/^JPIP-cnew: cid=\([0-9a-f]*\).*/\1/p' headers.txt)
curl -s -o r2.jpp "http://localhost:8900/jpip?cid=$cid&stream=0&fsiz=4096,4096,closest"
build/client/hv_jpp2j2k -o frame4096.j2k r1.jpp r2.jpp
opj_decompress -i frame4096.j2k -o frame4096.pgm
curl -s "http://localhost:8900/jpip?cid=$cid&cclose=$cid"
```

The decoded pixels equal those of `image.jp2` decoded with the same options.
For a frame of a JPX, `-c` names the codestream:

```sh
curl -s -o f3.jpp \
  'http://localhost:8900/movie.jpx?cnew=http&type=jpp-stream&stream=3&fsiz=1024,1024,closest'
build/client/hv_jpp2j2k -c 3 -o frame3.j2k f3.jpp
```

## Source files

| File | Responsibility |
| --- | --- |
| `hv_jpp.c` | JPP-stream messages (T.808 A.2 and D.3) |
| `hv_cache.c` | The data-bins received on one channel |
| `hv_reconstruct.c` | One codestream of the store as a JPEG 2000 codestream; its complete resolution levels |
| `hv_metadata.c` | Codestream count, XML and color table, from the metadata bins |
| `hv_image.c` | Decoding to 8-bit pixels, with OpenJPEG |
| `hv_jpp2j2k.c` | The command-line tool |
| `hv_wasm.c` | The WebAssembly module's entry points |
| `js/source.mjs` | `JpipSource` |
| `js/worker.mjs` | The Web Worker of a source |
| `js/jpip.mjs` | `JpipChannel`: the HTTP exchange around the module |
| `demo/index.html` | The demonstration page |
| [`vendor/openjpeg/`](vendor/openjpeg/README.md) | OpenJPEG 2.5.4 |

`hv_reconstruct` writes the main header, one tile-part and the precinct bins
in identifier order, which is RPCL, with an empty packet per layer for each
precinct the store has nothing of. It parses no packet. A window smaller than
the image would decode wrongly near its edges, as the server sends no margin
for the wavelet filters.

`hv_jpp.c` and `hv_cache.c` come from the `codex/wasm-client` branch, with
four changes: they use the C library, their errors are constant strings, the
data-bin classes are renamed `HV_BIN_*`, and the store is a hash table without
a fixed limit.

## Tests

```sh
ctest --test-dir build -L client --output-on-failure
```

They are in `../tests/client/` and need no network. `client_reconstruct`
serves every codestream with `jpip::DataBinServer`, at each of its resolutions
on one channel. It requires the written codestream to be the file's header and
packets in RPCL order, and exactly the resolutions delivered to be complete in
the store. For the transcoder's reference images and the merger's reference
movie (a JPX of eight frames of several sizes) it also requires the decoded
pixels of the file's own codestream, and for the movie its frame count and
each frame's XML and color table on one channel. It also reads color tables
from made-up header boxes.

`client_image` encodes known samples at several precisions and verifies both
sample scaling and unchanged palette indices. The JavaScript integration
checks need Node.js, a built module and a running local server:

```sh
node tests/client/check.mjs build/client/web/esajpip_client.wasm \
  http://localhost:8900 movie.jpx
```

The runner checks argument validation, error types, cache reuse and closure
through the actual channel and worker, using a Node worker transport. It also
decodes the first frame at every resolution and the remaining frames at their
lowest resolution, checking fetch-ahead. For each decoded frame it prints the
dimensions, received bytes, pixel checksum, XML size and color-table entries.
The checksums are diagnostic output, not comparisons with reference pixels.

For lossy images the pixels of the module and of a native build can differ by
1: the floating-point wavelet is rounded differently.
