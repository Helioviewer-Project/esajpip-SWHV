#!/bin/sh
# check-model.sh: checks the model in spec/ against the code that restates
# it, then generates the complete model with the pinned asn1scc, builds it
# as strict C11 with AddressSanitizer and UndefinedBehaviorSanitizer, and
# runs the corpus harness, which fails on any vector whose label or reason
# is not the one its mutant expects.
#
#   ASN1SCC_IMAGE=image spec/check-model.sh [corpus-directory]
#   spec/check-model.sh --static
#
# The static checks (--static runs only these; they need sh, sed and awk):
#   * each box-type mapping function of harness/mapping.c keeps exactly
#     the TBox values of the CHOICEs it serves, and every `other`
#     alternative is MAPPING_OTHER_BOX, a type no mapping keeps;
#   * lib/hv_codes.h: every constant is written HV_<NAME> = 0x<hex> (but
#     the sizes listed below), and is the value the model states for the
#     field of that name;
#   * each marker-code value set selects exactly one field of its segment
#     SEQUENCE for every code in it;
#   * the ACN encodings the model restates (profile types, -Std types,
#     parameterized instances, the box wrappers) and the ASN.1 types it
#     copies (Cod-Profile, SizSegment-Std, CodSegment-Std, Rreq-Std) agree;
#   * the profile limits restated in C: the tile-part limit
#     (HV_PROFILE_TILE_PARTS) and the dimension limit of the harness
#     (PROFILE_MAX_DIMENSION).
# The compiler checks run lib/generate.sh --check with the same Docker
# image as the corpus, whatever ASN1SCC says, so that lib/generated and the
# corpus come from one compiler.
#
# Assumptions about the layout of the model files: a type assignment starts
# at the beginning of a line with its name, and an ACN type's `{` is on the
# line of its name.

set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=${ASN1SCC_IMAGE:-esajpip-asn1scc}

usage() {
    echo "usage: ASN1SCC_IMAGE=image $0 [corpus-directory]" >&2
    echo "       $0 --static" >&2
    exit 2
}

static=0
if [ "${1:-}" = --static ]; then
    static=1
    shift
    [ "$#" -eq 0 ] || usage
fi
[ "$#" -le 1 ] || usage

temporary=$(mktemp -d "${TMPDIR:-/tmp}/esajpip-model.XXXXXX")
trap 'rm -rf "$temporary"' EXIT
trap 'exit 1' HUP INT TERM

modules=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$repo/spec/modules")

fail() {
    echo "$*" >&2
    exit 1
}

