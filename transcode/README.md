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

    hv_transcode [-p W,H] input output

- `-p W,H`: precinct width and height, powers of 2 from 2 to 32,768; default
  `128,128`.

The input is read into memory (not mapped, so that a file another process
truncates meanwhile cannot crash the tool), and one over INT_MAX bytes is
refused before it is read (`file.size-limit`). The file keeps its boxes, in
order: the `jp2c` box is transcoded, a top-level XML box ends before its
first NUL (see below), the others are copied as read. The output is
written to a temporary file next to it with the input's
permissions (without the setuid, setgid and sticky bits) and renamed into
place (`hv_file`, in `../tools/`: synced to disk before the rename, and
removed if a signal ends the tool first; a replaced file keeps its owner and
group where the process may give them), so input and output may be the same
file; an output that is a symbolic link is written where the link points,
the file it names created if need be. On error nothing is written.
Superboxes nested more than `HV_BOX_DEPTH_MAX` (32) deep are refused. Exit
status: 0, 1 on error, 2 on usage errors.

A linked JPX file (`hv_merge -links`) records where each codestream is in
its JP2 file. Transcoding such a file, in place or not, moves its
codestream: merge the JPX file again from the new files.

## Supported input

The output is for the JPIP server, so the input must be a JP2 file within
the served profile (`../JPIP_PROFILE.md`) except for its tile-parts,
which are rewritten. The reader checks this with the rules shared with the
model (`../lib/`): the file rules of `hv_check_jp2` (signature, file type
with the `jp2 ` brand and compatibility entry, exactly one `jp2c`, at most
`INT_MAX` bytes; JPX files and raw codestreams fail) and the main-header
rules of `HV_PROFILE_HEADERS` (zero origins, unit sampling, one tile,
dimensions up to `INT32_MAX`, no COC, POC or PPM). The header boxes, which
the output keeps as read, must pass `hv_check_jp2h` (T.800 I.5.3: one
`jp2h` before the codestream, its boxes well formed and in order, and
`ihdr` and `bpcc` agreeing with SIZ), after the main header: which must
then be valid at the standard layer too, as the header boxes are checked
against it. What else the profile asks for, the
transcoder writes: the tile-parts and a COD without SOP. Errors from the
reader name the rule where a shared one fails, and the offset in the file,
as in `siz.zero-origin at 410`. The output is at most `INT_MAX` bytes and
passes the whole profile (`HV_PROFILE`): the tests check every file they
transcode, and the fuzz target every codestream whose main header is
within the served profile.

Within that, as in hvJP2K: up to 255 tile-parts, as many as TPsot can number
(Psot = 0 allowed on the last one), any progression order, only PLT and COM
in tile-part headers, and code-block styles without selective arithmetic
coding bypass or termination on each coding pass. RGN, which hvJP2K rejects,
is kept: it changes neither the packets nor their headers. The new precincts
must keep the code-block partition. SOP and EPH markers are checked and not
written; the input's PLT is checked as the reader does (Zplt order, lengths
adding up to each tile-part's data, zero entries only after the last packet
of a tile-part) but not used. One rule (`main_marker` and `tile_marker` in
`transcode.c`) decides every marker, at the file and the codestream level
alike: in the main header SIZ, QCD, QCC, RGN, CRG and COM are copied as
read (COD is replaced), TLM and PLM dropped, and anything else rejected:
COC, POC and PPM, a marker code
T.800 does not define (Table A.2: 0xFF70, say, or an extension of T.801),
which may describe the packets the transcoder rewrites, and 0xFF30 to
0xFF3F, which have no segment and are outside the served profile. In
tile-part headers, PLT and COM are dropped with the headers, and anything
else rejected.

`hv_transcode_codestream`, the codestream level that the tests and the fuzz
target also use, does not apply the profile unless given
`HV_PROFILE_HEADERS` (its only other flag value is 0): it takes nonzero
origins and sub-sampled components too, and keeps, drops and rejects the
same markers, by the rule above. It reads the input with
`HV_ACCEPT_PLT_PADDING`, zero PLT entries accepted at the end of each
tile-part, as the input's PLT is not used.

Three bounds keep memory in check where a header can declare much more
than its data holds: at most 65,536 component-resolutions (components
times resolutions, 360 bytes of layout each, checked before allocating),
at most 250,000 code-blocks (under 100 bytes of state each) and at most
2,000,000 packets in the output (20 bytes each, plus at least a byte of
output, and 64 bytes per precinct: 40 to put the packets in order, 24 to
index its tag trees). The input's packets are bounded by its tile's data,
each taking at least a byte of it, and take 12 bytes each: at most
16,000,000 (192 MB; 1,024 code-blocks in 15,625 layers take that many),
so that a file of empty packets cannot ask for 12 times its size. Each
contribution of a code-block to a layer that they signal takes 32 bytes,
and a packet header signals one in a few bits, so at most 8,000,000 are
read (250,000 code-blocks in 32 layers, 256 MB). A file beyond a bound
fails with a message that states it, as in "code-block count exceeds
supported limit (250,000)". The resolution and packet bounds are
`hv_geometry`'s (`hv_geometry_limits`), which the transcoder sets.

