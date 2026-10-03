# Diagnostics: the runners beyond `tests/run.sh`

Everything here is a way to run the suite in `tests/` under another
toolchain. The scripts are deliberately small; they do not replace CTest or
long fuzz campaigns.

```
tests/run.sh               the suite, normal or sanitized
tests/run_profile.sh       the suite under asan, extended, msan, optimized,
                           valgrind, or the fuzz targets
tests/run_baseline.sh      the suite with Clang source coverage
tests/run_linux_docker.sh  replay under Valgrind/MSan and Linux sanitizer
                           fuzz campaigns, on Debian 13
tests/run_mutation.sh      mutants of the sources, each run against the tests
tests/fuzz/replay          the deterministic counterpart of the fuzz targets
```

Use the standard build and CTest controls with these scripts:

- `ESAJPIP_JOBS=8` limits `cmake --build --parallel` in every runner.
- `ESAJPIP_CTEST_JOBS=8` limits scripts that invoke CTest internally.
- `CMAKE_BUILD_PARALLEL_LEVEL` and `CTEST_PARALLEL_LEVEL` are the standard
  equivalents, used when the `ESAJPIP_*` variables are not set.
- `tests/run.sh` also accepts CTest options after the mode, so `-j8`,
  `-L tools` and `-R '^merge$'` go straight to CTest.
- Docker fuzzing has its own worker knob, `ESAJPIP_FUZZ_WORKERS`.

Common 8-core commands:

```sh
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 ./tests/run.sh normal
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 ./tests/run.sh sanitize
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 sh tests/run_baseline.sh
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 \
  ESAJPIP_BASELINE_SPLIT=ON sh tests/run_baseline.sh

ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 sh tests/run_profile.sh asan
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 sh tests/run_profile.sh extended
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 sh tests/run_profile.sh optimized

ESAJPIP_JOBS=8 ESAJPIP_FUZZ_SECONDS=300 ESAJPIP_FUZZ_WORKERS=8 \
  sh tests/run_linux_docker.sh fuzz-all
```

## Coverage

```sh
sh tests/run_baseline.sh
```

A host-local Clang source-coverage run. By default it uses
`build/coverage` and `build/coverage-report`; `ESAJPIP_BASELINE_BUILD` and
`ESAJPIP_BASELINE_OUT` override them. Set `ESAJPIP_JOBS` for the build and
`ESAJPIP_CTEST_JOBS` for the test run:

```sh
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 sh tests/run_baseline.sh
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 \
  ESAJPIP_BASELINE_SPLIT=ON sh tests/run_baseline.sh
```

The script configures `BUILD_TESTING=ON` with the `coverage` flags from
`tests/instrumentation.sh`, runs the whole of CTest, and writes three
`llvm-cov report` outputs:

- `handwritten.txt`: `jpeg2000`, `jpip`, `merge`, `transcode`, `server` and `tools`, excluding
  generated and test files.
- `generated.txt`: `jpeg2000/generated`, excluding the ASN.1 runtime files.
- `asn1-runtime.txt`: `asn1crt*.c`.

With `ESAJPIP_BASELINE_SPLIT=ON`, the script also writes
`build/coverage-report/by-test/` reports for these deterministic groups:

- `normal-no-replay`
- `jpeg2000`
- `merge`
- `transcode`
- `server`
- `replay`
- `jpeg2000-merge-transcode`

The compiler and the profiler must be the same LLVM, so when Homebrew's LLVM
provides `llvm-cov` the script also builds with its `clang`; `LLVM_COV`,
`LLVM_PROFDATA` and `LLVM_PREFIX` override the search. `tests/COVERAGE_GAPS.md`
reads these reports.

## Replay Driver

`tests/fuzz/replay` includes every fuzz target under another name, so a mode
*is* a target: the same assertions, run one input at a time, with no libFuzzer
runtime. It builds in any configuration, including a plain
`-DBUILD_TESTING=ON` one, and is what the sanitizer, coverage and Valgrind runs
use.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target replay