# The ACN encoding of a type, on one line, without its name and comments:
# up to the `]` that closes its properties or, where it has children, the
# `}` that closes them.
acn_body() {
    awk -v name="$1" '
        function count(s, c,    t) { t = s; return gsub(c, "", t) }
        !on && ($1 == name || index($1, name "<") == 1) && $0 ~ /[[{<]/ { on = 1; first = 1 }
        on {
            line = $0
            sub(/--.*/, "", line)
            if (first) { sub("^[[:space:]]*" name, "", line); first = 0 }
            text = text " " line
            braces += count(line, "[{]") - count(line, "[}]")
            brackets += count(line, "[[]") - count(line, "[]]")
            if (line ~ /[{]/) opened = 1
            if (line ~ /[]]/) closed = 1
            if ((opened && braces == 0) || (!opened && closed && brackets == 0)) exit
        }
        END {
            gsub(/[[:space:]]+/, " ", text)
            sub(/^ /, "", text)
            sub(/ $/, "", text)
            print text
        }
    ' "$repo"/spec/*.acn
}

# The ASN.1 definition of a type, after `::=`, on one line without comments.
asn1_body() {
    awk -v name="$1" '
        function count(s, c,    t) { t = s; return gsub(c, "", t) }
        !on && $1 == name && $2 == "::=" { on = 1; first = 1 }
        on {
            line = $0
            sub(/--.*/, "", line)
            if (first) { sub("^[[:space:]]*" name "[[:space:]]*::=", "", line); first = 0 }
            text = text " " line
            depth += count(line, "[{(]") - count(line, "[})]")
            if (depth == 0) exit
        }
        END {
            gsub(/[[:space:]]+/, " ", text)
            sub(/^ /, "", text)
            sub(/ $/, "", text)
            print text
        }
    ' "$repo"/spec/*.asn1
}

# The present-when TBox values of a CHOICE's ACN encoding, one per line;
# `other` gives "other <value>".
choice_values() {
    acn_body "$1" | awk '{
        t = $0
        while (match(t, /[A-Za-z0-9]+ \[present-when tbox == [0-9]+/)) {
            m = substr(t, RSTART, RLENGTH)
            t = substr(t, RSTART + RLENGTH)
            split(m, w, " ")
            print (w[1] == "other" ? "other " : "") w[5]
        }
    }'
}

# ---------------------------------------------------------------------------
# Box-type mapping functions (harness/mapping.c) against the CHOICEs they
# serve: a box wrapper's TBox determinant names the function, its `payload`
# field the CHOICE.
# ---------------------------------------------------------------------------
mapping_c=$repo/spec/harness/mapping.c
other=$(sed -n 's/^#define MAPPING_OTHER_BOX \([0-9]*\)u.*/\1/p' "$repo/spec/harness/mapping.h")
abcd=$(( (97 << 24) | (98 << 16) | (99 << 8) | 100 ))
[ "$other" = "$abcd" ] ||
    fail "mapping.h: MAPPING_OTHER_BOX is not 'abcd' ($abcd)"
! grep -q "$other" "$mapping_c" ||
    fail "mapping.c: write MAPPING_OTHER_BOX, not its value"
: > "$temporary/served"
for type in $(sed -n 's/^\([A-Z][A-Za-z0-9-]*\)[[:space:]].*/\1/p' "$repo"/spec/*.acn); do
    function=$(acn_body "$type" |
        sed -n 's/.*mapping-function \([a-z0-9]*boxtype\)\].*/\1/p')
    [ -n "$function" ] || continue
    choice=$(asn1_body "$type" | sed -n 's/.*[{,] payload \([A-Za-z0-9-]*\).*/\1/p')
    [ -n "$choice" ] || fail "no payload CHOICE in $type"
    echo "$function $choice" >> "$temporary/served"
done
for choice in $(sed -n 's/^\([A-Z][A-Za-z0-9-]*\)[[:space:]]*<[A-Za-z-]*:tbox.*/\1/p' \
                    "$repo"/spec/*.acn); do
    grep -q " $choice\$" "$temporary/served" ||
        fail "no box wrapper with a box-type mapping function selects $choice"
    for value in $(choice_values "$choice" | sed -n 's/^other //p'); do
        [ "$value" = "$other" ] || fail "$choice: other is $value, not MAPPING_OTHER_BOX"
    done
done
for function in $(cut -d ' ' -f 1 "$temporary/served" | sort -u); do
    expected=$(for choice in $(sed -n "s/^$function //p" "$temporary/served"); do
                   choice_values "$choice"
               done | grep -v '^other ' | sort -u)
    mapped=$(awk -v f="$function" '
        index($0, "MAPPING_DECODE_NAME(" f ")(") > 0 { on = 1; next }
        on && /^}/ { exit }
        on && match($0, /case [0-9]+u:/) { print substr($0, RSTART + 5, RLENGTH - 7) }
    ' "$mapping_c" | sort -u)
    if [ "$expected" != "$mapped" ]; then
        echo "mapping.c: $function keeps other types than the CHOICEs it serves:" \
            $(sed -n "s/^$function //p" "$temporary/served") >&2
        exit 1
    fi
    ! printf '%s\n' "$mapped" | grep -qx "$other" ||
        fail "mapping.c: $function keeps MAPPING_OTHER_BOX"
done

# ---------------------------------------------------------------------------
# lib/hv_codes.h against the model: the values the model states for the
# field of the same name (`present-when` values, fixed INTEGER fields and
# marker termination patterns). The C names the model does not state are
# listed in `unmodeled`, the constants that are not codes in `sizes`.
# ---------------------------------------------------------------------------
{
    sed -En 's/^[[:space:]]*([A-Za-z][A-Za-z0-9]*)[[:space:]]*\[present-when \(?(code|tbox) == ([0-9]+)[),].*/model \2 \1 \3 \3/p' \
        "$repo"/spec/*.acn
    sed -En 's/^[[:space:]]*([A-Za-z][A-Za-z0-9]*)[[:space:]]+INTEGER \(([0-9]+)\.\.([0-9]+)\).*/model code \1 \2 \3/p' \
        "$repo"/spec/*.asn1
    sed -En "s/^[[:space:]]*([A-Za-z][A-Za-z0-9]*)[[:space:]]*\[.*termination-pattern '(FF[0-9A-F]{2})'H.*/model code \1 0x\2 0x\2/p" \
        "$repo"/spec/*.acn
    sed 's/^/c /' "$repo/lib/hv_codes.h"
} | awk '
    function number(s,    n, i) {
        if (s !~ /^0x/)
            return s + 0
        n = 0
        for (i = 3; i <= length(s); i++)
            n = n * 16 + index("0123456789abcdef", tolower(substr(s, i, 1))) - 1
        return n
    }
    BEGIN {
        # Model field names that differ from the C name.
        alias["tilePart"] = "sot"; alias["sizCode"] = "siz"
        alias["segments"] = "eoc"  # a codestream ends at EOC
        alias["headers"] = "sod"   # a tile-part header ends at SOD
        # C names the model does not state.
        split("code:sop code:eph code:no_segment_first code:no_segment_last " \
              "tbox:cgrp tbox:comp tbox:drep tbox:j2cx brand:jp2 brand:jpx brand:jpxb", u, " ")
        for (i in u)
            unmodeled[u[i]] = 1
        # Constants that are sizes, not codes.
        split("BOX_HEADER BOX_HEADER_XL", s, " ")
        for (i in s)
            sizes[s[i]] = 1
    }
    $1 == "model" && $4 != $5 {
        next                # a range, not a fixed value
    }
    $1 == "model" {
        name = ($3 in alias) ? alias[$3] : tolower($3)
        k = $2 ":" name
        if (index(model[k] " ", " " number($4) " ") == 0)
            model[k] = model[k] " " number($4)
        next
    }
    {
        line = substr($0, 3)
        # Comments, which may span lines, and preprocessor lines.
        code = ""
        while (line != "") {
            if (comment) {
                if ((i = index(line, "*/")) == 0) { line = ""; break }
                line = substr(line, i + 2)
                comment = 0
            }
            if ((i = index(line, "/*")) == 0) { code = code line; break }
            code = code substr(line, 1, i - 1)
            line = substr(line, i + 2)
            comment = 1
        }
        if (code ~ /^[[:space:]]*#/)
            next
        while (match(code, /HV_[A-Za-z0-9_]+/)) {
            name = substr(code, RSTART + 3, RLENGTH - 3)
            code = substr(code, RSTART + RLENGTH)
            if (name in sizes)
                continue
            if (!match(code, /^[[:space:]]*=[[:space:]]*0x[0-9A-Fa-f]+[[:space:]]*([,}]|$)/)) {
                print "hv_codes.h: HV_" name " is not written HV_" name " = 0x<hex>" > "/dev/stderr"
                bad = 1
                continue
            }
            value = substr(code, RSTART, RLENGTH)
            code = substr(code, RSTART + RLENGTH)
            sub(/^[[:space:]]*=[[:space:]]*/, "", value)
            sub(/[[:space:]]*[,}]?$/, "", value)
            kind = "code"
            if (name ~ /^BOX_/) { kind = "tbox"; name = substr(name, 5) }
            else if (name ~ /^BRAND_/) { kind = "brand"; name = substr(name, 7) }
            c[kind ":" tolower(name)] = number(value)
        }
    }
    END {
        for (k in c) {
            if (k in unmodeled) {
                if (k in model) { print "hv_codes.h: " k " is in the model" > "/dev/stderr"; bad = 1 }
                continue
            }
            if (!(k in model)) { print "hv_codes.h: " k " is not in the model" > "/dev/stderr"; bad = 1; continue }
            n = split(model[k], v, " ")
            for (i = 1; i <= n; i++)
                if (v[i] != c[k]) {
                    printf "hv_codes.h: %s is %d, the model has %d\n", k, c[k], v[i] > "/dev/stderr"
                    bad = 1
                }
        }
        for (k in unmodeled)
            if (!(k in c)) { print "check-model.sh: unmodeled " k " is not in hv_codes.h" > "/dev/stderr"; bad = 1 }
        exit bad
    }
' || fail "lib/hv_codes.h differs from the model"

# ---------------------------------------------------------------------------
# Marker-code value sets: every code of the set selects exactly one field of
# the segment SEQUENCE (present-when `code == N`, or the `other` field's
# range and exclusions); in a layer-1 SEQUENCE, which has no `other`, no
# field is for a code outside the set.
# ---------------------------------------------------------------------------
check_markers() {
    { asn1_body "$1"; acn_body "$2"; } | awk -v set="$1" -v sequence="$2" '
        NR == 1 {
            s = $0
            sub(/^[^(]*[(]/, "", s)
            sub(/[)][^)]*$/, "", s)
            n = split(s, alternative, "|")
            for (i = 1; i <= n; i++) {
                a = alternative[i]
                gsub(/[[:space:]]/, "", a)
                if (split(a, range, /\.\./) == 2) {
                    for (code = range[1] + 0; code <= range[2] + 0; code++) value[code] = 1
                } else {
                    value[a + 0] = 1
                }
            }
            next
        }
        {
            t = $0
            while (match(t, /present-when [(]?code == [0-9]+/)) {
                m = substr(t, RSTART, RLENGTH)
                t = substr(t, RSTART + RLENGTH)
                sub(/.*== /, "", m)
                explicit[m + 0]++
            }
            if (match($0, /code >= [0-9]+ and code <= [0-9]+ and ![(][^)]*[)]/)) {
                o = substr($0, RSTART, RLENGTH)
                split(o, w, " ")
                low = w[3] + 0
                high = w[7] + 0
                sub(/^[^!]*![(]/, "", o)
                while (match(o, /[0-9]+/)) {
                    excluded[substr(o, RSTART, RLENGTH) + 0] = 1
                    o = substr(o, RSTART + RLENGTH)
                }
                other = 1
            }
        }
        END {
            for (c in value) {
                code = c + 0
                selected = c in explicit ? explicit[c] : 0
                selected += other && code >= low && code <= high && !(c in excluded)
                if (selected != 1) {
                    printf "%s: code %d selects %d fields of %s\n", set, code, selected,
                           sequence > "/dev/stderr"
                    bad = 1
                }
            }
            for (c in explicit)
                if (!other && !(c in value)) {
                    printf "%s: a field of %s is for code %d, outside %s\n", sequence,
                           sequence, c + 0, set > "/dev/stderr"
                    bad = 1
                }
            exit bad
        }
    ' || fail "$1 and the fields of $2 disagree"
}
check_markers MainMarkerCode MainSegment
check_markers TileMarkerCode TileSegment
check_markers MainMarkerCode-Profile MainSegment-Profile
check_markers TileMarkerCode-Profile TileSegment-Profile

# ---------------------------------------------------------------------------
# Repeated encodings and copied types. ACN is not inherited through WITH
# COMPONENTS or parameterization, so each instance restates its template's
# encoding; each group must agree. The box wrappers differ only in the name
# of their TBox determinant and its mapping function.
# ---------------------------------------------------------------------------
wrapper_body() {
    acn_body "$1" | awk '{
        t = $0
        if (match(t, / [A-Za-z0-9]+ BoxType \[/)) {
            d = substr(t, RSTART + 1, RLENGTH - 1)
            sub(/ .*/, "", d)
            gsub(" " d " ", " TYPE ", t)
            gsub("<" d ",", "<TYPE,", t)
        }
        gsub(/mapping-function [a-z0-9]*boxtype\]/, "mapping-function BOXTYPE]", t)
        print t
    }'
}
compare_group() {
    what=$1
    shift
    first=
    for type in "$@"; do
        case $what in
            acn) body=$(acn_body "$type") ;;
            wrapper) body=$(wrapper_body "$type") ;;
            asn1) body=$(asn1_body "$type") ;;
            asn1-bounds) body=$(asn1_body "$type" | sed 's/SIZE ([0-9.]*)/SIZE (n)/g') ;;
        esac
        [ -n "$body" ] || fail "check-model.sh: no $what definition of $type"
        if [ -z "$first" ]; then
            first=$body
            first_type=$type
        elif [ "$body" != "$first" ]; then
            fail "$what definition of $type differs from that of $first_type"
        fi
    done
}
compare_group acn Siz Siz-Profile
compare_group acn Cod Cod-Profile
compare_group acn QcdBody Qcd Qcd-Profile Qcd-Std
compare_group acn PltBody Plt Plt-Profile Plt-Std
compare_group acn ComBody Com Com-Profile Com-Std
compare_group acn SizSegment SizSegment-Profile SizSegment-Std CodSegment CodSegment-Profile \
    CodSegment-Std QcdSegment QcdSegment-Profile QcdSegment-Std PltSegment PltSegment-Profile \
    PltSegment-Std ComSegment ComSegment-Profile ComSegment-Std
