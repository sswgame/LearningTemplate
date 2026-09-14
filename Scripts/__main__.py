"""
Scripts/__main__.py

SW Engine 통합 CLI 엔트리포인트:
  프로젝트의 빌드, 에셋 쿠킹, 환경 설정, 정적 분석 및 문서화 도구를 단일 CLI로 실행합니다.

명령 목록은 아래 `_kSubcommand` 하나다. 예전에는 같은 목록이 **세 곳**에 있었다 — 이 독스트링,
`--help` 의 설명 문자열, 그리고 `cmdXxx` 함수 여덟과 그것들을 모은 딕셔너리. 하나를 더할 때
셋을 고쳐야 했고, 실제로 독스트링이 가장 먼저 낡는다.

  py -3 -m Scripts --help     # 무엇이 있는지는 이 명령이 답한다
  py -3 -m Scripts <명령> ... # 나머지 인자는 그대로 넘어간다
"""

from __future__ import annotations

import argparse
import importlib
import sys
from dataclasses import dataclass
from pathlib import Path

# 부모 경로(Scripts) 및 프로젝트 루트 임포트 보장
_scriptRoot = Path(__file__).resolve().parent
if str(_scriptRoot) not in sys.path:
    sys.path.insert(0, str(_scriptRoot))


@dataclass(frozen=True)
class Subcommand:
    """
    하위 명령 하나.

    - `name`        : `py -3 -m Scripts <name>`
    - `modulePath`  : `Scripts` 기준 모듈 경로. 그 모듈의 `main` 을 부른다.
    - `description` : `--help` 에 찍히는 한 줄.
    - `bForwardArgs`: 남은 인자를 `main(args)` 로 넘길지. 인자를 받지 않는 `main()` 도 있다.
    """

    name: str
    modulePath: str
    description: str
    bForwardArgs: bool = True

    def run(self, listArgument: list[str]) -> int:
        main = importlib.import_module(self.modulePath).main
        return main(listArgument) if self.bForwardArgs else main()


_kSubcommand: tuple[Subcommand, ...] = (
    Subcommand("setup", "setup.SetupEnvironment", "개발 환경 및 도구체인 탐색/설정"),
    Subcommand("vcpkg", "setup.SetupVcpkg", "vcpkg 탐색 및 부트스트랩"),
    Subcommand("llvm", "setup.SetupLlvm", "LLVM/Clang 탐색 및 설정"),
    Subcommand("cook", "generate.CookAssets", "프리팹, 씬, 리소스 팩 일괄 쿠킹"),
    Subcommand("lint", "lint.PreCommitLint", "Staged 파일 대상 사전 커밋 린트 검사", bForwardArgs=False),
    Subcommand("format", "lint.report.RunClangFormat", "C++ 코드 clang-format 자동 포맷팅"),
    Subcommand("docs", "generate.GenerateDocs", "Doxygen API 레퍼런스 문서 생성"),
    Subcommand("defender", "setup.AddDefenderExclusions", "Windows Defender 빌드 디렉터리 제외 등록",
               bForwardArgs=False),
)

_kSubcommandByName: dict[str, Subcommand] = {command.name: command for command in _kSubcommand}


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="py -3 -m Scripts",
        description="SW Engine 통합 개발 도구체인 CLI",
        epilog="\n".join(f"  {command.name:<10} {command.description}" for command in _kSubcommand),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("command", choices=list(_kSubcommandByName), help="실행할 하위 도구 명령")
    parser.add_argument("args", nargs=argparse.REMAINDER, help="선택한 명령에 전달할 추가 인수")

    if len(sys.argv) < 2:
        parser.print_help()
        return 1

    parsed = parser.parse_args(sys.argv[1:2])
    return _kSubcommandByName[parsed.command].run(sys.argv[2:])


if __name__ == "__main__":
    sys.exit(main())