build/tests/fuzz/replay reader-rewrite tests/vectors/j2k/jp2.jp2
build/tests/fuzz/replay reader-rewrite build/tests/fuzz/corpus/reader-rewrite
build/tests/fuzz/replay deferred-plt tests/vectors/j2k/jp2-precincts.jp2
build/tests/fuzz/replay transcode-fuzz seed.j2k
build/tests/fuzz/replay merge-fuzz seed.merge
build/tests/fuzz/replay asn1 build/tests/fuzz/corpus/asn1
```

Modes: `reader-rewrite`, `deferred-plt`, `asn1`, `transcode-fuzz`,
`merge-fuzz`. A `codestream` input is a raw codestream (the `jp2c` payload of
a JP2 file); a `merge` input is a 4-byte big-endian split offset followed by
two JP2 files, which is the layout `tests/fuzz/fuzz_merge.c` documents; an
`asn1` input is a byte that selects the PDU type, modulo the number of types,
followed by its encoding. The types are those with a `T_ACN_Decode` in
`jpeg2000/generated`, in the order of the headers' names, which CMake writes to
`build/.../tests/fuzz/asn1_pdus.h`; `make_corpus.py` gives every type the
structures at the starts of boxes, marker segments and their bodies in four
vectors.
Seed regeneration adds or refreshes seeds without deleting existing corpus
files, so inputs discovered by a campaign survive rebuilds.
Each input may be a file or a directory. Directory entries run in sorted order.
`ESAJPIP_REPLAY_VERBOSE=1`, or `-v` after the mode, names each input before
running it, so a crash under a corpus names its file. CMake generates the
standard replay and fuzz seed corpora under `build/.../tests/fuzz/corpus`.

## Host Profiles

```sh
sh tests/run_profile.sh asan
sh tests/run_profile.sh extended
sh tests/run_profile.sh msan
sh tests/run_profile.sh optimized
sh tests/run_profile.sh fuzz
sh tests/run_profile.sh fuzz-asan
sh tests/run_profile.sh fuzz-extended
sh tests/run_profile.sh valgrind tests/vectors/j2k/jp2.jp2
```

Builds go to `build/profile-MODE` unless `ESAJPIP_TEST_BUILD_DIR` says
otherwise. `asan` uses the build's `ESAJPIP_SANITIZE`; the others are flag
recipes in `tests/instrumentation.sh`, so the build files stay out of it. Use
`ESAJPIP_JOBS` and `ESAJPIP_CTEST_JOBS` for the CTest-based profiles:

```sh
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 sh tests/run_profile.sh extended
```

Not every profile works on every host:

- `asan`: the whole suite with `ESAJPIP_SANITIZE=ON`.
- `extended`: the whole suite with asan and ubsan plus `unsigned-integer-overflow`,
  `implicit-conversion` and `local-bounds`, failing on the first report.
- `msan`: the whole suite under MemorySanitizer with origins; needs a Linux
  Clang target, the Darwin one has no MSan runtime.
- `optimized`: the whole suite in `Release`.
- `fuzz`: builds the libFuzzer targets under `tests/fuzz` and runs their
  seeded CTest smoke runs (`ctest -L fuzz`). Needs the libFuzzer runtime:
  Homebrew's LLVM has it, the Apple Command Line Tools do not. Set
  `LLVM_PREFIX`, or let the script use `brew --prefix llvm`.
- `fuzz-asan`: bounded libFuzzer mutation campaigns under ASan and UBSan.
- `fuzz-extended`: bounded libFuzzer mutation campaigns under ASan, UBSan,
  `unsigned-integer-overflow`, `implicit-conversion` and `local-bounds`. This
  profile is supplied by `tests/instrumentation.sh`; it is not a CMake option.
- `valgrind`: builds `replay` in a non-sanitized debug build and runs
  `reader-rewrite` under Memcheck for the files given, or for `jp2.jp2` and
  `jp2-precincts.jp2` when none are.

The local fuzz campaign defaults match the Linux runner: 60 seconds per target
and 8 workers, over `fuzz_reader_rewrite`, `fuzz_deferred_plt`, `fuzz_asn1`,
`fuzz_transcode`, `fuzz_merge`, `fuzz_client_response` and
`fuzz_client_source`. Override with `ESAJPIP_FUZZ_SECONDS`,
`ESAJPIP_FUZZ_WORKERS` and `ESAJPIP_FUZZ_TARGETS`. Results stay in
`build/profile-fuzz-*/fuzz-results/PROFILE/TARGET/`.

## Linux Docker Profiles

```sh
sh tests/run_linux_docker.sh valgrind
sh tests/run_linux_docker.sh msan
sh tests/run_linux_docker.sh all
sh tests/run_linux_docker.sh fuzz-asan
sh tests/run_linux_docker.sh fuzz-extended
sh tests/run_linux_docker.sh fuzz-msan
sh tests/run_linux_docker.sh fuzz-all
```

The runner builds `tests/Dockerfile.linux` as
`esajpip-linux-diagnostics:debian13`, mounts the checkout read-only at `/src`,
and keeps builds under `/tmp` in the container. Each run creates a host
directory under `build/linux-diagnostics`, or `ESAJPIP_LINUX_OUT`, and prints
its path. That directory is mounted at `/results`: each fuzz target keeps its
corpus, `run.log`, worker logs and `artifacts/` there, including when the
campaign fails and the container is removed. The image tracks Debian's default toolchain, including
`build-essential`, `clang`, `llvm` and `libclang-rt-dev`.

`valgrind`, `msan` and `all` execute a fixed replay set: `reader-rewrite` on
two corpus vectors and on a real transcode fixture, `deferred-plt` on a vector,
`transcode-fuzz` on a raw codestream extracted from that fixture, `merge-fuzz`
on a two-file seed, and `asn1` on the generated corpus of selector-prefixed
PDU inputs.

The Valgrind profile also builds `client_tests` and runs every registered
`client` test and `client_fuzz` seed replay through `tests/client/memcheck.cmake`.
The client timeout is 300 seconds to account for Memcheck's execution cost.
Logs and CTest memory-check reports remain in `client-valgrind/` under the
run's result directory, including on failure. OpenJPEG is instrumented along
with the client in sanitizer and coverage builds.

The `fuzz-*` profiles build the libFuzzer targets and run actual mutation
campaigns, not just replay:

- `fuzz-asan`: `ESAJPIP_SANITIZE=ON`, so ASan plus UBSan.
- `fuzz-extended`: ASan and UBSan plus `unsigned-integer-overflow`,
  `implicit-conversion` and `local-bounds`.
- `fuzz-msan`: MemorySanitizer with origin tracking.
- `fuzz-all`: all three Linux fuzz profiles.

The defaults are intentionally bounded: 60 seconds per target and 8 fuzz
workers. `ESAJPIP_JOBS` controls the Docker build, while
`ESAJPIP_FUZZ_WORKERS` controls libFuzzer workers. Override the campaign with
`ESAJPIP_FUZZ_SECONDS`, `ESAJPIP_FUZZ_WORKERS` and `ESAJPIP_FUZZ_TARGETS`, for
example:

```sh
ESAJPIP_JOBS=8 ESAJPIP_FUZZ_SECONDS=300 ESAJPIP_FUZZ_WORKERS=8 \
  sh tests/run_linux_docker.sh fuzz-extended