compare_group acn Codestream Codestream-Profile
compare_group acn TilePart TilePart-Profile
compare_group acn TilePartRest TilePartRest-Profile
compare_group acn MainMarkerCode TileMarkerCode MainMarkerCode-Profile TileMarkerCode-Profile \
    MarkerCode
compare_group acn Jp2Family Jp2File-Profile JpxFile-Profile
compare_group acn Superbox Superbox-Profile Association Res
compare_group acn DataReferences DataReferences-Profile
compare_group acn TopPayload TopPayload-Profile
compare_group acn InnerPayload InnerPayload-Profile
compare_group acn DataEntryUrl DataEntryUrl-Profile
compare_group acn Rreq Rreq-Std
compare_group wrapper TopBox InnerBox ResBox Jp2Box-Profile TopBox-Profile InnerBox-Profile
compare_group asn1 Cod Cod-Profile
compare_group asn1 SizSegment SizSegment-Std
compare_group asn1 CodSegment CodSegment-Std
compare_group asn1-bounds Rreq Rreq-Std

# ---------------------------------------------------------------------------
# Profile limits restated in C.
# ---------------------------------------------------------------------------
# The tile-part limit: TilePart-Profile and HV_PROFILE_TILE_PARTS.
tile_part=$(asn1_body TilePart-Profile)
tnsot=$(printf '%s\n' "$tile_part" | sed -n 's/.* tnsot INTEGER (0\.\.\([0-9]*\)).*/\1/p')
tpsot=$(printf '%s\n' "$tile_part" | sed -n 's/.* tpsot INTEGER (0\.\.\([0-9]*\)).*/\1/p')
limit=$(sed -n 's/.*HV_PROFILE_TILE_PARTS = \([0-9]*\).*/\1/p' "$repo/lib/hv_rules.h")
if [ -z "$tnsot" ] || [ -z "$tpsot" ] || [ "$tnsot" != "$limit" ] ||
   [ "$((tpsot + 1))" != "$limit" ]; then
    fail "tile-part limit: TilePart-Profile tpsot 0..$tpsot, tnsot 0..$tnsot;" \
        "HV_PROFILE_TILE_PARTS $limit"
