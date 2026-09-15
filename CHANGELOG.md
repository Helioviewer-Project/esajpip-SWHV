# Changelog

## 1.9.0-rc2 - 2026-09-15

### Changed

- Reduce per-client linked-JPX indexing state and transient allocations.
- Stream gzip responses with zlib instead of buffering the compressed response.
- Simplify request, response, socket, data-bin, and image-index ownership.
- Update the bundled log4cpp to 1.1.6 and libconfig to 1.8.2.
- Add focused JPEG 2000 and JPX parser regression tests.

### Fixed

- Implement PCRL and CPRL packet ordering for origin-zero, single-tile
  codestreams with unit component sampling; reject other PCRL/CPRL geometries.
- Reject multi-tile codestreams instead of flattening their tile-parts into the
  single-tile packet model.
- Validate JPEG 2000 marker, tile-part, packet, box, and codestream bounds.
- Validate linked-JPX fragment lists, data references, and external ranges.
- Reject invalid cache-model lengths, response limits, and codestream selectors.
- Handle mapped-file and client-thread failures without invalid access or stale
  connections.
- Correct platform-specific alignment, address-resolution, and system-query
  errors.

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
