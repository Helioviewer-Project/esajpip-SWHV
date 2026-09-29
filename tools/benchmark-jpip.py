#!/usr/bin/env python3
"""Alternate two benchmark binaries; retain raw results and report medians."""
import argparse
import json
import statistics
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("before")
parser.add_argument("after")
parser.add_argument("directory")
parser.add_argument("target", help="linked movie with at least five frames")
parser.add_argument("--runs", type=int, default=7)
args = parser.parse_args()
if args.runs < 1:
    parser.error("--runs must be positive")

samples = []
for run in range(args.runs + 1):
    variants = [("before", args.before), ("after", args.after)]
    if run % 2:
        variants.reverse()
    for variant, binary in variants:
        for mode in ("open", "response"):
            command = [binary, mode, args.directory, args.target]
            result = json.loads(subprocess.check_output(command, text=True))
            samples.append(dict(run=run, variant=variant, mode=mode, result=result))

summary = {}
for variant in ("before", "after"):
    result = {}
    for mode in ("open", "response"):
        rows = [s["result"] for s in samples if s["run"] and s["variant"] == variant and s["mode"] == mode]
        result[mode] = {key: statistics.median(r[key] for r in rows) for key in ("open_ms", "peak_rss_bytes")}
        if mode == "response":
            result[mode]["phases_ms"] = [statistics.median(r["phases"][i]["ms"] for r in rows) for i in range(6)]
    summary[variant] = result

# Fragmentation changes can alter wire bytes while preserving reconstructed bins.
# Report differences explicitly; equivalence is established by the library tests.
wire_equal = []
for phase in range(6):
    identities = {(s["result"]["phases"][phase]["bytes"], s["result"]["phases"][phase]["fnv64"])
                  for s in samples if s["mode"] == "response"}
    wire_equal.append(len(identities) == 1)
print(json.dumps(dict(summary=summary, wire_equal_by_phase=wire_equal, samples=samples), indent=2))