ESAJPIP_FUZZ_TARGETS="fuzz_reader_rewrite fuzz_transcode" \
  sh tests/run_linux_docker.sh fuzz-msan
```

The Docker Valgrind/MSan replay profiles are deterministic replay runs, not
parallel mutation campaigns. The fuzz profiles run one target after another,
and each target uses `ESAJPIP_FUZZ_WORKERS` libFuzzer workers.

Pin Docker architecture if needed:

```sh
ESAJPIP_DOCKER_PLATFORM=linux/arm64 sh tests/run_linux_docker.sh all
ESAJPIP_DOCKER_PLATFORM=linux/amd64 sh tests/run_linux_docker.sh fuzz-msan
```

## Mutation

```sh
sh tests/run_mutation.sh                         # jpeg2000/hv_rules.c jpeg2000/hv_reader.c
ESAJPIP_MUTATION_OPERATORS=rule sh tests/run_mutation.sh jpeg2000/hv_rules.c
ESAJPIP_MUTATION_LINES=600-900 sh tests/run_mutation.sh jpeg2000/hv_rules.c
ESAJPIP_MUTATION_TESTS="-L tools -E _command" sh tests/run_mutation.sh merge/merge.c
```

`tests/mutate.py` lists the one-operator mutants of a C file and applies
one: a rule's `return "..."` made `return NULL`, a relational operator
changed (`==` and `!=` swapped, `<` and `<=`, `>` and `>=`), `&&` and `||`
swapped, a `!` dropped, and, when asked for, an integer literal made one
larger (`constant`). Comments, strings and preprocessor lines are left
alone, and a mutant's number is its place in the file, so the same file
gives the same numbers.

`tests/run_mutation.sh` copies the checkout, but for `build/` and `.git`,
to `build/mutation/src` (`ESAJPIP_TEST_BUILD_DIR` to move it), builds it
without LTO, and checks that the copy passes the tests. Then, for each
mutant, it writes the mutant into the copy, builds incrementally, runs the
tests (`ESAJPIP_MUTATION_TESTS`, by default `-L tools`, each within
`ESAJPIP_MUTATION_TIMEOUT` seconds, by default 120) and puts the file back.
The checkout is never changed. A mutant is killed when a test fails,
survives when every test passes, and is counted apart when a test times
out or it does not compile. `ESAJPIP_MUTATION_EVERY=N` takes every Nth
mutant and `ESAJPIP_MUTATION_LINES=FIRST-LAST` those on some lines, to split
a long run. `build/mutation/report.tsv` has a line per mutant and
`build/mutation/survivors.txt` the survivors, each a check that no test
makes fail, or code whose change nothing observes.
