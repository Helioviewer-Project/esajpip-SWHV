# Changelog

## 1.9.0-rc2 - 2026-09-15

### Changed

- Reduce retained and transient per-client state when indexing large linked-JPX
  targets.
- Stream gzip responses with zlib instead of buffering each compressed response.
- Screen new connections before allocating a serving thread or JPEG 2000 state.
- Keep a JPIP channel available when its HTTP connection is replaced, while
  retaining JPEG 2000 and cache state in the channel's serving thread.
- Update the bundled log4cpp to 1.1.6.
- Replace bundled libconfig with GLib-based INI configuration parsing.
- Use standard CMake install directories and per-target compiler warnings.

### Fixed

- Implement PCRL and CPRL packet ordering for origin-zero, single-tile
  codestreams with unit component sampling, and reject other PCRL/CPRL
  geometries.
- Reject multi-tile codestreams instead of flattening their tile-parts into the
  single-tile packet model.
- Validate JPEG 2000 marker, tile-part, packet, box, and codestream bounds before
  indexing or copying data.
- Validate linked-JPX fragment lists, data references, and external ranges.
- Reject invalid cache-model lengths, response limits, and codestream selectors.
- Avoid invalid scaling for empty frame sizes.
- Use codestream zero for window requests without an explicit selector.
- Allow JPIP messages and metadata placeholders to exactly fill the configured
  response buffer.
- Accept valid HTTP header whitespace and reject incomplete header blocks.
- Track client completion by stable connection identifiers instead of reusable
  file descriptor numbers.
- Expire inactive JPIP channels whether or not an HTTP connection remains
  attached.
- Fix mapped-file cleanup, address resolution, client-thread failure handling,
  platform-specific alignment, and macOS system queries.

## 1.9.0-rc1 - 2026-09-14

### Changed

- Partition top-level JPX association contents into separate metadata bins using
  JPIP placeholders.
- Coalesce contiguous contributions to the same data-bin within the existing
  response buffer.
- Release linked-file mappings after indexing and after completing each
  response.
- Modernize the CMake build.

### Fixed

- Preserve linked-JPX codestream order when resolving frame URLs.
- Continue linked-JPX metadata traversal across response chunks.
- Handle partial socket writes when sending chunked responses.
- Accept a JPIP cache-model descriptor at the end of a query string.
