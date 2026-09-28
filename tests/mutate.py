#!/usr/bin/env python3
"""One-operator mutants of a C file, for tests/run_mutation.sh.

    mutate.py list FILE [OPERATORS]     one line per mutant: ID, line, operator, change
    mutate.py apply FILE ID OUT [OPERATORS]   apply an ID from the same file and operators

OPERATORS is a comma-separated subset of:

    rule        return "..."  ->  return NULL      (a rule that no longer fails)
    relational  == <-> !=,  < -> <=,  <= -> <,  > -> >=,  >= -> >
    logical     && <-> ||
    negation    !x -> x
    constant    an integer literal n -> n + 1

The default is rule,relational,logical,negation. Comments, string and
character literals and preprocessor lines are never mutated; mutant IDs are
the positions of the mutants in the file, so they stay the same for the same
file and operators.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

DEFAULT = ("rule", "relational", "logical", "negation")
OPERATORS = DEFAULT + ("constant",)


def code_mask(text: str) -> list[bool]:
    """For each character, whether it is code: not in a comment, a string or
    character literal, or a preprocessor line."""
    mask = [True] * len(text)
    i, n = 0, len(text)
    line_start = True
    while i < n:
        c = text[i]
        if line_start and c == "#":
            j = i
            while j < n and text[j] != "\n":   # to the end of the line, and its continuations
                if text[j] == "\\" and j + 1 < n and text[j + 1] == "\n":
                    j += 1
                j += 1
            for k in range(i, j):
                mask[k] = False
            i = j
            continue
        if c == "\n":
            line_start = True
            i += 1
            continue
        if not c.isspace():
            line_start = False
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                mask[k] = False
            i = j
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                mask[k] = False
            i = j
            continue
        if c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            for k in range(i, j):
                mask[k] = False
            i = j
            continue
        i += 1
    return mask


def mutants(text: str, operators: tuple[str, ...]) -> list[tuple[int, int, str, str, str]]:
    """(start, end, replacement, operator, description), in file order."""
    mask = code_mask(text)
    found: list[tuple[int, int, str, str, str]] = []

    def code(start: int, end: int) -> bool:
        return all(mask[start:end])

    def add(start: int, end: int, new: str, operator: str) -> None:
        found.append((start, end, new, operator, f"{text[start:end]!r} -> {new!r}"))

    if "rule" in operators:
        for m in re.finditer(r'\breturn\s+("(?:[^"\\\n]|\\.)*")', text):
            if code(m.start(), m.start() + 6) and not mask[m.start(1)]:
                found.append((m.start(1), m.end(1), "NULL", "rule",
                              f"return {m.group(1)} -> return NULL"))
    if "relational" in operators:
        for m in re.finditer(r"==|!=|<=|>=|(?<![<>\-])<(?![<=])|(?<![<>\-])>(?![>=])", text):
            if not code(m.start(), m.end()):
                continue
            op = m.group(0)
            new = {"==": "!=", "!=": "==", "<=": "<", ">=": ">", "<": "<=", ">": ">="}[op]
            add(m.start(), m.end(), new, "relational")
    if "logical" in operators:
        for m in re.finditer(r"&&|\|\|", text):
            if code(m.start(), m.end()):
                add(m.start(), m.end(), "||" if m.group(0) == "&&" else "&&", "logical")
    if "negation" in operators:
        for m in re.finditer(r"!(?!=)", text):
            if code(m.start(), m.end()):
                add(m.start(), m.end(), "", "negation")
    if "constant" in operators:
        for m in re.finditer(r"(?<![\w.])(\d+)([uUlL]*)(?![\w.])", text):
            if code(m.start(), m.end()):
                add(m.start(1), m.end(1), str(int(m.group(1)) + 1), "constant")
    found.sort(key=lambda f: (f[0], f[3]))
    return found


def parse_operators(arg: str | None) -> tuple[str, ...]:
    if not arg:
        return DEFAULT
    chosen = tuple(o for o in arg.split(",") if o)
    unknown = [o for o in chosen if o not in OPERATORS]
    if unknown:
        raise SystemExit(f"mutate.py: unknown operator {', '.join(unknown)}; "
                         f"known: {', '.join(OPERATORS)}")
    return chosen


def main(argv: list[str]) -> int:
    if len(argv) >= 3 and argv[1] == "list":
        text = Path(argv[2]).read_text()
        for k, (start, _, _, operator, what) in enumerate(
                mutants(text, parse_operators(argv[3] if len(argv) > 3 else None))):
            print(f"{k}\t{text.count(chr(10), 0, start) + 1}\t{operator}\t{what}")
        return 0
    if len(argv) >= 5 and argv[1] == "apply":
        text = Path(argv[2]).read_text()
        found = mutants(text, parse_operators(argv[5] if len(argv) > 5 else None))
        k = int(argv[3])
        if not 0 <= k < len(found):
            raise SystemExit(f"mutate.py: no mutant {k} in {argv[2]}")
        start, end, new, _, _ = found[k]
        Path(argv[4]).write_text(text[:start] + new + text[end:])
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
