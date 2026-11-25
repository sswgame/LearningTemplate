"""
Scripts/__main__.py

스크립트 명령의 입구 하나. 명령 목록은 어디에도 적지 않는다 — `Scripts/<폴더>/*.py` 의 `Command` 하위 클래스가 곧 명령이다(`common/command.py`).

  py -3 -m Scripts --help     # 무엇이 있는지는 이 명령이 답한다
  py -3 -m Scripts <명령> ... # 나머지 인자는 그대로 넘어간다
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))   # Scripts — import 루트는 하나다

from common.command import find_commands  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402


def main(argv: Sequence[str] | None = None) -> int:
    map_command = find_commands()
    parser = argparse.ArgumentParser(
        prog="py -3 -m Scripts",
        description="SW Engine 통합 개발 도구체인 CLI",
        epilog="\n".join(f"  {name:<22} {map_command[name].help}" for name in sorted(map_command)),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("command", choices=sorted(map_command), metavar="명령", help="아래 목록의 하나 — 나머지 인자는 그 명령에 그대로 넘어간다")

    list_argument = list(sys.argv[1:] if argv is None else argv)
    if not list_argument:
        parser.print_help()
        return ExitCode.VIOLATION
    return map_command[parser.parse_args(list_argument[:1]).command].run(list_argument[1:])


if __name__ == "__main__":
    sys.exit(main())
