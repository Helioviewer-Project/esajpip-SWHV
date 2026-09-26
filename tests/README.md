# Running the tests

From the repository root:

```sh
./tests/run.sh
./tests/run.sh sanitize
```

The runner configures, builds, and runs the server's CTest suite. Normal and
ASan/UBSan builds use separate directories under `build/`, so neither changes
your installation build. It needs the same compiler and libraries as the
server, but no external image collection, running server, JHV, Docker, or
ASN.1 compiler. Tests create their own files and use loopback sockets.

CTest options follow the mode. For example:

```sh
./tests/run.sh normal -R '^server$'         # live HTTP and JPP validation
./tests/run.sh sanitize -R '^jpeg2000$'    # source parsing and indexing
./tests/run.sh normal --repeat until-fail:10
```

Set `ESAJPIP_TEST_BUILD_DIR` to use another build directory and
`CMAKE_BUILD_PARALLEL_LEVEL` to limit build parallelism. For an already built
tree, `ctest --test-dir build --output-on-failure` remains sufficient.

The JPEG 2000 reader's and `hv_transcode`'s tests are separate: they are
built only with `ESAJPIP_TRANSCODE_TESTS=ON` and run with
`transcode/test/run.sh` (see [`../transcode/README.md`](../transcode/README.md)).
One of them, `reader_profile`, checks the reader against the JP2 labels of
the same corpus, so run both runners after a corpus change.

## What each test owns

| CTest name | Responsibility |
| --- | --- |
| `logging` | Rotation, truncation, concurrent producers, dropped-record reporting, output failure and shutdown draining. Uses small test-only log limits. |
| `protocol` | Configuration boundaries and missing keys, request semantics, cache state, window geometry, packet indexing primitives and exact JPP writer bytes/capacity boundaries. |
| `jpeg2000` | Every committed source-vector label, all declared packets in accepted vectors, linked graphs, progression order, malformed file boundaries, and worker migration/serialization. |
| `server_connection` | Incremental HTTP parsing and direct libuv connection callbacks, deadline transitions, ordered writes and graceful closure. |
| `server` | The real serving loop: HTTP status/headers, admission, limits, channel routing, disconnects and shutdown. Its independent JPP reader reconstructs and verifies source bytes across the full response matrix and stateful scenarios. |

The live JPP checks replace the old nonempty-body gzip and partial-model
smoke tests. Matrix and stateful requests share one response-draining loop.
Configuration cases alter one setting in one valid INI fixture and identify
the setting on failure.

Some overlap is intentional. Writer tests force buffers too small for a header
and check exact encodings. Connection tests inspect callback order directly.
Worker tests force thread migration and reject concurrent work on one channel.
Those properties are not guaranteed to occur in a live-server run. Keep these
tests rather than replacing them with another successful HTTP request.

## When a test fails

CTest prints the failed test's output. Its complete log is
`<build-directory>/Testing/Temporary/LastTest.log`. Run the matching executable
directly or use `ctest --test-dir <build-directory> -V -R '^test_name$'` for
verbose output. The live-server harness names the failing JPP matrix case and
prints its retained temporary directory for inspecting files and server logs.
Successful live runs remove their temporary files.

Source-vector failures name the file and expected outcome. Follow
[the source-model guide](../spec/README.md#when-a-vector-fails) to investigate
the model, manifest and server together. Do not hand-edit generated vectors or
labels. The [coverage map](../spec/COVERAGE.md) records the tested rules and
intentional boundaries. Regeneration is a separate Docker workflow needed only
when the model or corpus generator changes; it also regenerates
`lib/generated/`.

## What a passing suite does not establish

These tests are deterministic correctness checks, not a performance benchmark
or a general JPEG 2000 decoder conformance suite. Synthetic packets are not
entropy-decoded. Release qualification still needs Debian 13, a separate
Valgrind run on a non-sanitized build, representative warm/cold workloads, and
a real JHV movie with completion, output and logs checked. Do not run Valgrind
on the sanitizer build or infer production throughput from CTest timings.
