#!/usr/bin/env python3
r"""
Scripts/setup/SeedParserConfig.py

  python3 Scripts/setup/SeedParserConfig.py
"""

from __future__ import annotations

import platform
import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parents[1]
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from ConfigHelper import EnsureScriptsOnPath, GetProjectRoot

EnsureScriptsOnPath()

from setup.UpdateParserConfig import UpdateParserConfig


def Main() -> int:
    root = GetProjectRoot()
    out = root / "Config" / "parser_config.json"
    os_key = {"Windows": "windows", "Linux": "linux", "Darwin": "darwin"}.get(
        platform.system(), "linux"
    )
    UpdateParserConfig(os_key, out)
    print(f"[SeedParserConfig] wrote {out} ({os_key})")
    return 0


if __name__ == "__main__":
    sys.exit(Main())
