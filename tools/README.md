# Command support

`hv_file.c` and `hv_file.h` provide atomic output replacement for `hv_merge`
and `hv_transcode`. CMake compiles them directly into each command and the
output-file test. They are not part of `jpeg2000_io`.

This helper owns process-wide signal cleanup and allows one active output.
It also briefly changes the process umask when reading it. These policies
belong to the commands; library callers supply their own output and lifecycle.
