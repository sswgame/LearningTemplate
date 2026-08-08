#!/usr/bin/env python3
r"""
Scripts/SeedParserConfig.py

Config/parser_config.json 을 현재 OS용으로 생성합니다.
실제 로직은 SetupEnvironment.UpdateParserConfig 에 위임합니다.

  python3 Scripts/SeedParserConfig.py
"""

from __future__ import annotations

import platform
import sys

from ConfigHelper import GetProjectRoot
from SetupEnvironment import UpdateParserConfig


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
