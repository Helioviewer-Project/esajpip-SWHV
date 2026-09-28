# Coverage Snapshot

This file records one local baseline run and the follow-up diagnostic checks.
Re-run the commands before using the numbers as current coverage data.

## Baseline Command

```sh
sh tests/run_baseline.sh
```

The first sandboxed run could not write the checkout build tree. A rerun of the
same command with build-directory write access passed all CTest tests.

Clean rerun result:

- Tests: 22/22 passed.
- Duration: 14.57 s real, 1.35 s user, 1.36 s sys.
- Peak RSS: 89,849,856 bytes.
- Report directory: `build/coverage-report`.

`llvm-cov` warned about 27 functions with mismatched data because some sources
are compiled into more than one executable. Treat the group totals as triage
data, not exact per-function accounting.

## Coverage Totals

| Code group | Function coverage | Line coverage | Branch coverage |
| --- | ---: | ---: | ---: |
| Handwritten code | 89.16% | 83.51% | 73.12% |
| Generated JPEG 2000 model code | 68.97% | 74.00% | 55.34% |
| ASN.1 runtime | 63.16% | 42.30% | 28.75% |

## Triage From This Snapshot

- Existing tests plus corpus replay cover most handwritten reader/profile,
  rewrite, geometry, transcode, merge, packet selection, writer stability, and
  focused allocation-failure paths.
- `src/server/server.cc` still reports no coverage even though server tests run.
  Investigate coverage object mapping before assuming the live server loop is
  untested.
- `lib/hv_walk.c` reports no coverage; add a deterministic command-line test if
  `hv_walk` is intended to be part of regression coverage.
- Generated decoder/encoder coverage improved through replay, but
  `j2k-headers.c` and `jp2-boxes.c` still have many unexecuted functions and
  weak branch coverage.
- ASN.1 runtime branch coverage remains low. The `asn1` replay/fuzz path reaches
  more generated code, but does not yet systematically exercise determinant,
  optional-field, array-count, and error-cleanup paths.
- Merge fuzzing now has valid paired seeds, but still needs one-valid-one-mutated
  source coverage and linked-output replay if that behavior matters.
- Transcode replay checks structural/profile validity and codestream idempotence.
  It does not prove pixel equivalence or independent decoder agreement.

## Diagnostic Checks Run

- ASan/UBSan: host CTest passed in `/private/tmp/esajpip-profile-asan`.
- Extended sanitizer: focused `protocol` passed after explicit byte conversions
  were added for sanitizer-reported narrowing. The full host suite was not
  completed in that pass because `server` produced no progress after about 90 s.
- Optimized build: `protocol` and replay smoke checks passed in
  `/private/tmp/esajpip-profile-optimized`.
- Fuzz build: all five libFuzzer targets built with Homebrew LLVM and passed
  seeded smoke runs, plus deterministic replay over the generated corpora. No
  long fuzz campaign result is recorded here.
- Docker Valgrind: `sh tests/run_linux_docker.sh valgrind` passed the
  fixed replay set under Debian 13 Linux arm64. Memcheck reported zero errors
  and no leaks for each replay invocation.
- Docker MSan: `sh tests/run_linux_docker.sh msan` passed the same replay
  set under Debian 13 Linux arm64 using Debian's Clang/MSan runtime packages.
- Docker sanitizer fuzz: short `fuzz-all` validation passed all five libFuzzer
  targets under Linux ASan/UBSan, extended integer/bounds checks, and MSan.

## Added Diagnostic Entry Points

- CMake options: `BUILD_TESTING` and `ESAJPIP_FUZZ` (both in `tests/`), and
  `ESAJPIP_SANITIZE`.
- Instrumentation profiles in `tests/instrumentation.sh`, which are flag
  recipes rather than options: `extended`, `msan`, `coverage`, `fuzz`.
- Replay modes:
  - `reader-rewrite`
  - `deferred-plt`
  - `transcode-fuzz`
  - `merge-fuzz`
  - `asn1`
- Fuzz targets:
  - `fuzz_reader_rewrite`
  - `fuzz_deferred_plt`
  - `fuzz_merge`
  - `fuzz_transcode`
  - `fuzz_asn1`
- Docker profiles:
  - `tests/run_linux_docker.sh valgrind`
  - `tests/run_linux_docker.sh msan`
  - `tests/run_linux_docker.sh all`
  - `tests/run_linux_docker.sh fuzz-asan`
  - `tests/run_linux_docker.sh fuzz-extended`
  - `tests/run_linux_docker.sh fuzz-msan`
  - `tests/run_linux_docker.sh fuzz-all`
