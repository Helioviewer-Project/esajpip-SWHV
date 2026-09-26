#!/bin/sh
# The hv_transcode command: options, exit status, in-place replacement and
# nothing left behind on failure.
#   cli_test.sh HV_TRANSCODE FIXTURES WORK
set -eu
exe=$1
fixtures=$2
work=$3
rm -rf "$work"
mkdir -p "$work"
input="$fixtures/input/solo_fsi174_127x129_RLCP_PLT.jp2"
failures=0

fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}

status() {
    set +e
    "$@" >"$work/stdout" 2>"$work/stderr"
    code=$?
    set -e
    echo $code
}

mode() {
    ls -l "$1" | cut -c2-10
}

[ "$(status "$exe")" = 2 ] || fail "no arguments: expected exit status 2"
[ "$(status "$exe" -q "$input" "$work/out.jp2")" = 2 ] || fail "unknown option: expected 2"
[ "$(status "$exe" -p 100,128 "$input" "$work/out.jp2")" = 2 ] || fail "-p 100,128: expected 2"
[ "$(status "$exe" -p 1,128 "$input" "$work/out.jp2")" = 2 ] || fail "-p 1,128: expected 2"
[ ! -e "$work/out.jp2" ] || fail "a usage error wrote the output"

[ "$(status "$exe" -x "$input" "$work/out.jp2")" = 0 ] || fail "transcoding the fixture failed: $(cat "$work/stderr")"

# In place: same bytes as to another file, same mode.
cp "$input" "$work/in-place.jp2"
chmod 640 "$work/in-place.jp2"
[ "$(status "$exe" -x "$work/in-place.jp2" "$work/in-place.jp2")" = 0 ] || fail "in place failed"
cmp -s "$work/in-place.jp2" "$work/out.jp2" || fail "in place gives other bytes"
[ "$(mode "$work/in-place.jp2")" = "rw-r-----" ] || fail "in place changed the mode to $(mode "$work/in-place.jp2")"

# Failures: exit status 1, a message naming the input, no output, no
# temporary file.
head -c 2000 "$input" >"$work/cut.jp2"
[ "$(status "$exe" "$work/cut.jp2" "$work/cut-out.jp2")" = 1 ] || fail "truncated file: expected 1"
grep -q "cut.jp2" "$work/stderr" || fail "the message does not name the input"
[ ! -e "$work/cut-out.jp2" ] || fail "a failed transcode wrote its output"
: >"$work/empty.jp2"
[ "$(status "$exe" "$work/empty.jp2" "$work/empty-out.jp2")" = 1 ] || fail "empty file: expected 1"
[ "$(status "$exe" "$work/missing.jp2" "$work/missing-out.jp2")" = 1 ] || fail "missing file: expected 1"
# Outside the served profile.
[ "$(status "$exe" "$fixtures/input/synthetic_rgb_129x129_origin129_CPRL.jp2" "$work/origin-out.jp2")" = 1 ] ||
    fail "nonzero origins: expected 1"
grep -q "siz.zero-origin" "$work/stderr" || fail "nonzero origins: the message does not name the rule"
[ ! -e "$work/origin-out.jp2" ] || fail "a rejected input wrote its output"
leftover=$(ls "$work" | grep -c '\.jp2\.' || true)
[ "$leftover" = 0 ] || fail "temporary files left behind: $(ls "$work")"

rm -rf "$work"
[ "$failures" = 0 ] || exit 1
echo "hv_transcode command: all checks passed"
