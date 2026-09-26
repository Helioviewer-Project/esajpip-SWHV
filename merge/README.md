# merge

`hv_merge`, the C port of hvJP2K's JPX merger (`hv_jpx_merge`,
`jpx_merge.pyx`, `jpx_common.pyx`). It merges JP2 files into a JPX movie
for JHelioviewer and the JPIP server, embedding their codestreams or
linking to them, and writes the same bytes as hvJP2K. It reads the inputs
with `jpeg2000_io` (`../lib/`: `hv_reader` with the shared rules of
`hv_rules`), and writes the boxes the server reads (`flst`, `url`, `dtbl`)
with `hv_writer`, from the model's types.

## Usage

    hv_merge -i jp2file1,jp2file2,... [jp2file ...] -o jpxfile [-links] [-s argfile]

The options are hvJP2K's:

- `-i`: the input JP2 files, in the order of the JPX codestreams, comma or
  space separated (empty names between commas are dropped).
- `-o`: the output JPX file.
- `-links`: link to the codestreams (`ftbl`, and `url` boxes naming each
  input by absolute, resolved path) instead of copying them (`jp2c`).
- `-s`: more arguments from a file, or from standard input for `-`, split as
  Python's `shlex.split` does (quotes and backslashes).
- `-h`: the usage, on standard output.

They are parsed as hvJP2K's `argparse` parser does. With `-s`, the command
line and the file's words after it are parsed again, once: a later `-i`
or `-o` replaces an earlier one, the command line's included; a `-s` in
the file is parsed but not read; words in the file continue a `-i` that
ends the command line, and are an error otherwise. `-i` takes the words up
to the next one that looks like an option: one starting with `-`, other
than `-` alone, a negative number such as `-1`, or a word with a space
that names no option. An option's value may be attached (`-ofile.jpx`,
`-o=file.jpx`, `-i` then taking that one name), and an option may be
abbreviated (`-lin`). `--` is an error. Names that leave nothing once the
commas are dropped fail with hvJP2K's "no JP2 input files". `cli_test.sh`
checks these cases. Deliberate differences: the argument file is read as
bytes, where hvJP2K decodes it as text and fails on bytes the locale's
encoding rejects; a NUL byte in it is an error (it would end the text
early); and `-h` prints hv_merge's own usage text.

The inputs are read in two passes, as hvJP2K does. The first input is
checked once and stays mapped throughout. Each later input is checked, unmapped,
then mapped and checked again before its bytes are used. Its size and
parsed structure must match the first pass. At most two inputs are mapped
at a time. With `-links`, the second open supplies the header and XML
boxes; the codestream stays in the input JP2 file. The output is written
to a temporary file next to it and renamed into place (`hv_file`, in
`../lib/`); on error nothing is written. An existing output keeps its
permissions, and one that is a symbolic link is written where the link
points, the file it names created if need be, as when hvJP2K opens it for
writing. Exit status: 0 (also for `-h`), 1 on error, 2 on usage errors.

A linked JPX file records each input's path and its codestream's offset
and length, so the inputs must stay as they are: rewriting one, as
`hv_transcode` does in place, moves its codestream and breaks the link.
Merge again after changing an input.

## The output

As hvJP2K writes it:

- the signature, `ftyp` (brand `jpx `, MinV 1, compatible with `jpx `,
  `jp2 ` and `jpxb`, or `jpx ` alone when linked; the model's `FtypHeader`
  and `Brand`) and `rreq`, written with the model's encoders one part at
  a time (`hv_write_rreq`: `RreqHeader` with ML 1, then each
  `RreqStandardFeature`, and no vendor features; T.801 M.11.1: features 1,
  2 for more than one codestream, 4 and 5 from the codestreams' Rsiz, 9
  and 10 for opacity channels in `cdef`, 15 when linked);
- `jp2h`, the first input's, with each `colr`'s APPROX of 0 written as 1
  (T.801 has no 0);
- for each input, a `jpch` and a `jplh`: empty when its `jp2h` is the
  first input's, otherwise the `ihdr` and whatever of `bpcc`, `pclr`,
  `cmap` (generated where the first input has a palette and this one has
  none), `colr` (in a `cgrp`), `cdef` and `res` differs from the first;
