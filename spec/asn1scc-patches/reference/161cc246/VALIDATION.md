# Retirement validation: ASN1SCC issue #417

Validated on 2026-09-29 against unmodified upstream ASN1SCC 4.9.7.0,
commit `1afedf0c205c1fb783245e1f2d1e06cacc06d042`.

## Integration and preservation

[Upstream issue #417](https://github.com/esa/asn1scc/issues/417#issuecomment-5897269874)
confirms acceptance of all eleven patches. Commit `1e10e1b1` integrates the
series; later commits fix the length-embedded error-code declaration,
Ada deferred copies, legacy C BIT STRING region bounds, Python fixed-size
determinants, and test registration and coverage.

All eleven archived patch files and their `series` file are byte-identical
to the preceding esajpip commit. Their original base is
`161cc2465b568685c09b0a218149fb514ea2a95e`, not the new compiler pin.
All issue reproducer sources and scripts are unchanged. The current build
exports the new pin and applies no patches.

The default Docker image `esajpip-asn1scc` was rebuilt by
`spec/build-asn1scc.sh`. The assemblies in `~/jhv/asn1scc-bin/asn1scc` were
updated from that build; `--version` reports 4.9.7.0. The existing bundled
.NET runtime is Linux ARM64, so that installed copy was verified in Docker,
not as a native macOS executable.

## Deterministic checks

| Check | Result |
| --- | --- |
| Upstream C ACN v2 `23-CONTAINING` | 11 configurations pass |
| Upstream C ACN v2 `24-DEDUCED-SIZE` | 18 configurations pass |
| Upstream C ACN v2 `25-ACNV2-BOUNDARIES` | 26 configurations pass |
| Upstream wire tests | 19 byte-exact checks, truncated input and warning/rejection checks pass |
| Upstream `-icdPdus` checks | Pass |
| Preserved local C reproducers 0001–0011 | All pass, including their ASan/UBSan checks |
| Preserved Rust reproducers 0009–0011 | All pass, with legacy/v2 and uPER modes selected by their scripts |
| Generated production parser | Byte-identical to checked-in C and headers |
| Full model and writer harness, ASan/UBSan | 845 vectors; all expected labels and reasons pass |
| Regenerated corpus | Existing files and manifest byte-identical |
| OpenJPEG comparison | Pass with the documented limitations below |
| Complete release CTest suite | 34/34 pass |
| Complete extended sanitizer CTest suite | 34/34 pass |
| Fuzz build's seeded smoke/replay tests | 10/10 pass |
| Debian 13 Valgrind replay | Seven replay invocations; zero errors and no leaks |
| Debian 13 MemorySanitizer replay | All seven replay invocations pass |

The extended profile checks address errors, undefined behavior, unsigned
integer overflow, implicit conversions and local bounds. It uses Homebrew
LLVM 23.1.2. The Linux checks use the repository's Debian 13 diagnostic
image. Rust emits an unused-comparison warning in the preserved 0011
reproducer; it is not suppressed and does not affect the result.

The model writer harness rewrote 389 inputs, transcoded 107 and merged 128;
it checked 251 written files and found every expected label.

## Bounded fuzz campaigns

Each target ran for 300 seconds under ASan/UBSan, with seed 417 and a
separate copy of its generated seed corpus. No crash artifacts or sanitizer
failures were produced. Coverage below is libFuzzer's instrumented-edge
count, not a line-coverage percentage.

| Target | Executed inputs | Coverage before → after | Crash artifacts |
| --- | ---: | ---: | ---: |
| `fuzz_asn1` | 22,995,555 | 4074 → 4113 | 0 |
| `fuzz_deferred_plt` | 3,394,318 | 1750 → 1802 | 0 |
| `fuzz_merge` | 605,807 | 5458 → 5644 | 0 |
| `fuzz_reader_rewrite` | 804,740 | 6296 → 6578 | 0 |
| `fuzz_transcode` | 1,469,063 | 4509 → 4718 | 0 |

Total: 29,269,483 executed inputs. These campaigns provide bounded regression
evidence, not a proof that every input is correct. Production code,
fuzz assertions and corpus labels were not weakened.

## OpenJPEG gate correction

The first complete model run found two previously unlisted OpenJPEG
failures: `jp2-rule-ppm.index-112.jp2` and
`jpx-embedded-rule-ppm.index-112.jpx`. They were already present before the
compiler update, and the regenerated files are byte-identical.

The JP2 fixture differs from the existing
`jp2-rule-main.packet-headers-moved-48.jp2` control only at byte offset 154:
Zppm is 1 instead of 0. Both have empty tile data and packet headers in PPM,
and both produce the same OpenJPEG failure. OpenJPEG explicitly skips gaps
in [`opj_j2k_merge_ppm`](https://github.com/uclouvain/openjpeg/blob/v2.5.4/src/lib/openjp2/j2k.c#L4004-L4006)
but [`opj_j2k_decode_tile`](https://github.com/uclouvain/openjpeg/blob/v2.5.4/src/lib/openjp2/j2k.c#L10133-L10137)
rejects a tile without an allocated data buffer.

The two exact fixture names were therefore added to the existing empty-tile
exception in `spec/check-model.sh`. Their bytes, labels and rule assertions
were retained. No exception for Z-index gaps was added.

## Reproduction

From the esajpip repository root, with a local upstream clone containing
the pinned commit:

```sh
spec/build-asn1scc.sh ~/git/asn1scc esajpip-asn1scc
jpeg2000/generate.sh --check
spec/check-model.sh
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 tests/run.sh normal
ESAJPIP_JOBS=8 ESAJPIP_CTEST_JOBS=8 tests/run_profile.sh extended
ESAJPIP_JOBS=8 tests/run_profile.sh fuzz
ESAJPIP_JOBS=8 tests/run_linux_docker.sh all
```

After the fuzz build, run each of the five executables with its corresponding
copied corpus and `-max_total_time=300 -seed=417 -print_final_stats=1`.
The preserved `run_rust.sh` scripts need Cargo and the pinned compiler.
Raw logs, corpora and campaign summaries from this run are retained under
`/private/tmp/esajpip-asn1scc-417/`.

Ada and Python backend execution was not independently repeated here.
Upstream records remaining Ada limitations in issue #417; esajpip uses C.
