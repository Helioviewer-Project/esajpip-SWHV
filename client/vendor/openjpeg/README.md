# Vendored OpenJPEG

The JPEG 2000 library of [OpenJPEG](https://www.openjpeg.org/) 2.5.4,
released in September 2025: the decoder the client uses. It is kept here so
that the client, and its WebAssembly build, do not depend on whichever
OpenJPEG version a system packages.

- Source: <https://github.com/uclouvain/openjpeg>, tag `v2.5.4`, commit
  `6c4a29b00211eb0430fa0e5e890f1ce5c80f409f`.
- The 22 `.c` and 29 `.h` files are those of `src/lib/openjp2/` that upstream
  builds into `libopenjp2` without its JPIP option, unmodified.
- `opj_config.h` and `opj_config_private.h` are not upstream files: upstream
  generates them with CMake. They are written by hand here.
- `LICENSE` is from the upstream root. OpenJPEG is distributed under the
  2-clause BSD license.

The `esajpip_client_wasm` target compiles it for WebAssembly or native decoding
tests. The core `esajpip_client` target does not depend on it.
`opj_clock.c` is not compiled: the library does not call it, and it does not
build for WebAssembly.

## Updating

Do not edit the upstream files here. To change version, replace them
wholesale from a release tag, refresh `LICENSE`, and update the version in
the two configuration headers and the version, date and commit above. To check
a copy against an upstream checkout:

```sh
for f in client/vendor/openjpeg/*.[ch]; do
    case $f in */opj_config*.h) continue;; esac
    cmp $f /path/to/openjpeg/src/lib/openjp2/$(basename $f)
done
```
