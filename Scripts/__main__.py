"""
Scripts/__main__.py

SW Engine 통합 CLI 엔트리포인트:
  프로젝트의 빌드, 에셋 쿠킹, 환경 설정, 정적 분석 및 문서화 도구를 단일 CLI로 실행합니다.

명령 목록은 아래 `_kSubcommand` 하나다 — 이 독스트링이나 `--help` 설명에 목록을 다시 적지 말 것.
여러 곳에 적으면 하나를 더할 때 모두 고쳐야 하고, 독스트링이 가장 먼저 낡는다.

  py -3 -m Scripts --help     # 무엇이 있는지는 이 명령이 답한다
  py -3 -m Scripts <명령> ... # 나머지 인자는 그대로 넘어간다
"""

from __future__ import annotations

import argparse
import importlib
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # Scripts — import 루트는 하나다

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402


@dataclass(frozen=True)
class Subcommand:
    """
    하위 명령 하나.

    - `name`        : `py -3 -m Scripts <name>`
    - `modulePath`  : `Scripts` 기준 모듈 경로. 그 모듈의 `main` 을 부른다(`모듈:함수` 로 다른 진입 함수를 고른다).
    - `description` : `--help` 에 찍히는 한 줄.

    모든 진입점은 `(argv)` 를 받는다 — 남은 인자를 그대로 넘긴다.
    """

    name: str
    modulePath: str
    description: str

    def run(self, listArgument: list[str]) -> int:
        moduleName, _, entryName = self.modulePath.partition(":")
        return getattr(importlib.import_module(moduleName), entryName or "main")(listArgument)


_kSubcommand: tuple[Subcommand, ...] = (
    Subcommand("setup", "setup.Environment", "개발 환경 및 도구체인 탐색/설정"),
    Subcommand("vcpkg", "setup.Vcpkg", "vcpkg 탐색 및 부트스트랩"),
    Subcommand("llvm", "setup.Llvm", "LLVM/Clang 탐색 및 설정"),
    Subcommand("lint", "lint.PreCommit", "Staged 파일 대상 사전 커밋 린트 검사"),
    Subcommand("include-order", "lint.IncludeOrder:mainCheck", "include 순서 검사(--files 로 좁힌다)"),
    Subcommand("format-includes", "lint.Format:mainFormatIncludes", "include 순서 · 중복 자동 수정(--check 는 검사만)"),
    Subcommand("format", "lint.Format:mainFormatClang", "C++ 코드 clang-format 자동 포맷팅"),
    Subcommand("net-pair", "net.NetPairRun", "두 프로세스 온라인 접속 시나리오 — 서버 · 클라이언트로 로그인 → 게임 접속 티켓 → 암호화 UDP 접속(run · check)"),
    Subcommand("destruction-net", "dev.DestructionNetPair", "파괴 복제 실제 세션 확인 — 서버 · 클라이언트 두 프로세스의 구조 해시 맞대기(run · compare)"),
)

_kSubcommandByName: dict[str, Subcommand] = {command.name: command for command in _kSubcommand}

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="py -3 -m Scripts",
        description="SW Engine 통합 개발 도구체인 CLI",
        epilog="\n".join(f"  {command.name:<15} {command.description}" for command in _kSubcommand),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("command", choices=list(_kSubcommandByName), help="실행할 하위 도구 명령")
    parser.add_argument("args", nargs=argparse.REMAINDER, help="선택한 명령에 전달할 추가 인수")

    listArgument = list(sys.argv[1:] if argv is None else argv)
    if not listArgument:
        parser.print_help()
        return ExitCode.Violation

    parsed = parser.parse_args(listArgument[:1])
    return _kSubcommandByName[parsed.command].run(listArgument[1:])


if __name__ == "__main__":
    sys.exit(main())
