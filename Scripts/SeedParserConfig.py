#!/usr/bin/env python3
"""Write Config/parser_config.json from parser_config.defaults.json for the current OS."""

from __future__ import annotations

import json
import platform
import sys
from pathlib import Path

from ConfigHelper import GetProjectRoot


def Main() -> int:
    root = GetProjectRoot()
    defaults = root / "Config" / "parser_config.defaults.json"
    out = root / "Config" / "parser_config.json"
    if not defaults.exists():
        print(f"[SeedParserConfig] missing {defaults}", file=sys.stderr)
        return 1

    data = json.loads(defaults.read_text(encoding="utf-8"))
    os_key = {"Windows": "windows", "Linux": "linux", "Darwin": "darwin"}.get(
        platform.system(), "linux"
    )
    data["parser_args"] = list(data.get("default_parser_args", [])) + list(
        data.get("platform_parser_args", {}).get(os_key, [])
    )
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")
    print(f"[SeedParserConfig] wrote {out} ({os_key}, {len(data['parser_args'])} args)")
    return 0


if __name__ == "__main__":
    sys.exit(Main())
