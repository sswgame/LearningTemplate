#!/usr/bin/env python3
"""
Scripts/lint/RunClangFormat.py

CI와 동일하게 Source / Test / Tools/ReflectionParser 에 clang-format 적용.

  py -3 Scripts/lint/RunClangFormat.py           # in-place
  py -3 Scripts/lint/RunClangFormat.py --check   # dry-run --Werror
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import List

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ConfigHelper import getProjectRoot, loadToolchainConfig, kKeyLlvmPath
from setup.SetupLlvm import ensureClangFormat

_kFormatRoots = ("Source", "Test", "Tools/ReflectionParser")
_kSuffixes = {".cpp", ".h", ".hpp"}

def collectFilesInternal(root: Path) -> List[Path]:
    fileList: List[Path] = []
    for rel in _kFormatRoots:
        base = root / rel
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.is_file() and path.suffix.lower() in _kSuffixes:
                fileList.append(path)
    return sorted(fileList)

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run clang-format on Source/Test/ReflectionParser.")
    parser.add_argument(
        "--check",
        action="store_true",
        help="dry-run with --Werror (CI parity); do not modify files",
    )
    args = parser.parse_args(argv)

    root = getProjectRoot()
    toolchain = loadToolchainConfig()
    llvmPath = str(toolchain.get(kKeyLlvmPath, "") or "")
    import shutil
    clangFormat = shutil.which("clang-format")
    if not clangFormat:
        clangFormat = ensureClangFormat(llvmPath, allowDownload=True)
    if not clangFormat:
        sys.stderr.write(
            "[RunClangFormat] clang-format not available. "
            "Re-run SetupEnvironment / SetupLlvm or install LLVM.\n"
        )
        return 1

    fileList = collectFilesInternal(root)
    if not fileList:
        sys.stderr.write("[RunClangFormat] No source files found.\n")
        return 1

    print(f"[RunClangFormat] Using {clangFormat} on {len(fileList)} files", file=sys.stderr)
    # Batch to avoid command-line length limits on Windows.
    batchSize = 64
    for start in range(0, len(fileList), batchSize):
        batch = fileList[start : start + batchSize]
        cmd = [clangFormat]
        if args.check:
            cmd.extend(["--dry-run", "--Werror"])
        else:
            cmd.append("-i")
        cmd.extend(str(path) for path in batch)
        result = subprocess.run(cmd, cwd=str(root), check=False)
        if result.returncode != 0:
            sys.stderr.write(f"[RunClangFormat] clang-format failed (exit {result.returncode})\n")
            return result.returncode
    return 0

if __name__ == "__main__":
    sys.exit(main())
