# lib

The JPEG 2000 reader/writer, built by the repository's CMake as the static
library `jpeg2000_io`. `hv_transcode` (in `../transcode/`) uses it; the
server may later. The header types come from the ASN.1/ACN model in
`../spec/`; the code here steps from header to header and records where each
payload starts and ends.

## Layout

| File | Role |
| --- | --- |
| `hv_reader.h` / `.c` | Steps through a file in memory: boxes (T.800 I.4) and codestream items (Annex A). Checks framing and decodes marker segments with the generated code. Never copies a payload. |
| `hv_writer.h` / `.c` | Writes box headers, SIZ, COD, QCD, COM, PLT, SOT and opaque segments with the generated encoders into a growing buffer. Fills in Psot and LBox/XLBox when a tile-part or box ends, and splits PLT the way Kakadu does (as many whole entries as fit in Lplt = 65 535). |
| `hv_mapping.c` | The one ACN mapping function the generated code calls (`lxxx`: Lxxx counts itself). |
| `hv_walk.c` | `hv_walk [-v] [-p] [-w] file...`: checks files with the reader and prints one line per file; `-v` lists every box and codestream item, `-p` accepts trailing zero PLT entries, `-w` rewrites the whole file with the writer from what the reader decoded and compares it with the input. |
| `generated/` | Code generated from `../spec/` by `generate.sh`: only the types in `../spec/jpeg2000-io.asn1` and what they use. |
| `generate.sh` | Regenerates `generated/` with the pinned asn1scc (Docker image, or `ASN1SCC=...`). |
| `CMakeLists.txt` | The `jpeg2000_io` library and the `hv_walk` tool, added by the top-level `CMakeLists.txt`. |

## Build

With the rest of the repository:

```sh
cmake -S . -B build [-DESAJPIP_SANITIZE=ON]
cmake --build build --target hv_walk
```

After a model change in `../spec/`, run `lib/generate.sh`. It needs the
compiler that `../spec/build-asn1scc.sh` builds: the pinned revision with the
local patches in `../spec/asn1scc-patches/`. The unpatched compiler generates
a PLT decoder that reads an uninitialized flag on truncated input.

## What the reader checks

- Boxes: LBox, and XLBox when LBox = 1, within the container; LBox = 0 only
  for the last box, and inside a superbox only if the superbox also runs to
  the end of the file.
- Codestream: SOC then SIZ; exactly one COD and one QCD in the main header;
  marker placement per Table A.1; tile-part order, TPsot and TNsot; Psot, or
  up to the final EOC when Psot = 0; nothing after EOC.
- Bodies, by the generated decoders: SIZ, COD, QCD, PLT and COM at the
  standard's bounds, plus the SIZ and COD cross-field rules listed in
  `../spec/j2k-headers.asn1`, zero packet lengths, and PLT sums against
  the tile-part data.
- `HV_ACCEPT_PLT_PADDING` (`hv_walk -p`) accepts zero PLT entries after the
  last packet of a tile-part, as the server does. T.800 does not allow them,
  but deployed files carry them: every one of 4,014 EUI files checked has
  them.

Not yet: file-level box rules (signature, `ftyp`, JPX requirements), box
bodies other than the box header, and PLT against the packet count, which
needs the packet geometry. Unknown marker codes are reported as items and
skipped by length; the caller decides.
