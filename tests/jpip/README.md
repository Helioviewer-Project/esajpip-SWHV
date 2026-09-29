# JPIP library tests

Every executable here links `jpip` and its `jpeg2000` dependency. No test uses
server headers, file-mapping adapters, sockets, HTTP, gzip or server logging.
The index/session tests load checked-in fixtures into memory. The other tests
construct their inputs directly.

| Directory | CTest tests | Owns |
| --- | --- | --- |
| `source/` | `jpip_source` | Absolute reads, end boundaries and unchanged destinations on failed reads |
| `index/` | `index_library`, `jpip_packet_layout` | Corpus indexing, deferred errors, access order, progression coordinates, resolution selection and compact-index bounds |
| `request/` | `jpip_request` | JPIP syntax, selectors, cache-model descriptors and routing-field values |
| `response/` | `jpip_cache`, `jpip_window`, `jpip_writer` | Cache state, precinct selection, exact JPP bytes, coalescing, buffer limits and placeholders |
| `response/` | `jpip_library` | Stateful responses: independently reconstructed bins, source failures, remapping, budgets, repeated requests and viewport-to-zoom reuse |

`tests/jpeg2000/test_geometry.c` checks the C format library against independent
standard-derived geometry. `index/test_packet_layout.cc` checks the C++
`CodingParameters` and compact `PacketIndex` contracts used for serving.
`index/test_index.cc` also compares the two implementations and checks that
packet access order does not change indexed ranges or deferred errors.

The session tests deliberately combine layers of the library. They complement
the focused tests. File mapping, HTTP extraction, channel lifecycle, gzip and
worker scheduling remain integration tests under `tests/server/`.

## Build and select

From the repository root:

```sh
cmake -S . -B build/jpip-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/jpip-tests --target jpip_tests -j8
ctest --test-dir build/jpip-tests -L '^jpip$' --output-on-failure
ctest --test-dir build/jpip-tests -L '^jpip_response$' --output-on-failure
ctest --test-dir build/jpip-tests -R '^jpip_writer$' --output-on-failure
```

`jpip_tests` builds only these tests and their library dependencies. The project
configuration still discovers the server's development packages. Group labels
are `jpip_source`, `jpip_index`, `jpip_request` and `jpip_response`; every test
also keeps the `tools` label used by the existing runners.

The same suite supports ASan/UBSan via `-DESAJPIP_SANITIZE=ON` in a separate
build, or through the common runner:

```sh
ESAJPIP_TEST_TARGETS=jpip_tests ./tests/run.sh sanitize -L '^jpip$'
```

## Add a test

Place focused tests beside the responsibility they exercise. Add one entry to
`tests/jpip/CMakeLists.txt`, for example:

```cmake
jpip_add_test(test_jpip_selection jpip_selection request request/test_selection.cc)
```

This registers the executable, compiler/sanitizer settings, library link,
CTest labels and `jpip_tests` dependency. Add fixture definitions only when the
test reads the corpus. There is no test framework dependency or server fixture
required to add a library case.

Use explicit expected bytes, coordinates and state transitions. Preserve the
independent JPP decoder in the session tests; using the production writer as
the expected-result generator would hide matching errors. Keep malformed inputs
and failure diagnostics when extending coverage. Introduce shared fixtures
when multiple test files actually need them.

Useful next extensions include numeric/escape/duplicate-field tables in
`request/`, VBAS and header-size boundaries in `response/test_writer.cc`, and
interleaved independent response sessions with controlled source failures in
`response/test_session.cc`. These are extension points, not claims of new
coverage from the directory reorganization.
