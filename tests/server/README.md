# Server tests

These tests exercise the server adapters and lifecycle. Pure JPIP algorithms
are tested independently under [tests/jpip](../jpip/README.md).

| CTest test | Responsibility |
| --- | --- |
| `server_storage` | FileManager acceptance against the corpus, linked sources, real mapped-source responses, deferred packet errors, stable acquisitions, release, reacquisition and failed-open retry |
| `server_engine` | ChannelEngine thread migration and byte-equivalent plain/gzip output, including one-byte compressed output buffers |
| `server_worker` | Serialized worker operations, deterministic queued cancellation, completion after an open has started, cleanup and reuse |
| `server_http` | Every split point and bytewise HTTP parsing, pipelined bytes left unconsumed, parser reset, Host/body rules and exact line/head limits |
| `server_connection` | Direct libuv connection callbacks, ordered writes, read/write deadlines and graceful closure |
| `server` | Live HTTP/JPIP routing, source acceptance, limits, independent bin reconstruction, gzip, cache continuation and shutdown while a worker is blocked |
| `server_overlap` | Live active/waiting/busy exchanges, both disconnect paths, cache preservation and generation failure before/after HTTP headers |
| `server_support`, `logging` | Configuration/address/storage support and logging behavior |

The former `jpeg2000` server test is now `server_storage`. Its file and response
adapter assertions remain intact. Engine and worker assertions moved into their
own executables. Progression bijection checks moved to `jpip_packet_layout`.
Parser-only assertions moved out of the connection tests into `server_http`.

`channel_fixture.h` copies a checked-in source into a temporary directory for
the two channel adapter tests. `server_fixture.h` shares the live-server and
independent HTTP/JPP decoding harness. The JPP oracle remains independent of
production encoder code and checks exact source bytes, offsets and final flags.

The overlap test uses one source with 8 MiB of opaque metadata to keep a response
active while its client does not drain the socket. It does not model a JHV movie
request pattern or benchmark response generation. Server request logs synchronize
placement of the waiting exchange. No fixed sleep determines which client won
that slot. The shutdown case waits for the stop log before releasing a blocked
open. Queued worker cancellation occupies the two libuv workers with barriers.

## Run

From the repository root:

```sh
cmake -S . -B build/server-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/server-tests --target server_tests jpip_tests -j8
ctest --test-dir build/server-tests -L '^(server|jpip)$' --output-on-failure -j8
ctest --test-dir build/server-tests -L '^server_channel$' --output-on-failure
ctest --test-dir build/server-tests -R '^server_overlap$' --output-on-failure
ESAJPIP_TEST_TARGETS=server_tests ./tests/run.sh sanitize -L '^server$'
```

Group labels are `server_storage`, `server_channel`, `server_http`,
`server_live` and `server_support`. Every test also has `server`. `server_tests`
builds only these tests and their dependencies. The live tests and direct
connection test share a CTest resource lock. Normal focused tests have a
30-second timeout, and live tests have 90 seconds. The live harness has an
81-second deadline that kills its child server process groups on failure.

These are deterministic regression checks, not exhaustive protocol conformance
or representative JHV performance measurements. Platform-specific qualification
still needs Linux runs and real JHV playback/zoom with overlapping requests.

`-DESAJPIP_TEST_TIMEOUT=seconds` overrides these CTest deadlines. The mutation
runner passes its `ESAJPIP_MUTATION_TIMEOUT` through this setting, so the new
normal-test limits do not override campaign deadlines. The live harness uses
90% of its CTest timeout to clean up children before CTest kills the test.
