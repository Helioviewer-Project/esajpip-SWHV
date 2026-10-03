# client

A JPIP client for the JP2 images and JPX movies that esajpip serves. Given a
frame number and a resolution, it fetches what it does not have yet, decodes
the frame, and returns 8-bit pixels. It also gives each frame's XML metadata
and color table. Data received once is kept, so showing a frame again, or
smaller, makes no request.

| Part | Use it for | Start at |
| --- | --- | --- |
| JavaScript module | A web application | [JavaScript](#javascript) |
| C library | A native program that makes its own HTTP requests | [C](#c) |
| Demonstration page | Seeing it work against your server | [Quick start](#quick-start) |
| `hv_jpp2j2k` | Turning responses saved with curl into a `.j2k` file | [hv_jpp2j2k](#hv_jpp2j2k) |

The C library is the client. The JavaScript module is the same code built for
WebAssembly, with the HTTP exchange and a Web Worker around it.

## Quick start

With the project configured in `build`, the server running on its default
port, and a `movie.jpx` in its image directory
([`../README.md`](../README.md)):

```sh
cmake --build build --target esajpip_client_wasm
python3 -m http.server -d build/client/web 8000
```

Then open
<http://localhost:8000/?server=http://localhost:8900&image=movie.jpx>.

The first command needs [zig](https://ziglang.org/) (`brew install zig`): its
C compiler builds the WebAssembly module, and carries the C library OpenJPEG
needs. Without zig the target does not exist; the rest of the project builds
as usual. `build/client/web/` receives everything a page needs:
`esajpip_client.wasm`, `jpip_source.mjs`, `jpip_worker.mjs`,
`jpip_channel.mjs`, and the demonstration page as `index.html`.

On the page:

- The image opens at its lowest resolution. There is a button for each
  resolution, up to `Full size`.
- For a movie, the slider chooses the frame and Play steps through the frames,
  at most 10 a second. While a frame is on its way, only the last slider
  position is kept.
- The strip under the slider shows the cache: dark for frames cached at the
  resolution shown, pale for frames cached only at a lower one.
- "Fetch ahead" fetches the other frames at the resolution shown, one at a
  time, starting after the current frame.
- "XML metadata" shows the XML of the current frame. The status line shows the
  bytes received so far.

The page is an example of the JavaScript module and nothing more. It draws on
a 2D canvas, so it converts the pixels to RGBA and applies the color table
itself.

## Terms

- **Frame.** A codestream of the file, numbered from 0. A JP2 has one. A JPX
  has one per image, whether the images are embedded or linked. The frames of
  one JPX may differ in size, components, resolution levels and color table,
  so each frame reports its own.
- **`reduce`.** The number of highest resolution levels left out, as in
  `opj_decompress -r` and `kdu_expand -reduce`. The size is the full size
  divided by 2^`reduce`, rounded up: a 4096x4096 frame is 1024x1024 at
  `reduce` 2. A value beyond what the frame has gives its lowest resolution.
- **Cached.** A resolution level of a frame is cached once all its data has
  arrived. A request brings only the levels that are not.
- **Channel.** The server's record of what one client has received for one
  image. The server ends a channel left idle for `connections.timeout`, 60
  seconds by default; see [Failures](#failures).

## JavaScript

### Setup

Put `jpip_source.mjs`, `jpip_worker.mjs`, `jpip_channel.mjs` and
`esajpip_client.wasm` in one directory of the site, and import
`jpip_source.mjs`. The page may be on another origin than the server, which
sends `Access-Control-Allow-Origin: *`.

### Showing a frame

```js
import { JpipSource } from "./jpip_source.mjs";

const source = await JpipSource.open({
    wasm: new URL("esajpip_client.wasm", import.meta.url),
    server: "http://localhost:8900",
    image: "movie.jpx" });               // a path below the image directory

const frame = await source.frame(3, 1);  // frame 3 at half size

// One byte per pixel: rows are not padded to 4 bytes.
gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
gl.texImage2D(gl.TEXTURE_2D, 0, gl.R8, frame.width, frame.height, 0,
              gl.RED, gl.UNSIGNED_BYTE, frame.pixels);

await source.close();
```

`frame` resolves to:

| Field | Meaning |
| --- | --- |
| `index` | The frame number |
| `reduce` | The `reduce` used: the one asked for, or the highest the frame has |
| `width`, `height` | The size of the decoded image |
| `components` | 1 or 3 |
| `pixels` | A `Uint8Array` of `width * height * components` bytes, top row first, components interleaved. It is a copy that belongs to the caller |
| `fullWidth`, `fullHeight` | The size at `reduce` 0 |
| `resolutions` | The number of resolution levels: `reduce` runs from 0 to `resolutions - 1` |

`reduce` is a nonnegative integer or `Infinity`, which means the lowest
resolution. `source.frames` is the number of frames.

### API flow

```text
Application                         Client
-----------                         ------
JpipSource.open(...)  ------------>  Open channel; receive metadata
                                    and frame 0's lowest resolution
                     <------------  Return source

source.frame(index, reduce)  ------>  Obtain frame header if missing
                                    Prepare geometry/header once
                                    Fetch missing resolution levels
                                    Reconstruct codestream and decode
                     <------------  Return pixels and frame dimensions

Upload pixels and display
Repeat frame(...) for another frame or resolution

source.close()  ------------------>  Close channel; release cache and worker
```

The application currently chooses `reduce`. The client clamps it to the
frame's available levels. A frame whose data is already cached needs no
request, but each `frame` call decodes again. The header-only request is
needed only when that frame's main header has not arrived.

### What the pixels are

| Frame | `components` | `pixels` |
| --- | --- | --- |
| One or two components, no color table | 1 | The first component as gray, scaled to 8 bits |
| Three or more components | 3 | The first three components as RGB, each scaled to 8 bits |
| With a color table | 1 | Indices into the table, unchanged |

Scaling keeps the 8 most significant bits of deeper samples, and shifts
shallower ones up. Signed samples are offset to unsigned first.

A frame with a color table is not converted to color. `palette` gives the
table, and the application applies it, typically as a lookup texture:

```js
const lut = await source.palette(3);     // null if the frame has none
if (lut !== null && lut.channels === 3)
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGB8, lut.entries, 1, 0,
                  gl.RGB, gl.UNSIGNED_BYTE, lut.table);
```

- `table` is a `Uint8Array` of `entries * channels` bytes: for each index from
  0 to `entries - 1`, its `channels` values, scaled to 8 bits. `channels` is 3
  for red, green, blue. It is a copy that belongs to the caller.
- An index can be larger than `entries - 1` when the table has fewer than 256
  entries. The demonstration page uses the last entry for those.
- Indices are supported for a frame of one unsigned component of at most 8
  bits. Any other frame with a color table fails to decode.

### XML

```js
const xml = await source.xml(3);         // a string, or null
```

This is the XML that describes the frame; for a FITS image, its header. In a
JPX it is found as `hv_merge` and hvJP2K write it: the `xml` box of the
association box (`asoc`) whose number list (`nlst`) names the frame. In a JP2
it is the first top-level `xml` box.

The XML and the color tables of all frames arrive when the source is opened,
so `xml` and `palette` never make a request.

### Playing a movie

`frame` fetches what is missing before it decodes, so a movie played frame by
frame waits for the server at every new frame. `fetch` gets a frame's data
without decoding it:

```js
for (let index = 0; index < source.frames; index++)
    await source.fetch(index, reduce);
```

After that, `frame(index, reduce)` decodes from the cache.

- **One at a time.** `frame`, `fetch` and `close` run in the order they were
  called, one after the other. With one `fetch` pending, as in the loop above,
  a `frame` call made meanwhile waits for one request at most. A hundred
  `fetch` calls made at once would all run before it.
- **Progress.** `fetch` resolves to the lowest `reduce` now cached for that
  frame. `cached(index)` gives the same value at any time, or `null` when no
  level is cached; it does not wait for requests under way.
- **Other resolutions.** A frame cached at `reduce` 1 also serves `reduce` 2
  and above. Fetching it at `reduce` 0 later brings the one missing level.
- **Decoding.** Every `frame` call decodes. Decoded frames are not kept: keep
  the textures of frames shown repeatedly.
- **First request for a frame.** If its main header has not arrived, a
  header-only request (`stream=index&layers=0`) obtains its size and resolution
  levels before requesting pixels. This adds one round trip on the first
  visit, and makes the request exact even when movie frames differ in size.

### Several images

A source is one channel and one Web Worker: fetching and decoding run off the
page's thread, and sources do not wait for each other. Open one per image or
movie on screen. The module is downloaded and compiled once per page, whatever
the number of sources.

### Failures

A call that fails rejects its promise with an `Error` whose message says why.

| Failure | What happens | What to do |
| --- | --- | --- |
| `index` is not a frame, or `reduce` is not valid | `RangeError`; no request is made | Fix the call |
| A request fails: the server is unreachable, answers with an error, or has ended the channel | The call rejects. Every later call that needs a request rejects with the same error. Calls served from the cache still succeed | Close the source and open the image again |
| "the server did not send frame N whole" | The call rejects; the source is unaffected. A response ended without the data asked for, which this server does not do | |
| A call after `close()` | Rejects with "the source is closed" | |

The idle timeout is the failure to plan for. A source that makes no request
for `connections.timeout` (60 seconds by default) has lost its channel, and
its next request fails with `503`. What it cached still decodes, but it cannot
fetch more, and a new source starts with an empty cache. An application that
will need more of a movie later should fetch it while the source is active.

### Memory

A source keeps every byte it receives until `close()`: the server does not
send data twice on a channel, and takes no note of a client that drops some. A
movie fetched whole at full size therefore takes about the size of its
codestreams, plus the tables that index them. Decoding adds, for its duration,
a copy of the frame's codestream and OpenJPEG's working memory. The module
keeps the last decoded image until the next decode; the page gets its own
copy. A frame's parsed geometry and prepared reconstruction header are also
kept once first used, until the source closes.

### Reference

| `JpipSource` | Result | Requests |
| --- | --- | --- |
| `JpipSource.open({ wasm, server, image })` | A source. `wasm` is the URL of the module, `server` the server's address, `image` a path below its image directory | 1 |
| `frames` | The number of frames | |
| `received` | Bytes of response bodies so far, as of the last `frame` or `fetch` | |
| `frame(index, reduce = 0)` | The decoded frame | For the levels not cached |
| `fetch(index, reduce = 0)` | `cached(index)` after fetching | For the levels not cached |
| `cached(index)` | The lowest `reduce` that needs no request, or `null` | 0 |
| `xml(index)` | A string, or `null` | 0 |
| `palette(index)` | `{ entries, channels, table }`, or `null` | 0 |
| `close()` | Closes the channel, frees the cache and ends the worker | 1 |

All but `frames` and `received` return promises.

The first request brings the metadata of all frames and the lowest resolution
of frame 0.

### Without a worker

`jpip_channel.mjs` exports `JpipChannel`, which is what the worker runs. Use
it directly where the code is already off the main thread, or under Node.js:

```js
import { JpipChannel } from "./jpip_channel.mjs";

const channel = await JpipChannel.open(wasm, server, image);
```

`wasm` is a compiled `WebAssembly.Module`, the module's bytes, or its URL. The
methods are those of `JpipSource`, except that `cached`, `xml` and `palette`
return their result directly, not a promise.

## C

`esajpip_client` is a static library; its headers are in `client/`. Inside
this build, `target_link_libraries(app PRIVATE esajpip_client)` is enough. The
library does no I/O: the program sends the requests and passes each response
body to the library.

### What the program does

1. Prepare a store with `hv_cache_begin`. A store belongs to one channel.
2. Send the first request and read the channel from its response.
3. Pass every response body through `hv_jpp_next` into `hv_cache_apply`.
4. After the first response, index the metadata with `hv_metadata_open`. That
   gives the number of frames, and each frame's XML and color table.
5. For a frame, ask `hv_reconstruct_status` what is cached. If the level
   wanted is not, request it and go back to step 3.
6. Write the frame as a codestream with `hv_reconstruct`, and decode it with
   `hv_image_decode` or another decoder.
7. At the end, close the channel on the server, then `hv_metadata_close` and
   `hv_cache_release`.

### Requests

One channel per image, one request at a time on it.

```
GET /movie.jpx?cnew=http&type=jpp-stream&stream=3&fsiz=1024,1024,closest
GET /<path>?cid=<cid>&stream=3&fsiz=2048,2048,closest
GET /<path>?cid=<cid>&cclose=<cid>
```

- The first request names the image and asks for a channel. Its response has
  the header `JPIP-cnew`, whose `cid` and `path` fields the later requests
  use.
- `stream` is the frame. `fsiz` is its size at the `reduce` wanted: the full
  size divided by 2^`reduce`, rounded up.
- The first response of a channel also carries the metadata of all frames,
  whatever it asks for.
- A frame's size and resolution levels are known once its main header has
  arrived. Before that, request `stream=<frame>&layers=0`, then use
  `hv_reconstruct_status` to size the pixel request.
- Do not use `len` or nonzero `layers` limits for pixel requests: a response
  cut short leaves partial data that `hv_reconstruct` refuses. `layers=0` is
  used only to obtain headers.
- [`../JPIP_PROFILE.md`](../JPIP_PROFILE.md) describes the requests the server
  accepts, and [`../CHANNELS.md`](../CHANNELS.md) its channels, status codes
  and timeouts.

### Calls

| Call | Header | Result |
| --- | --- | --- |
| `hv_cache_begin`, `hv_cache_release` | `hv_cache.h` | The store of one channel |
| `hv_jpp_begin`, `hv_jpp_next`, `hv_jpp_reason` | `hv_jpp.h` | The messages of a response body, and why the response ended |
| `hv_cache_apply` | `hv_cache.h` | A message added to the store |
| `hv_metadata_open`, `hv_metadata_close` | `hv_metadata.h` | An index of the metadata; its `count` is the number of frames |
| `hv_metadata_xml` | `hv_metadata.h` | A frame's XML, in place in the store |
| `hv_metadata_palette` | `hv_metadata.h` | A frame's color table, copied out |
| `hv_reconstruct_status` | `hv_reconstruct.h` | A frame's size, components and resolution levels, and how many levels are cached |
| `hv_reconstruct` | `hv_reconstruct.h` | A frame as a JPEG 2000 codestream |
| `hv_image_decode` | `hv_image.h` | A codestream decoded to pixels |

Each header documents its calls' results. Calls that take `error` and
`error_size` write a message there when they fail.

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

/* The metadata, after the first response. */
char error[256];
hv_metadata metadata = {0};
if (hv_metadata_open(&cache, &metadata, error, sizeof error) != 0)
    return fail(error);
/* metadata.count frames. */

const uint8_t *xml;                     /* NULL if the frame has none */
size_t xml_size;
if (hv_metadata_xml(&metadata, frame, &xml, &xml_size,
                    error, sizeof error) != 0)
    return fail(error);

uint8_t *table = malloc(HV_PALETTE_MAX);
int channels;                           /* entries is 0 if it has none */
int entries = hv_metadata_palette(&metadata, frame, &channels, table,
                                  HV_PALETTE_MAX, error, sizeof error);
if (entries < 0)
    return fail(error);

/* A frame at its highest cached resolution level. */
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
/* image.pixels: image.width * image.height * image.components bytes. */

free(image.pixels);
free(codestream);
free(table);
hv_metadata_close(&metadata);
hv_cache_release(&cache);
```

### Notes

- **`hv_status`.** `complete` counts cached resolution levels from the lowest,
  so the lowest usable `reduce` is `resolutions - complete`, and 0 means
  nothing can be decoded yet. All fields are 0 until the frame's main header
  has arrived. The first call prepares the geometry and reconstruction
  header; later calls reuse them and look the precinct bins up. It reads no
  packet and decodes nothing.
- **`hv_reconstruct`.** Called with no buffer, it returns the size; called
  with one, it writes. The codestream has empty packets above the cached
  levels, so any decoder reads it, provided it is told to leave out
  `resolutions - complete` levels. A frame with a partly received precinct is
  refused.
- **`hv_image_decode`.** `HV_IMAGE_SAMPLES` scales samples to 8 bits.
  `HV_IMAGE_INDICES` keeps them as they are, for a frame with a color table;
  it accepts one unsigned component of at most 8 bits. See [What the pixels
  are](#what-the-pixels-are).
- **`hv_metadata`.** Start with `{0}`. `hv_metadata_open` fails while the
  metadata is incomplete, which it is not after a whole first response. The
  index points into the store: XML pointers stay valid until
  `hv_cache_release`, and the index must be closed before the store is
  released.
- **Color tables.** `HV_PALETTE_MAX` bytes hold any table. A call with
  capacity 0 returns `entries` and sets `channels` without writing.
- **Errors in a response.** When `hv_jpp_next` or `hv_cache_apply` fails, the
  store no longer matches the server's record of the channel. Close the
  channel. What is cached still decodes.
- **Threads.** A store has no locking: use it from one thread at a time.
- **Pixels.** `image.pixels` is allocated with `malloc`; the caller frees it.

`hv_wasm.c` and `js/jpip_channel.mjs` together are such a program, and the
shortest complete example.

## Limits

- **Whole frames only.** No regions, and no display of a frame that has partly
  arrived.
- **No eviction.** The cache grows until the channel is closed.
- **No recovery of a channel.** Once a channel is lost, its cache cannot be
  used with a new one.
- **This server's files.** The client reads what esajpip sends for the files
  it accepts ([`../JPIP_PROFILE.md`](../JPIP_PROFILE.md)): one tile, no
  component subsampling. It is not a general JPIP client.
- **Lossy images.** Pixels from the WebAssembly module and from a native build
  can differ by 1, as the two round the floating-point wavelet differently.

## hv_jpp2j2k

```
hv_jpp2j2k [-c codestream] -o file.j2k response [response ...]
```

Writes a frame as a `.j2k` file from the response bodies of one channel, in
the order received. With `image.jp2` (4096x4096 here) on the server:

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

`-r 2` leaves out the two resolution levels that were not requested. The
decoded pixels equal those of `image.jp2` decoded with the same options.

For a frame of a JPX, `-c` names it:

```sh
curl -s -o f3.jpp \
  'http://localhost:8900/movie.jpx?cnew=http&type=jpp-stream&stream=3&fsiz=1024,1024,closest'
build/client/hv_jpp2j2k -c 3 -o frame3.j2k f3.jpp
```

The tool prints the number of messages of each response and why it ended. It
exits with 0 on success, 1 on an error and 2 on a usage error.

## Tests

```sh
ctest --test-dir build -L client --output-on-failure
```

The tests are in `../tests/client/` and need no network.

| Test | What it checks |
| --- | --- |
| `client_cache` | The store: appending, completion, refusals, many bins |
| `client_jpp` | Messages written by the server's own writer, read back |
| `client_jpp_malformed` | Damaged messages, all refused |
| `client_reconstruct` | Every codestream of the corpus, the transcoder's reference images and the merger's reference movie, served by the server's own code at each resolution: the written codestream, the cached levels, the decoded pixels, and the movie's frame count, XML and color tables |
| `client_image` | Sample scaling at several precisions, and unchanged color table indices |

The JavaScript is checked by a script that needs Node.js, the built module and
a running server:

```sh
node tests/client/check.mjs build/client/web/esajpip_client.wasm \
  http://localhost:8900 movie.jpx
```

It checks argument validation, error types, cache reuse and closing, through
`JpipChannel` and through `JpipSource` with its worker. It decodes the first
frame at every resolution and the others at their lowest, and prints for each
the size, the bytes received, a checksum of the pixels, and the sizes of the
XML and the color table. The checksums are for comparing runs, not checked
against a reference.

## How it works

The server delivers a codestream as data-bins: one for the main header, and
one per precinct holding that precinct's packets. The client keeps them in a
hash table (`hv_cache.c`). To decode a frame it writes them back as a
codestream (`hv_reconstruct.c`): the main header, one tile-part, and the
precinct bins in the order of their identifiers, which is resolution,
position, component (RPCL). A precinct with no data gets one empty packet per
layer. No packet is parsed.

The file's boxes arrive as metadata bins (`hv_metadata.c`): bin 0 holds the
top-level boxes, with a placeholder for each codestream and, in a JPX, for
each association box, whose contents are a bin of their own. A color table is
the palette box (`pclr`) that the component mapping (`cmap`) applies, from the
JP2 Header box or, in a JPX, from the frame's own codestream header box
(`jpch`) where that has one.

| File | Responsibility |
| --- | --- |
| `hv_jpp.c` | JPP-stream messages (T.808 A.2 and D.3) |
| `hv_cache.c` | The data-bins received on one channel |
| `hv_frame.c` | Geometry and reconstruction header prepared once per frame, owned by its main-header bin |
| `hv_reconstruct.c` | One codestream of the store as a JPEG 2000 codestream; its cached resolution levels |
| `hv_metadata.c` | Frame count, XML and color tables, from the metadata bins |
| `hv_image.c` | Decoding to 8-bit pixels, with OpenJPEG |
| `hv_jpp2j2k.c` | The command-line tool |
| `hv_wasm.c` | The WebAssembly module's entry points |
| `js/jpip_source.mjs` | `JpipSource` |
| `js/jpip_worker.mjs` | The Web Worker of a source |
| `js/jpip_channel.mjs` | `JpipChannel`: the HTTP exchange around the module |
| `demo/index.html` | The demonstration page |
| [`vendor/openjpeg/`](vendor/openjpeg/README.md) | OpenJPEG 2.5.4 |
