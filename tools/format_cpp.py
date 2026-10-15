#!/usr/bin/env python3
"""Format or check first-party C++ files with clang-format 20.1.8."""

import argparse
import fnmatch
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
VERSION = "20.1.8"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="report formatting violations")
    args = parser.parse_args()

    formatter = shutil.which("clang-format")
    if formatter is None:
        parser.error(f"install the formatter with: python3 -m pip install clang-format=={VERSION}")

    version = subprocess.check_output([formatter, "--version"], text=True).strip()
    if version.split()[-1] != VERSION:
        parser.error(f"expected clang-format {VERSION}; found {version}")

    excluded = [
        line.strip()
        for line in (ROOT / ".clang-format-ignore").read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    files = sorted(
        str(path)
        for folder in ("src", "include", "tests", "tools")
        for path in (ROOT / folder).rglob("*")
        if path.suffix in (".cpp", ".hpp")
        and not any(fnmatch.fnmatch(path.relative_to(ROOT).as_posix(), pattern) for pattern in excluded)
    )
    options = ["--dry-run", "--Werror"] if args.check else ["-i"]
    result = subprocess.run([formatter, *options, *files], cwd=ROOT)
    if result.returncode == 0:
        print(f"{'Checked' if args.check else 'Formatted'} {len(files)} C++ files.")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
