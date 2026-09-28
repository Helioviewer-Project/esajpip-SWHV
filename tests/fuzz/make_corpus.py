#!/usr/bin/env python3
"""Build per-target fuzz seed corpora from the checked-in test vectors."""

from __future__ import annotations

import argparse
import hashlib
import shutil
import struct
from pathlib import Path


JP2C = b"jp2c"


def write_unique(directory: Path, prefix: str, data: bytes) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(data).hexdigest()[:16]
    (directory / f"{prefix}-{digest}").write_bytes(data)


def copy_seed(path: Path, directory: Path, prefix: str) -> None:
    data = path.read_bytes()
    if data:
        write_unique(directory, prefix, data)


def jp2c_payloads(data: bytes) -> list[bytes]:
    payloads: list[bytes] = []
    pos = 0
    end = len(data)
    while pos + 8 <= end:
        box_start = pos
        length = int.from_bytes(data[pos : pos + 4], "big")
        box_type = data[pos + 4 : pos + 8]
        pos += 8

        if length == 1:
            if pos + 8 > end:
                break
            length = int.from_bytes(data[pos : pos + 8], "big")
            pos += 8
            payload_start = pos
        elif length == 0:
            payload_start = pos
            pos = end
            if box_type == JP2C and payload_start <= end:
                payloads.append(data[payload_start:end])
            break
        else:
            payload_start = pos

        if length < payload_start - box_start:
            break
        box_end = box_start + length
        if box_end > end:
            break
        if box_type == JP2C:
            payloads.append(data[payload_start:box_end])
        pos = box_end
    return [payload for payload in payloads if payload]


def corpus_paths(repo: Path) -> dict[str, list[Path]]:
    return {
        "vectors": sorted((repo / "tests" / "vectors" / "j2k").glob("*.jp*")),
        "transcode": sorted((repo / "tests" / "transcode" / "fixtures" / "input").glob("*.jp2")),
        "merge": sorted((repo / "tests" / "merge" / "fixtures" / "input").glob("*.jp2")),
    }


def build_corpora(repo: Path, out: Path) -> None:
    paths = corpus_paths(repo)
    for child in (out.iterdir() if out.exists() else ()):
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()
    out.mkdir(parents=True, exist_ok=True)

    reader = out / "reader-rewrite"
    deferred = out / "deferred-plt"
    asn1 = out / "asn1"
    transcode = out / "transcode-fuzz"
    merge = out / "merge-fuzz"

    for path in paths["vectors"]:
        prefix = path.stem.replace(".", "-")
        copy_seed(path, reader, prefix)
        copy_seed(path, asn1, prefix)
        data = path.read_bytes()
        if data.startswith(b"\xff\x4f"):
            write_unique(deferred, prefix, data)
            write_unique(transcode, prefix, data)
        for index, payload in enumerate(jp2c_payloads(data)):
            write_unique(deferred, f"{prefix}-jp2c-{index}", payload)
            write_unique(transcode, f"{prefix}-jp2c-{index}", payload)

    for path in paths["transcode"]:
        prefix = path.stem.replace(".", "-")
        copy_seed(path, reader, prefix)
        copy_seed(path, asn1, prefix)
        for index, payload in enumerate(jp2c_payloads(path.read_bytes())):
            write_unique(deferred, f"{prefix}-jp2c-{index}", payload)
            write_unique(transcode, f"{prefix}-jp2c-{index}", payload)

    merge_inputs = [path.read_bytes() for path in paths["merge"] if path.stat().st_size > 0]
    for left_index, left in enumerate(merge_inputs):
        for right_index, right in enumerate(merge_inputs):
            payload = struct.pack(">I", len(left)) + left + right
            write_unique(merge, f"merge-{left_index}-{right_index}", payload)

    for seed in (b"", b"\x00", b"\xff", b"\xff\x4f", b"\x00\x00\x00\x0cjP  "):
        write_unique(reader, "tiny", seed)
        write_unique(deferred, "tiny", seed)
        write_unique(asn1, "tiny", seed)
        write_unique(transcode, "tiny", seed)
        write_unique(merge, "tiny", seed)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    build_corpora(args.repo.resolve(), args.out.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
