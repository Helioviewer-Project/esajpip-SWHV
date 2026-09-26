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

rest="$kakadu/2015_12_21__00_10_34_34__SDO_AIA_AIA_171.jp2 \
$kakadu/solo_fsi174_127x129_RLCP_PLT.jp2 \
$kakadu/solo_fsi174_509x513_PCRL.jp2 $kakadu/solo_fsi174_510x514_LRCP.jp2 \
$kakadu/solo_fsi174_511x513_LRCP_PLT.jp2 $kakadu/synthetic_rgb_129x129_CPRL_SOP_EPH.jp2"

[ "$(status "$exe")" = 2 ] || fail "no arguments: expected exit status 2"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2")" = 2 ] || fail "no -o: expected 2"
[ "$(status "$exe" -o "$work/out.jpx")" = 2 ] || fail "no -i: expected 2"
[ "$(status "$exe" -q -i "$fixtures/input/swap_000.jp2" -o "$work/out.jpx")" = 2 ] ||
    fail "unknown option: expected 2"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -x.jp2 -o "$work/out.jpx")" = 2 ] ||
    fail "a name starting with - after -i: expected 2"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -o "$work/out.jpx" --)" = 2 ] ||
    fail "--: expected 2"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -o "$work/out.jpx" -links=yes)" = 2 ] ||
    fail "-links=yes: expected 2"
[ ! -e "$work/out.jpx" ] || fail "a usage error wrote the output"
[ "$(status "$exe" -q -h)" = 0 ] && grep -q "^usage: hv_merge" "$work/stdout" ||
    fail "-h: expected the usage on standard output and exit status 0"
# No names left once the commas are dropped: hvJP2K's error.
[ "$(status "$exe" -i , -o "$work/out.jpx")" = 1 ] && grep -q "no JP2 input files" "$work/stderr" ||
    fail "only commas: expected 1 and \"no JP2 input files\""
[ ! -e "$work/out.jpx" ] || fail "a failed merge wrote the output"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -o "")" = 1 ] || fail "-o \"\": expected 1"

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

# As argparse reparses: a later -i replaces an earlier one, the command
# line's included; a -s in the file is parsed but not read.
{
    printf -- "-i %s" "$fixtures/input/swap_000.jp2,$fixtures/input/swap_001.jp2"
    for f in $rest; do printf ' %s' "$f"; done
    printf ' -s %s\n' "$work/missing-args"
} >"$work/later-args"
[ "$(status "$exe" -i "$fixtures/input/swap_001.jp2" -o "$work/later.jpx" \
    -s "$work/later-args")" = 0 ] ||
    fail "-i and -s in the argument file: $(cat "$work/stderr")"
cmp -s "$work/later.jpx" "$expected" || fail "-i in the argument file: other bytes"
# Only space, tab, line feed and carriage return separate words, as in
# shlex: "-links" and a form feed make an unknown option.
printf -- '-i %s -o %s -links\f\n' "$fixtures/input/swap_000.jp2" "$work/ff.jpx" >"$work/ff-args"
[ "$(status "$exe" -s "$work/ff-args")" = 2 ] || fail "a form feed in the argument file: expected 2"
[ ! -e "$work/ff.jpx" ] || fail "a form feed in the argument file: the output was written"
# Bare names in the file continue a final -i of the command line.
for f in $rest; do printf '%s\n' "$f"; done >"$work/names"
[ "$(status "$exe" -o "$work/bare.jpx" -s "$work/names" -i "$fixtures/input/swap_000.jp2" \
    "$fixtures/input/swap_001.jp2")" = 0 ] || fail "bare names in the file: $(cat "$work/stderr")"
