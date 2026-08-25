#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import os
import sys
from pathlib import Path

def main() -> int:
    repo = Path(__file__).resolve().parents[2]
    gitDir = repo / ".git"

    if not gitDir.is_dir():
        print("[InstallGitHooks] .git directory not found.")
        return 1

    hooksDir = gitDir / "hooks"
    hooksDir.mkdir(parents=True, exist_ok=True)

    preCommitPath = hooksDir / "pre-commit"
    hookScript = """#!/bin/sh
# pre-commit hook
echo "[pre-commit] Checking engine layers..."
python3 Scripts/lint/CheckEngineLayers.py --strict
if [ $? -ne 0 ]; then
    echo "[Error] Engine layer violation detected."
    exit 1
fi

echo "[pre-commit] Checking source GLOB coverage..."
python3 Scripts/lint/CheckSourceGlob.py
if [ $? -ne 0 ]; then
    echo "[Error] Source GLOB coverage check failed."
    exit 1
fi

echo "[pre-commit] Lint checks passed."
exit 0
"""

    preCommitPath.write_text(hookScript, encoding="utf-8")
    if os.name != "nt":
        os.chmod(preCommitPath, 0o755)

    print(f"[InstallGitHooks] Installed: {preCommitPath}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
