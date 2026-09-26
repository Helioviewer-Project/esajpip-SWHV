# transcode

`hv_transcode`, the C port of hvJP2K's transcoder (`jp2_transcode.py`,
`jp2_precincts.py`, `jp2_packets.pyx`). Like

    kdu_transcode Corder=RPCL ORGgen_plt=yes Cprecincts={128,128}

it rewrites the codestream in RPCL order with the given precincts and PLT
markers, without recompressing it: every code-block keeps its coding passes,
bytes and zero bit-planes, and only the packet headers change. It reads and
writes headers, and lays out precincts and packets, with `jpeg2000_io`
(`../lib/`: `hv_reader` with the shared rules of `hv_rules`, `hv_writer`,
`hv_geometry`).

## Usage

    hv_transcode [-x] [-p W,H] input output

- `-p W,H`: precinct width and height, powers of 2 from 2 to 32768; default
  `128,128`.
- `-x`: rewrite the first top-level XML box as its root element alone, as
  hvJP2K's `--xml-rewrite` does (see below).

The input is mapped into memory. The file keeps its boxes, in order: the
`jp2c` box is transcoded, the others are copied as read. The output is
written to a temporary file next to it with the input's permissions
(without the setuid, setgid and sticky bits) and renamed into place, so
input and output may be the same file; an output that is a symbolic link is
written where the link points. On error nothing is written. Superboxes
nested more than `HV_BOX_DEPTH_MAX` (32) deep are refused. Exit status: 0, 1 on error, 2 on
usage errors.

## Supported input

The output is for the JPIP server, so the input must be a JP2 file within
the server's profile (`../JPIP_PROFILE.md`) except for its tile-parts,
which are rewritten. The reader checks this with the rules shared with the
model (`../lib/`): the file rules of `hv_check_jp2` (signature, file type
with the `jp2 ` brand and compatibility entry, exactly one `jp2c`, at most
`INT_MAX` bytes; JPX files and raw codestreams fail) and the main-header
rules of `HV_PROFILE_HEADERS` (zero origins, unit sampling, one tile,
dimensions up to `INT32_MAX`, no COC, POC or PPM). The header boxes, which
the output keeps as read, must pass `hv_check_jp2h` (T.800 I.5.3: one
`jp2h` before the codestream, its boxes well formed and in order, and
`ihdr` and `bpcc` agreeing with SIZ). What else the profile asks for, the
transcoder writes: the tile-parts and a COD without SOP.
Errors from the shared rules name the rule, as in `siz.zero-origin`. The
output is at most `INT_MAX` bytes and passes the whole profile
(`HV_PROFILE`): the tests check every file they transcode, and the fuzz
target every codestream whose main header is within the profile.

Within that, as in hvJP2K: any number of tile-parts (Psot = 0 allowed on
the last one), any progression order, only PLT and COM in tile-part
headers, and code-block styles without selective arithmetic coding bypass
or termination on each coding pass. RGN, which hvJP2K rejects, is kept:
it changes neither the packets nor their headers. The new precincts must
keep the code-block partition. SOP and EPH markers are checked and not
written; the input's PLT is checked as the reader does (Zplt order,
lengths adding up to each tile-part's data, zero entries only after the
last packet of a tile-part) but not used; TLM and PLM are dropped; every
other main-header segment, COM and RGN included, is copied as read.
Tile-part COM is dropped with the tile-part headers.

`hv_transcode_codestream`, the codestream level that the tests and the
fuzz target also use, does not apply the profile: it takes nonzero origins
and sub-sampled components too, and rejects COC, POC and PPM on its own.

Three bounds keep memory in check where a header can declare much more
than its data holds: at most 65,536 component-resolutions (components
times resolutions, 360 bytes of layout each, checked before allocating),
at most 250,000 code-blocks (under 100 bytes of state each) and at most
2,000,000 packets in the output (about 20 bytes each, plus at least a byte
of output). The input's packets need no bound of their own: each takes at
least a byte of its tile's data.

Packet headers are read as T.800 requires, where hvJP2K is lenient: an
SOP marker segment must have Lsop = 4 and Nsop equal to the packet's index
in the tile, modulo 65,536 (A.8.1); when COD signals EPH, every packet
header must end with it (A.8.2); and the byte after each 0xFF in a header
must have its top bit clear (B.10.1). hvJP2K skips six bytes after any SOP
code, skips EPH where it finds it, and ignores the stuffed bit. None of the
4,014 EUI files, 200 AIA files from JSOC (10 wavelengths, 2015 to 2026),
hvJP2K fixtures or test corpus files breaks these rules.

Packet-level errors otherwise use hvJP2K's messages, except for the rules
above, the memory bounds, and one case: hvJP2K reads a tile's tile-parts
as one concatenated block, `hv_transcode` reads each in place. A packet in
a tile-part other than the last that runs past its tile-part is reported
as "packet crosses a tile-part boundary" even when the data after it would
also run out, where hvJP2K reports "packet data overruns the tile". Header
errors come from the reader and use its messages.

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
| `hv_transcode.c` | The command: options, mapping the input, writing the output file. |
| `transcode_file.c` | `hv_transcode_file`: boxes (the codestream is transcoded straight into its `jp2c` box) and `-x`. |
| `transcode.h` / `.c` | `hv_transcode_codestream` (`transcode_codestream`): reads the codestream and writes its main header with the new COD, lays out the tile twice with `hv_geometry` (input and new precincts), checks the memory limits and the code-block partition, and writes the tile as one tile-part. |
| `tier2.h` / `.c` | Packets (T.800 B.9, B.10) on an `hv_geometry` and its packet order. `hv_read_packets` decodes the headers in place, tile-part by tile-part, and records each code-block's contributions per layer; `hv_write_packets` encodes them, either for the lengths only (for the PLT) or into the output, copying the code-block bytes from the input. |
| `fuzz_transcode.c` | libFuzzer target for the codestream level: an accepted input's output must transcode to itself, and pass `HV_PROFILE` when its main header passes `HV_PROFILE_HEADERS`. |

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

Tests, separate from the server's (`test/`):

```sh
lib/test/run.sh             # or: lib/test/run.sh sanitize
TRANSCODE_ARCHIVE=~/AIA:~/EUI lib/test/run.sh
```

The build goes to `build/tool-tests-<mode>`, or to
`ESAJPIP_TEST_BUILD_DIR`, which `../tests/run.sh` also reads: set it for
one runner at a time.

`test_transcode` checks that every file in `test/fixtures/input/`
transcodes, with `-x`, to its Kakadu reference in `test/fixtures/kakadu/`
(COM aside), that each output is within the served profile and transcodes
to itself (the origin-129 file is rejected, and only its codestream is
compared); what the profile accepts and rejects; the boxes around the
codestream (LBox = 0 on a superbox and its child, a malformed child, `-x`
on malformed XML); tile-parts split
every way T.800 allows and broken in the ways it does not; malformed main
headers; 2,000 corrupted tiles, each rejected or giving a stable output;
the SOP, EPH and bit-stuffing rules; and the memory bounds. `cli_test.sh`
checks the command: options, exit status, in-place replacement keeping the
mode, and nothing written or left behind on failure. With
`TRANSCODE_ARCHIVE` set to directories, every `.jp2` file in them is
transcoded and checked for a stable output too. The fixtures are described
in `test/fixtures/FIXTURES.md`. The same run includes the reader's tests
(`../lib/test/`).
