#!/usr/bin/env python3
"""Recursively lowercase .bin/.png filenames under a directory.

Examples:
  python3 lowercase_bin_png.py
  python3 lowercase_bin_png.py /path/to/root --dry-run
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path


TARGET_EXTS = {".bin", ".png"}


def make_unique_temp_path(path: Path) -> Path:
    """Return a temporary path in the same directory that does not exist."""
    base = path.name + ".__tmp_lowercase__"
    candidate = path.with_name(base)
    i = 1

    while candidate.exists():
        candidate = path.with_name(f"{base}{i}")
        i += 1

    return candidate


def collect_renames(root: Path) -> list[tuple[Path, Path]]:
    renames: list[tuple[Path, Path]] = []

    for src in root.rglob("*"):
        if not src.is_file():
            continue

        if src.suffix.lower() not in TARGET_EXTS:
            continue

        dst = src.with_name(src.name.lower())
        if src.name != dst.name:
            renames.append((src, dst))

    return renames


def apply_renames(renames: list[tuple[Path, Path]], dry_run: bool) -> tuple[int, int]:
    renamed = 0
    skipped = 0

    for src, dst in renames:
        if dst.exists():
            try:
                same_file = os.path.samefile(src, dst)
            except OSError:
                same_file = False

            if not same_file:
                print(f"SKIP (target exists): {src} -> {dst}")
                skipped += 1
                continue

        if dry_run:
            print(f"DRY-RUN: {src} -> {dst}")
            renamed += 1
            continue

        # On case-insensitive filesystems, a direct case-only rename can fail.
        # Rename via a temporary file in the same directory to make it robust.
        tmp = make_unique_temp_path(src)
        os.replace(src, tmp)
        os.replace(tmp, dst)
        print(f"RENAMED: {src} -> {dst}")
        renamed += 1

    return renamed, skipped


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Lowercase every .bin/.png filename recursively under ROOT."
    )
    parser.add_argument(
        "root",
        nargs="?",
        default=".",
        help="Root directory to scan (default: current directory).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print planned renames without changing files.",
    )

    args = parser.parse_args()
    root = Path(args.root).resolve()

    if not root.exists() or not root.is_dir():
        print(f"Error: not a directory: {root}")
        return 2

    renames = collect_renames(root)
    if not renames:
        print("No .bin/.png files need renaming.")
        return 0

    renamed, skipped = apply_renames(renames, args.dry_run)
    verb = "Would rename" if args.dry_run else "Renamed"
    print(f"{verb}: {renamed}, skipped: {skipped}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
