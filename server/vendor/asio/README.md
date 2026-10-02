# Vendored Asio

Standalone [Asio](https://think-async.com/Asio/) 1.38.2, released 19 July
2026: the header-only networking library the server is built on. It is kept
here so that the server does not depend on whichever Asio version a system
packages.

- Source: <https://github.com/chriskohlhoff/asio>, tag `asio-1-38-2`,
  commit `8806a6803cde7054c3049d3666d3ec36786568c5`.
- `include/` is the upstream `include/` directory without `Makefile.am` and
  `.gitignore`. The 634 header files are unmodified.
- `LICENSE_1_0.txt` and `COPYING` are from the upstream root. Asio is
  distributed under the Boost Software License 1.0.

Only `server/` uses it, through the `esajpip_asio` CMake target, which adds
`include/` as a system include directory.

## Updating

Do not edit files here. To change version, replace `include/asio.hpp` and
`include/asio/` wholesale from a release tag, refresh the two license files,
and update the version, date and commit above. To check a copy against an
upstream checkout:

```sh
diff -r /path/to/asio/include/asio server/vendor/asio/include/asio
cmp /path/to/asio/include/asio.hpp server/vendor/asio/include/asio.hpp
```