Packet headers are read as T.800 requires, where hvJP2K is lenient: an
SOP marker segment must have Lsop = 4 and Nsop equal to the packet's index
in the tile, modulo 65,536 (A.8.1); when COD signals EPH, every packet
header must end with it (A.8.2); and the byte after each 0xFF in a header
must have its top bit clear (B.10.1). hvJP2K skips six bytes after any SOP
code, skips EPH where it finds it, and ignores the stuffed bit. None of the
4,014 EUI files, 200 AIA files from JSOC (10 wavelengths, 2015 to 2026),
hvJP2K fixtures or test corpus files breaks these rules.

The code-block data are checked too: a code-block's contribution to a
packet must not end in 0xFF (B.10.7) or hold 0xFF followed by a byte above
0x8F, which reads as a marker (C.3.4).

Packet-level errors otherwise use hvJP2K's messages, except for the rules
above, the memory bounds, and one case: hvJP2K reads a tile's tile-parts
as one concatenated block, `hv_transcode` reads each in place. A packet in
a tile-part other than the last that runs past its tile-part is reported
as "packet crosses a tile-part boundary" even when the data after it would
also run out, where hvJP2K reports "packet data overruns the tile". Header
errors come from the reader and use its messages.

## XML boxes

An XML box holds a well-formed XML document (T.800 I.7.1), which has no
NUL character (XML 1.0, `Char`). Old Kakadu, run from IDL, ended its XML
boxes with one. jpylyzer rejects such a file, and hvJP2K's
`--xml-rewrite` was meant to remove the NUL. So every top-level XML box
ends before its first NUL, with its length set to match; a box without a
NUL is copied as read. Nothing else in the XML is read or changed: it is
not checked, and not reserialized as hvJP2K's glymur does. XML boxes
inside superboxes are copied as read with the superbox.

## Files

| File | Role |
| --- | --- |
| `hv_transcode.c` | The command: options, reading the input, writing the output file. |
| `transcode_file.c` | `hv_transcode_file`: boxes (the codestream is transcoded straight into its `jp2c` box, XML boxes end before their first NUL). |
| `transcode.h` / `.c` | `hv_transcode_codestream` (`transcode_codestream`): reads the codestream and writes its main header with the new COD, lays out the tile twice with `hv_geometry` (input and new precincts), checks the memory limits and the code-block partition, and writes the tile as one tile-part. |
| `tier2.h` / `.c` | Packets (T.800 B.9, B.10) on an `hv_geometry` and its packet order. `hv_read_packets` decodes the headers in place, tile-part by tile-part, and records each code-block's contributions per layer; `hv_write_packets` encodes them, either for the lengths only (for the PLT) or into the output, copying the code-block bytes from the input. |
| `test/` | `test_transcode.c` and `cli_test.sh` (below); `fixtures/`. |
| `fuzz_transcode.c` | libFuzzer target for the codestream level: an accepted input's output must transcode to itself, and pass `HV_PROFILE` when its main header passes `HV_PROFILE_HEADERS`. |

## Build and test

With the rest of the repository, so configuring needs the server's
dependencies too: the top-level `CMakeLists.txt` requires zlib, glib,
llhttp and libuv before it adds `lib/`, `transcode/` and `merge/`.

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
tests/run.sh             # or: tests/run.sh sanitize
TRANSCODE_ARCHIVE=~/AIA:~/EUI tests/run.sh
```

CTest options go after the mode, or first for normal mode
(`tests/run.sh [normal|sanitize] [CTest options]`, or
`tests/run.sh -R transcode`). The build goes to
`build/tool-tests-<mode>`, or to `ESAJPIP_TEST_BUILD_DIR`, which
`../tests/run.sh` also reads: set it for one runner at a time.

`test_transcode` checks that every file in `test/fixtures/input/` transcodes
to its Kakadu reference in `test/fixtures/kakadu/` (COM and the XML box
aside: it is the input's, before its NUL), that each output is within the
served profile and transcodes to itself (the origin-129 file is rejected,
and only its codestream is compared); what the profile accepts and rejects;
the boxes around the codestream (LBox = 0 on a superbox and its child, a
malformed child, XML boxes cut before their first NUL or copied as read);
tile-parts split every way T.800 allows and broken in the ways it does not;
malformed main headers; 2,000 corrupted tiles, each rejected or giving a
stable output; the SOP, EPH, bit-stuffing and code-block data rules; the
memory bounds (65,536 component-resolutions and 250,000 code-blocks at
their edge, and 16,000,000 input packets, 2,000,000 output packets and a
lowered contribution limit exceeded); the code-block partition changed at
one resolution only, at the first or the last, in a band of one
code-block, or only in empty bands; the
precinct sizes at their limits; the segments dropped, kept and rejected in
each header; 255 tile-parts and a 256th; two tiles. `cli_test.sh` checks the
command: options, exit status, in-place replacement keeping the mode, output
through a symbolic link, and nothing written or left behind on failure. With
`TRANSCODE_ARCHIVE` set to directories, every `.jp2` file in them is
transcoded and checked for a stable output too. The fixtures are described
in `test/fixtures/FIXTURES.md`. The same run includes the reader's tests
(`../tests/lib/`).
