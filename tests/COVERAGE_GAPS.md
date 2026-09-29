# Coverage Snapshot

This file records one local baseline run, the follow-up diagnostic checks,
and the gaps that remain.
Re-run the commands before using the numbers as current coverage data.

## Baseline Command

```sh
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 ESAJPIP_BASELINE_SPLIT=ON sh tests/run_baseline.sh
```

Run result:

- Tests: 23/23 passed.
- Duration: 9.67 s real, 1.52 s user, 2.19 s sys.
- Peak RSS: 85,590,016 bytes.
- Report directory: `build/coverage-report`.
- Split reports: `build/coverage-report/by-test`.

`llvm-cov` warned about 26 functions with mismatched data because some sources
are compiled into more than one executable. Treat the group totals as triage
data, not exact per-function accounting.

## Coverage Totals

| Code group | Function coverage | Line coverage | Branch coverage |
| --- | ---: | ---: | ---: |
| Handwritten code | 90.25% | 85.82% | 76.13% |
| Generated JPEG 2000 model code | 100.00% | 98.33% | 80.19% |
| ASN.1 runtime | 71.93% | 55.21% | 39.95% |

## Gaps

This snapshot includes `walk_command`, broad direct ASN.1 replay, linked and
embedded merge replay, and the new rule/mutation diagnostics. What remains:

- The historical zero coverage for `src/server/server.cc` was a collection
  problem, fixed on 2026-09-29. The forked test servers call `_exit`, so the
  coverage build now flushes each child's profile explicitly and resets its
  inherited counters. Each child uses a different filename pattern because
  LLVM caches PID substitutions and does not reparse an unchanged pattern
  ([runtime implementation](https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/profile/InstrProfilingFile.c)).
  A focused `server` run produced separate parent and child profiles and
  measured 84.09% line coverage and 66.02% branch coverage for `server.cc`.
  This verifies collection; the whole-suite snapshot above has not been
  refreshed.
- `lib/hv_file.c`: the read and map error paths, and the signal handler,
  whose process is killed before it writes its profile.
- `src/transcode`: the command's option and I/O error paths.
- ASN.1 runtime: the generated code calls only part of `asn1crt*.c`, so its
  totals stay low; the functions it does call are reached through
  `fuzz_asn1`.
- Mutation testing (`tests/run_mutation.sh`, `rule` operator) leaves
  mutants of `lib/hv_rules.c` that the tests pass: rules no vector or test
  case fails. `tests/lib/test_profile` requires a vector's rule only where it
  fails the header layer, which is part of why.
- Transcode replay checks structural/profile validity and codestream
  idempotence. It does not prove pixel equivalence or independent decoder
  agreement.

## Focused reader assertions added on 2026-09-29

- Count released reader allocations after successful parsing, parse errors,
  and failures while allocating tile pages. Cleanup checks now work without
  a leak sanitizer.
- Check exact error offsets for invalid JPX MinV, missing IPR metadata, and
  baseline JPX color/header restrictions, using the existing corpus files.
- Check BPCC's payload boundary with a reserved value just outside the box.
- Allocate PLT inputs at their exact size and cover overflow followed by a
  complete or truncated suffix, plus an unterminated maximum-length entry.

The PLT recovery-loop `<` to `<=` survivor is not established as a reachable
fault: recovery only follows successful decoding of a terminated, overflowing
entry. The new cases test the decoder and cursor boundaries without changing
production checks. No mutation score was recomputed for these additions.

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
