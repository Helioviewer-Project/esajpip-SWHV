# transcode

`hv_transcode`, the C port of hvJP2K's transcoder (`jp2_transcode.py`,
`jp2_precincts.py`, `jp2_packets.pyx`). Like

    kdu_transcode Corder=RPCL ORGgen_plt=yes Cprecincts={128,128}

it rewrites the codestream in RPCL order with the given precincts and PLT
markers, without recompressing it: every code-block keeps its coding passes,
bytes and zero bit-planes, and only the packet headers change. It reads and
writes headers with `jpeg2000_io` (`../lib/`).

## Usage

    hv_transcode [-x] [-p W,H] input output

- `-p W,H`: precinct width and height, powers of 2 from 2 to 32768; default
  `128,128`.
- `-x`: rewrite the first top-level XML box as its root element alone, as
  hvJP2K's `--xml-rewrite` does (see below).

The input is mapped into memory. A JP2/JPX file keeps its boxes: the first
top-level `jp2c` box is transcoded, the others are copied as read (a box
with LBox = 0 gets an explicit length). A raw codestream (starting with
SOC) is transcoded as such. The output is written to a temporary file next
to it with the input's mode and renamed into place, so input and output may
be the same file; on error nothing is written. Exit status: 0, 1 on error,
2 on usage errors.

## Supported input

As in hvJP2K: one tile (any number of tile-parts, Psot = 0 allowed on the
last one), any progression order, no COC, POC, PPM or RGN, only PLT and COM
in tile-part headers, and code-block styles without selective arithmetic
coding bypass or termination on each coding pass. The new precincts must
keep the code-block partition. SOP and EPH markers are skipped and not
written; TLM and PLM are dropped; the input's PLT is ignored (so its
trailing zero entries are accepted); every other main-header segment,
COM included, is copied as read.

Packet-level errors use the same messages as hvJP2K. Header errors come
from the reader and use its messages.

## XML (-x)

hvJP2K's `--xml-rewrite` goes through glymur, which reserializes the XML
with lxml (`ET.tostring(root, encoding="utf-8")`): the XML declaration and
anything outside the root element are dropped. `-x` does exactly that and
copies the root element as read. It does not redo lxml's reserialization of
the element itself (attribute quoting, character references, namespace
declarations), so the two can differ for XML that lxml would rewrite.

## Files

| File | Role |
| --- | --- |
| `hv_transcode.c` | The command: options, mapping, boxes, `-x`, output file. |
| `transcode.h` / `.c` | `hv_transcode_codestream`: precinct geometry, progression order and the new codestream (`_geometry`, `_packet_order`, `_flatten`, `transcode_codestream`). |
| `tier2.h` / `.c` | Packet headers: `hv_read_packets` and `hv_write_packets` (`read_packets`, `write_packets`). |
| `fuzz_transcode.c` | libFuzzer target: an accepted input's output must transcode to itself. |

## Build and test

With the rest of the repository:

```sh
cmake -S . -B build [-DESAJPIP_SANITIZE=ON]
cmake --build build --target hv_transcode
```

Fuzzing (Clang):

```sh
cmake -S . -B fuzz -DCMAKE_C_COMPILER=clang -DESAJPIP_SANITIZE=ON -DESAJPIP_FUZZ=ON
cmake --build fuzz --target fuzz_transcode
fuzz/transcode/fuzz_transcode -max_len=131072 corpus/
```

Against hvJP2K's fixtures (`hvJP2K/jp2/test/transcode`): with `-x`, every
file in `orig/` and `sop_eph/` gives its `trans/` file byte for byte, except COM
(the input's comment is kept, while Kakadu writes its own). hvJP2K's `integrity.py` cases are all rejected; packet-level
messages match.
