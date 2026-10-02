/* opj_config_private.h: what OpenJPEG's CMake generates from
 * opj_config_private.h.cmake.in, written by hand for the vendored copy (see
 * README.md): macOS, Linux and WebAssembly with a WASI libc all have
 * posix_memalign, and files are not read through OpenJPEG. */
#define OPJ_PACKAGE_VERSION "2.5.4"

#define OPJ_HAVE_POSIX_MEMALIGN

#if !defined(_POSIX_C_SOURCE)
/* Get the declaration of posix_memalign. */
#define _POSIX_C_SOURCE 200112L
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define OPJ_BIG_ENDIAN
#endif