fi
# The dimension limit: Siz-Profile and the harness's PROFILE_MAX_DIMENSION.
siz=$(asn1_body Siz-Profile)
xsiz=$(printf '%s\n' "$siz" | sed -n 's/.* xsiz (1\.\.\([0-9]*\)).*/\1/p')
ysiz=$(printf '%s\n' "$siz" | sed -n 's/.* ysiz (1\.\.\([0-9]*\)).*/\1/p')
dimension=$(sed -n 's/^#define PROFILE_MAX_DIMENSION \([0-9]*\)u.*/\1/p' \
    "$repo/spec/harness/vectors.c")
if [ -z "$xsiz" ] || [ "$xsiz" != "$ysiz" ] || [ "$xsiz" != "$dimension" ]; then
    fail "dimension limit: Siz-Profile xsiz 1..$xsiz, ysiz 1..$ysiz;" \
        "PROFILE_MAX_DIMENSION $dimension"
fi

echo "model: static checks passed"
[ "$static" = 0 ] || exit 0

# ---------------------------------------------------------------------------
# The compiler.
# ---------------------------------------------------------------------------
# lib/generated must be what this compiler makes of the model.
ASN1SCC= ASN1SCC_IMAGE=$image sh "$repo/lib/generate.sh" --check

generated=$temporary/generated
mkdir "$generated"
if [ "$#" -eq 1 ]; then
    corpus=$1
    mkdir -p "$corpus"
    if [ -n "$(ls -A "$corpus")" ]; then
        echo "corpus directory is not empty: $corpus" >&2
        exit 2
    fi
    corpus=$(CDPATH= cd -- "$corpus" && pwd)
