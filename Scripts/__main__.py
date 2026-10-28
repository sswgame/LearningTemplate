"""
Scripts/__main__.py

SW Engine 통합 CLI 엔트리포인트:
  프로젝트의 빌드, 에셋 쿠킹, 환경 설정, 정적 분석 및 문서화 도구를 단일 CLI로 실행합니다.

명령 목록은 아래 `_kSubcommand` 하나다 — 이 독스트링이나 `--help` 설명에 목록을 다시 적지 말 것.
여러 곳에 적으면 하나를 더할 때 모두 고쳐야 하고, 독스트링이 가장 먼저 낡는다.

  py -3 -m Scripts --help     # 무엇이 있는지는 이 명령이 답한다
  py -3 -m Scripts <명령> ... # 나머지 인자는 그대로 넘어간다
  py -3 -m Scripts gate CheckIncludeOrder --files a.h   # lint/ 의 폴더에서 이름으로(gate · fix)

이름 있는 명령은 표(`_kSubcommand`), 종류로 묶이는 린트 · 보고서는 폴더 훑기(`LintCatalog`)다 — 린트를 더해도 이 파일은 고치지 않는다.
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
    - `modulePath`  : `Scripts` 기준 모듈 경로. 그 모듈의 `main` 을 부른다.
    - `description` : `--help` 에 찍히는 한 줄.

    모든 진입점은 `main(argv)` 를 받는다 — 남은 인자를 그대로 넘긴다.
    """

    name: str
    modulePath: str
    description: str

    def run(self, listArgument: list[str]) -> int:
        return importlib.import_module(self.modulePath).main(listArgument)


_kSubcommand: tuple[Subcommand, ...] = (
    Subcommand("setup", "setup.SetupEnvironment", "개발 환경 및 도구체인 탐색/설정"),
    Subcommand("vcpkg", "setup.SetupVcpkg", "vcpkg 탐색 및 부트스트랩"),
    Subcommand("llvm", "setup.SetupLlvm", "LLVM/Clang 탐색 및 설정"),
    Subcommand("lint", "lint.PreCommitLint", "Staged 파일 대상 사전 커밋 린트 검사"),
    Subcommand("lint-suite", "lint.RunLintSuite", "린트 전체(게이트 + 셀프테스트)를 빌드 폴더 없이 동시에 돌리고 린트마다 시간을 남긴다"),
    Subcommand("format", "lint.fixer.FormatClangFormat", "C++ 코드 clang-format 자동 포맷팅"),
    Subcommand("validate-assets", "qa.ValidateAssets", "에셋 검증 규칙(Config/Editor/AssetValidationRules.json)을 Resource/ 에 돌린다"),
    Subcommand("plans", "dev.ListPlans", "계획 문서(docs/plans)를 머리 줄의 상태 · 영역으로 골라 보인다(--status · --area · --file · --section)"),
    Subcommand("doc", "dev.RunDoc", "큰 마크다운 문서의 목차(제목 · 행 번호 · 절 크기)나 한 절만 보인다(--section, 5-4 같은 번호도 된다)"),
    Subcommand("defender", "setup.AddDefenderExclusions", "Windows Defender 빌드 디렉터리 제외 등록"),
)

_kSubcommandByName: dict[str, Subcommand] = {command.name: command for command in _kSubcommand}

#: 폴더를 훑어 이름으로 부르는 명령 — `py -3 -m Scripts gate CheckIncludeOrder --files a.h`. 목록은 폴더다(LintCatalog).
_kFolderCommand: dict[str, str] = {"gate": "gate", "fix": "fixer"}


def runFolderCommand(folderName: str, listArgument: list[str]) -> int:
    """lint/ 의 폴더 하나에서 스크립트를 이름으로 부른다. 이름이 없으면 그 폴더의 목록을 찍는다(모르는 이름이면 1)."""
    from lint.LintCatalog import discoverLintScripts
    mapScript = {script.name: script for script in discoverLintScripts(folderName)}
    if not listArgument or listArgument[0] not in mapScript:
        if listArgument:
            print(f"[Scripts] {folderName}/ 에 '{listArgument[0]}' 이 없습니다.", file=sys.stderr)
        print(f"사용: py -3 -m Scripts <{'|'.join(_kFolderCommand)}> <이름> [인자…] — lint/{folderName}/ 의 이름:")
        for name in sorted(mapScript):
            print(f"  {name}")
        return 1 if listArgument else 0
    return mapScript[listArgument[0]].module.main(listArgument[1:])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="py -3 -m Scripts",
        description="SW Engine 통합 개발 도구체인 CLI",
        epilog="\n".join([*(f"  {command.name:<15} {command.description}" for command in _kSubcommand),
                          f"  {'|'.join(_kFolderCommand)} <이름>  lint/ 의 폴더(gate · fixer)에서 이름으로 — 이름 없이 부르면 목록"]),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("command", choices=[*_kSubcommandByName, *_kFolderCommand], help="실행할 하위 도구 명령")
    parser.add_argument("args", nargs=argparse.REMAINDER, help="선택한 명령에 전달할 추가 인수")

    listArgument = list(sys.argv[1:] if argv is None else argv)
    if not listArgument:
        parser.print_help()
        return ExitCode.Violation

    parsed = parser.parse_args(listArgument[:1])
    if parsed.command in _kFolderCommand:
        return runFolderCommand(_kFolderCommand[parsed.command], listArgument[1:])
    return _kSubcommandByName[parsed.command].run(listArgument[1:])


if __name__ == "__main__":
    sys.exit(main())