cmp -s "$work/bare.jpx" "$expected" || fail "bare names in the file: other bytes"
# Values that start with -: "-" alone and negative numbers; attached values
# and abbreviations.
ln -s "$fixtures/input/swap_000.jp2" "$work/-1"
(cd "$work" && "$exe" -i -1 -o neg.jpx 2>/dev/null) || fail "-i -1: a file named -1 not merged"
[ "$(status "$exe" "-i$work/-1" "-o$work/attached.jpx" -lin)" = 0 ] ||
    fail "-iNAME -oNAME -lin: $(cat "$work/stderr")"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -o "$work/full.jpx" -links)" = 0 ] &&
    cmp -s "$work/attached.jpx" "$work/full.jpx" || fail "-iNAME -oNAME -lin: other bytes"
rm -f "$work/-1" "$work/neg.jpx" "$work/attached.jpx" "$work/full.jpx" "$work/bare.jpx" \
    "$work/later.jpx"

# An argument file read only in part must not merge the part: a read
# error (a directory) and a NUL byte (which would end the text) fail.
mkdir "$work/argdir"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -o "$work/partial.jpx" \
    -s "$work/argdir")" = 1 ] ||
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

# Arguments from standard input.
[ "$(status "$exe" -s - <"$work/args")" = 0 ] || fail "-s - failed: $(cat "$work/stderr")"
cmp -s "$work/argfile.jpx" "$expected" || fail "-s - gives other bytes"

# An existing output keeps its mode; one that is a symbolic link keeps the
# link and gets its target replaced.
chmod 600 "$work/links.jpx"
ln -s links.jpx "$work/link.jpx"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -o "$work/link.jpx" -links)" = 0 ] ||
    fail "-o through a link failed: $(cat "$work/stderr")"
[ -L "$work/link.jpx" ] || fail "the link was replaced by a file"
[ "$(ls -l "$work/links.jpx" | cut -c2-10)" = "rw-------" ] ||
    fail "the output's mode changed to $(ls -l "$work/links.jpx" | cut -c2-10)"
# A link to a file not there yet: the file is created, the link stays.
ln -s new.jpx "$work/new-link.jpx"
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -o "$work/new-link.jpx" -links)" = 0 ] ||
    fail "-o through a dangling link failed: $(cat "$work/stderr")"
[ -L "$work/new-link.jpx" ] && [ -f "$work/new.jpx" ] ||
    fail "a dangling link was not written through"
rm -f "$work/new-link.jpx" "$work/new.jpx"

# An output that cannot be created: the message names it, not the
# temporary file.
[ "$(status "$exe" -i "$fixtures/input/swap_000.jp2" -o "$work/no-dir/out.jpx")" = 1 ] ||
    fail "no directory: expected 1"
grep -q "cannot create $work/no-dir/out.jpx: " "$work/stderr" &&
    ! grep -q 'out\.jpx\.' "$work/stderr" ||
    fail "no directory: $(cat "$work/stderr")"

# Failures: exit status 1, a message naming the input, no output, no
# temporary file.
head -c 2000 "$fixtures/input/swap_000.jp2" >"$work/cut.jp2"
[ "$(status "$exe" -i "$work/cut.jp2" -o "$work/cut.jpx")" = 1 ] ||
    fail "truncated file: expected 1"
grep -q "cut.jp2" "$work/stderr" || fail "the message does not name the input"
[ ! -e "$work/cut.jpx" ] || fail "a failed merge wrote its output"
[ "$(status "$exe" -i "$work/missing.jp2" -o "$work/missing.jpx")" = 1 ] ||
    fail "missing file: expected 1"
[ "$(status "$exe" -i "$3/input/synthetic_rgb_129x129_origin129_CPRL.jp2" \
    -o "$work/origin.jpx")" = 1 ] ||
    fail "nonzero origins: expected 1"
grep -q "siz.zero-origin" "$work/stderr" ||
    fail "nonzero origins: the message does not name the rule"
leftover=$(ls "$work" | grep -c '\.jpx\.' || true)
[ "$leftover" = 0 ] || fail "temporary files left behind: $(ls "$work")"

rm -rf "$work"
[ "$failures" = 0 ] || exit 1
echo "hv_merge command: all checks passed"