else
    corpus=$temporary/corpus
    mkdir "$corpus"
fi

files=
for m in $modules; do
    files="$files /project/spec/$m.asn1 /project/spec/$m.acn"
done
docker run --rm \
    -v "$repo:/project:ro" \
    -v "$generated:/output" \
    "$image" \
    -c -ACN --acn-v2 --field-prefix AUTO -o /output $files

docker run --rm --entrypoint sh \
    -v "$repo:/project:ro" \
    -v "$generated:/generated" \
    -v "$corpus:/corpus" \
    "$image" -c '
        set -eu
        cc -std=c11 -pedantic-errors -Wall -Wextra -Werror \
            -O1 -g -fsanitize=address,undefined -I/generated -I/project/lib \
            /project/spec/harness/vectors.c \
            /project/spec/harness/crossfield.c \
            /project/spec/harness/mapping.c \
            /project/lib/hv_rules.c \
            /generated/*.c \
            -o /tmp/vectors
        /tmp/vectors /corpus
    '

duplicates=$(cut -f1 "$corpus/manifest.tsv" | sort | uniq -d)
if [ -n "$duplicates" ]; then
    echo "duplicate vector names:" >&2
    echo "$duplicates" >&2
    exit 1
fi

echo "model: generated C compiles cleanly as strict C11"
echo "model: corpus labels and reasons agree with all mutant expectations"
if [ "$#" -eq 1 ]; then
    echo "model: corpus retained in $corpus"
fi
