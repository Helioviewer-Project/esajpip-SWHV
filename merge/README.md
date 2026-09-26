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
- `-s`: more arguments from a file, split as Python's `shlex.split` does
  (quotes and backslashes), after those of the command line.

The inputs are mapped into memory and the codestreams copied from there
to the output. The output is written to a temporary file next to it and
renamed into place; on error nothing is written. Exit status: 0, 1 on
error, 2 on usage errors.

## The output

As hvJP2K writes it:

- the signature, `ftyp` (brand `jpx `, compatible with `jpx `, `jp2 ` and
  `jpxb`, or `jpx ` alone when linked) and `rreq` (T.801 M.11.1: features
  1, 2 for more than one codestream, 4 and 5 from the codestreams' Rsiz, 9
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

Every input must be a JP2 file the server serves (`hv_check_jp2`, and its
codestream with `HV_PROFILE`), so the JPX file is one it serves too
(`hv_check_jpx`), and must have valid header boxes (`hv_check_jp2h`: T.800
I.5.3, with `ihdr` and `bpcc` agreeing with SIZ), which the JPX headers are
made of, so theirs are valid too (`hv_check_jpx_headers`). The tests check
both of every JPX file they write. hvJP2K checks neither the profile, nor
the header boxes, nor the link targets: it merges files without PLT, which the server then
rejects. Errors name the file and the rule, as in `siz.zero-origin`. The
output must be at most `INT_MAX` bytes (`file.size-limit`), a link must
name a `.jp2` file (`url.jp2-target`), and a linked JPX file holds at most
65,535 links.

Differences from hvJP2K, beyond these checks: an XML box running to the
end of its file (LBox = 0) gets an explicit length inside the `asoc`,
where hvJP2K would copy the zero; and every box copied from `jp2h` gets a
fresh header, as glymur writes it, where only an XLBox form would differ.

## Files

| File | Role |
| --- | --- |
| `hv_merge.c` | The command: options, `-s`, mapping the inputs, writing the output file. |
| `merge.h` / `.c` | `hv_merge_files`: checks the inputs, and writes the JPX file box by box to a stream. |
| `test/` | `test_merge.c`: hvJP2K's output byte for byte (`fixtures/`), the linked merge box for box against it and served, the reader requirement cases of hvJP2K's tests, and rejected inputs, header boxes included; `cli_test.sh`: the command. |
| `fuzz_merge.c` | libFuzzer target: two JP2 files from one input, merged; an accepted merge must pass `hv_check_jpx`, `hv_check_jpx_headers` and `HV_PROFILE` for each codestream. |

## Build and test

With the rest of the repository:

```sh
cmake -S . -B build [-DESAJPIP_SANITIZE=ON]
cmake --build build --target hv_merge
```

Tests, with the other tools' and separate from the server's:

```sh
lib/test/run.sh             # or: lib/test/run.sh sanitize
```

The fixtures are described in `test/fixtures/FIXTURES.md`.

Fuzzing (Clang):

```sh
cmake -S . -B fuzz -DCMAKE_C_COMPILER=clang -DESAJPIP_SANITIZE=ON -DESAJPIP_FUZZ=ON
cmake --build fuzz --target fuzz_merge
fuzz/merge/fuzz_merge -max_len=262144 corpus/
```
