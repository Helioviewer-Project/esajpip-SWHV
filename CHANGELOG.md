# Changelog

## 1.9.0-rc1 - 2026-09-14

### Changed

- Partition top-level JPX association contents into separate metadata bins using
  JPIP placeholders.
- Coalesce contiguous contributions to the same data-bin within the existing
  response buffer.
- Continue linked-JPX metadata traversal across response chunks.
- Release linked-file mappings after indexing and after completing each
  response.
- Send HTTP chunks with partial-write-safe vectored I/O.
- Store linked JPX paths in codestream order and simplify cached-file lookup and
  URL handling.
- Modernize the CMake build and add focused protocol tests.
- Remove unused request state, dependencies, and packet-index code.

### Fixed

- Accept a JPIP cache-model descriptor at the end of a query string.
- Treat an orderly client disconnect as normal EOF while retaining distinct
  diagnostics for read errors, incomplete request lines, and invalid requests.
