#!/bin/sh
# The hv_merge command: options as hvJP2K's, exit status, and nothing
# written or left behind on failure.
#   cli_test.sh HV_MERGE MERGE_FIXTURES TRANSCODE_FIXTURES WORK
set -eu
exe=$1
fixtures=$2
kakadu=$3/kakadu
work=$4
rm -rf "$work"
mkdir -p "$work"
expected="$fixtures/expected/merged.jpx"
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

rest="$kakadu/2015_12_21__00_10_34_34__SDO_AIA_AIA_171.jp2 $kakadu/solo_fsi174_127x129_RLCP_PLT.jp2 \
$kakadu/solo_fsi174_509x513_PCRL.jp2 $kakadu/solo_fsi174_510x514_LRCP.jp2 \
$kakadu/solo_fsi174_511x513_LRCP_PLT.jp2 $kakadu/synthetic_rgb_129x129_CPRL_SOP_EPH.jp2"

[ "$(status "$exe")" = 2 ] || fail "no arguments: expected exit status 2"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2")" = 2 ] || fail "no -o: expected 2"
[ "$(status "$exe" -o "$work/out.jpx")" = 2 ] || fail "no -i: expected 2"
[ "$(status "$exe" -q -i "$fixtures/input/swap_000.jp2" -o "$work/out.jpx")" = 2 ] ||
    fail "unknown option: expected 2"
[ "$(status "$exe" -i , -o "$work/out.jpx")" = 2 ] || fail "only commas: expected 2"
[ ! -e "$work/out.jpx" ] || fail "a usage error wrote the output"

# Comma and space separated names, stray commas included.
# shellcheck disable=SC2086
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2,,$fixtures/input/swap_001.jp2," $rest \
    -o "$work/out.jpx")" = 0 ] || fail "merge failed: $(cat "$work/stderr")"
cmp -s "$work/out.jpx" "$expected" || fail "the output differs from hvJP2K's"

# Arguments from a file, quoted, after those of the command line.
{
    printf -- "-i '%s' \"%s\"" "$fixtures/input/swap_000.jp2" "$fixtures/input/swap_001.jp2"
    for f in $rest; do printf ' %s' "$f"; done
    printf '\n-o %s\n' "$work/argfile.jpx"
} >"$work/args"
[ "$(status "$exe" -s "$work/args")" = 0 ] || fail "-s failed: $(cat "$work/stderr")"
cmp -s "$work/argfile.jpx" "$expected" || fail "-s gives other bytes"

# An argument file read only in part must not merge the part: a read
# error (a directory) and a NUL byte (which would end the text) fail.
mkdir "$work/argdir"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -o "$work/partial.jpx" -s "$work/argdir")" = 1 ] ||
    fail "-s with a read error: expected 1"
printf -- '-i %s\0%s\n' "$fixtures/input/swap_000.jp2" "$fixtures/input/swap_001.jp2" \
    >"$work/nul-args"
[ "$(status "$exe" -o "$work/partial.jpx" -s "$work/nul-args")" = 1 ] ||
    fail "-s with a NUL byte: expected 1"
[ ! -e "$work/partial.jpx" ] || fail "a partly read -s file gave a merge"
rmdir "$work/argdir"

[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -o "$work/links.jpx" -links)" = 0 ] ||
    fail "-links failed: $(cat "$work/stderr")"
[ -s "$work/links.jpx" ] || fail "-links wrote nothing"

# Failures: exit status 1, a message naming the input, no output, no
# temporary file.
head -c 2000 "$fixtures/input/swap_000.jp2" >"$work/cut.jp2"
[ "$(status "$exe" -i "$work/cut.jp2" -o "$work/cut.jpx")" = 1 ] || fail "truncated file: expected 1"
grep -q "cut.jp2" "$work/stderr" || fail "the message does not name the input"
[ ! -e "$work/cut.jpx" ] || fail "a failed merge wrote its output"
[ "$(status "$exe" -i "$work/missing.jp2" -o "$work/missing.jpx")" = 1 ] ||
    fail "missing file: expected 1"
[ "$(status "$exe" -i "$3/input/synthetic_rgb_129x129_origin129_CPRL.jp2" -o "$work/origin.jpx")" = 1 ] ||
    fail "nonzero origins: expected 1"
grep -q "siz.zero-origin" "$work/stderr" || fail "nonzero origins: the message does not name the rule"
leftover=$(ls "$work" | grep -c '\.jpx\.' || true)
[ "$leftover" = 0 ] || fail "temporary files left behind: $(ls "$work")"

rm -rf "$work"
[ "$failures" = 0 ] || exit 1
echo "hv_merge command: all checks passed"