- the codestream, as `jp2c` or as an `ftbl` holding one `flst` fragment;
- its first XML box, if any, in an `asoc` with an `nlst` naming codestream
  and compositing layer i;
- when linked, a final `dtbl` with one `url` per input.

## Supported input

Every input must be a JP2 file within the served profile (`hv_check_jp2`,
and its codestream with `HV_PROFILE`), so the JPX file is within it too
(`hv_check_jpx`), and must have valid header boxes (`hv_check_jp2h`: T.800
I.5.3, with `ihdr` and `bpcc` agreeing with SIZ), which the JPX headers are
made of, so theirs are valid too (`hv_check_jpx_headers`). The tests check
both of every JPX file they write. Two things valid in a JP2 file are not
in the JPX file, and are rejected: more than one `colr` box with the same
METH in the first input, whose `jp2h` the JPX file's is
(`colr.one-method`, T.801 M.11.7.1); and a later input without a `cdef` or
`res` box after a first input with one, as its `jplh` would inherit the
first input's from `jp2h` (T.801 M.11.7). hvJP2K checks neither the
profile, nor the header boxes, nor the link targets, nor these two: it
merges files without PLT, which the server then rejects.

Errors about an input name the file and, where a shared rule fails, the
rule and its offset, as in `siz.zero-origin at 410`. The others are
described in words: a missing `cdef` or `res`, too many `colr` boxes, a
file that changed between the passes, and the limits. The output must be
at most `INT_MAX` bytes (`file.size-limit`), and a link must name a `.jp2`
file (`url.jp2-target`). Limits: an input's `jp2h` holds at most 16 `colr`
boxes, a JPX file at most 16,777,215 inputs, and a linked one at most
65,535.

Differences from hvJP2K, beyond these checks: an XML box running to the
end of its file (LBox = 0) gets an explicit length inside the `asoc`,
where hvJP2K would copy the zero; and every box copied from `jp2h` gets a
fresh header, as glymur writes it, where only an XLBox form would differ.

## Files

| File | Role |
| --- | --- |
| `hv_merge.c` | The command: options, `-s`, mapping the inputs, writing the output file. |
| `merge.h` / `.c` | `hv_merge_files`: checks the inputs, and writes the JPX file box by box to a stream, opening each input when it needs it (`hv_merge_buffers` for inputs already in memory). |
| `test/` | `test_merge.c`: hvJP2K's output byte for byte (`fixtures/`), the linked merge box for box against it and within the served profile, the reader requirement cases of hvJP2K's tests, rejected inputs (header boxes, `colr` counts and methods, the limits), inputs opened on demand (`test_opening`: how often each is opened, at most two at a time, a failed first or second open, an input changed between the passes, in size or in content), and 1,024- and 1,025-character links; `cli_test.sh`: the command (options as `argparse` parses them, `-s`, permissions, symbolic links, failures). |
| `fuzz_merge.c` | libFuzzer target: two JP2 files from one input, merged; an accepted merge must pass `hv_check_jpx`, `hv_check_jpx_headers` and `HV_PROFILE` for each codestream. |

## Build and test

With the rest of the repository, so configuring needs the server's
dependencies too: the top-level `CMakeLists.txt` requires zlib, glib,
llhttp and libuv before it adds `lib/`, `transcode/` and `merge/`.

```sh
cmake -S . -B build [-DESAJPIP_SANITIZE=ON]
cmake --build build --target hv_merge
```

Tests, with the other tools' and separate from the server's:

```sh
lib/test/run.sh             # or: lib/test/run.sh sanitize
```

CTest options go after the mode, which must then be given
(`lib/test/run.sh [normal|sanitize] [CTest options]`). The build goes to
`build/tool-tests-<mode>`, or to `ESAJPIP_TEST_BUILD_DIR`, which
`../tests/run.sh` also reads: set it for one runner at a time. The
fixtures are described in `test/fixtures/FIXTURES.md`.

Fuzzing (Clang):

```sh
cmake -S . -B fuzz -DCMAKE_C_COMPILER=clang -DESAJPIP_SANITIZE=ON -DESAJPIP_FUZZ=ON
cmake --build fuzz --target fuzz_merge
fuzz/merge/fuzz_merge -max_len=262144 corpus/
```
