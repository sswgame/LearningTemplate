"""
Scripts/__main__.py

SW Engine 통합 CLI 엔트리포인트:
  프로젝트의 빌드, 에셋 쿠킹, 환경 설정, 정적 분석 및 문서화 도구를 단일 CLI로 실행합니다.

명령 목록은 아래 `_kSubcommand` 하나다 — 이 독스트링이나 `--help` 설명에 목록을 다시 적지 말 것.
여러 곳에 적으면 하나를 더할 때 모두 고쳐야 하고, 독스트링이 가장 먼저 낡는다.

  py -3 -m Scripts --help     # 무엇이 있는지는 이 명령이 답한다
  py -3 -m Scripts --list [폴더]   # 파일마다 docstring 첫 줄과 부르는 명령 — 손으로 쓴 파일 목록 문서를 대신한다
  py -3 -m Scripts <명령> ... # 나머지 인자는 그대로 넘어간다
  py -3 -m Scripts gate CheckEngineLayers --files a.h   # lint/ 의 폴더에서 이름으로(gate · fix · report · selftest)

이름 있는 명령은 표(`_kSubcommand`), 종류로 묶이는 린트 · 보고서는 폴더 훑기(`LintCatalog`)다 — 린트를 더해도 이 파일은 고치지 않는다.
"""

from __future__ import annotations

import argparse
import importlib
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # Scripts — import 루트는 하나다(docs/plans/ScriptsStructure.md)

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ScriptCatalog import readScriptSummaries  # noqa: E402


@dataclass(frozen=True)
class Subcommand:
    """
    하위 명령 하나.

    - `name`        : `py -3 -m Scripts <name>`
    - `modulePath`  : `Scripts` 기준 모듈 경로. 그 모듈의 `main` 을 부른다.
    - `description` : `--help` 에 찍히는 한 줄.

    모든 진입점은 `main(argv)` 를 받는다(CheckScriptEntryPoints) — 남은 인자를 그대로 넘긴다.
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
    Subcommand("cook", "generate.CookAssets", "프리팹, 씬, 리소스 팩 일괄 쿠킹"),
    Subcommand("lint", "lint.PreCommitLint", "Staged 파일 대상 사전 커밋 린트 검사"),
    Subcommand("lint-suite", "lint.RunLintSuite", "린트 전체(게이트 + 셀프테스트)를 빌드 폴더 없이 동시에 돌리고 린트마다 시간을 남긴다"),
    Subcommand("format", "lint.fixer.FormatClangFormat", "C++ 코드 clang-format 자동 포맷팅"),
    Subcommand("docs", "generate.GenerateDocs", "Doxygen API 레퍼런스 문서 생성"),
    Subcommand("test", "dev.RunTests", "스위트 · 케이스 이름으로 테스트 실행 (실행 파일 · 작업 폴더를 대신 찾는다)"),
    Subcommand("validate-assets", "qa.ValidateAssets", "에셋 검증 규칙(Config/Editor/AssetValidationRules.json)을 Resource/ 에 돌린다"),
    Subcommand("asset-merge", "asset.AssetMerge", "XML 에셋 의미 비교 · 3-way 병합 (git 드라이버로도 쓴다)"),
    Subcommand("golden", "qa.GoldenImages", "테스트 게임 자동 플레이를 네 백엔드로 그려 골든 이미지와 지표로 견준다"),
    Subcommand("soak", "qa.Soak", "자동 플레이 장시간 실행 — 메모리 · 핸들 증가와 프레임 p99"),
    Subcommand("perf", "qa.PerfRegression", "Release 프레임 p50 · p99 를 이 기계의 기준과 견준다"),
    Subcommand("load", "qa.Load", "부하 검증 — 부하 시나리오 × 하드웨어 프로필을 Release 로 재고 예산(p50 · p99 · max) · 기준선과 견주며 병목을 적는다"),
    Subcommand("perf-compare", "qa.PerfCompare", "성능 변경의 전 · 후(스위치 · 바이너리 A/B)를 번갈아 여러 번 재고 중앙값 표를 낸다"),
    Subcommand("scene-load-bench", "qa.SceneLoadBench", "큰 씬을 만들어 원본 XML 과 쿠킹본의 로드 시간 · 로드 중 최악 프레임을 견준다"),
    Subcommand("symbols", "dev.StoreSymbols", "빌드의 심볼(PDB · .debug)을 심볼 저장소 배치로 복사"),
    Subcommand("editor-theme", "dev.MakeEditorTheme", "에디터 테마 토큰(tokens.json)을 Figma 변수 · DTCG 와 주고받고 검사한다(import-figma · export-figma · check)"),
    Subcommand("stacks", "dev.SampleStacks", "살아 있는 프로세스의 스택을 여러 번 떠서 함수별로 모은다(Windows · DbgHelp)"),
    Subcommand("worktree-make", "dev.MakeWorktree", "작업 단위용 git 워크트리를 만들고 도구 · vcpkg 폴더를 main 체크아웃과 나눠 쓰게 연결한다"),
    Subcommand("worktree-remove", "dev.RemoveWorktree", "워크트리를 나눠 쓰는 링크를 먼저 끊고 지운다(브랜치 wt/<이름> 도) — 다른 세션이 쓰는 중이면 건너뛴다"),
    Subcommand("worktree-list", "dev.ListWorktrees", "워크트리마다 브랜치와 주인(살아 있는지)을 보인다"),
    Subcommand("shared-lock", "dev.RunSharedLock", "워크트리가 나눠 쓰는 자원(vcpkg 설치 폴더 · 빌드 칸 · GPU)을 쓰기 전에 사용 중인지 묻고 잡는 잠금(status · acquire · release · run)"),
    Subcommand("full-build", "dev.RunFullBuild", "빌드 칸을 잡고 all AllTests 전체 빌드 — 성공하면 그 트리의 증거를 남긴다(main push 가 요구한다)"),
    Subcommand("ci-jobs", "dev.ListCiJobs", "GitHub Actions 실행 · 잡 · 실패 주석을 공개 API 로 읽는다"),
    Subcommand("plans", "dev.ListPlans", "계획 문서(docs/plans)를 머리 줄의 상태 · 영역으로 골라 보인다(--status · --area · --file · --section)"),
    Subcommand("doc", "dev.RunDoc", "큰 마크다운 문서의 목차(제목 · 행 번호 · 절 크기)나 한 절만 보인다(--section, 5-4 같은 번호도 된다)"),
    Subcommand("edit-file", "dev.RunEditFile", "파일의 한 곳을 정확히 한 번 바꾼다 — 줄 끝(CRLF) · BOM 보존, NUL 거부, 여러 곳도 한꺼번에 검증한 뒤 쓴다(--old-file · --new-file · --changes)"),
    Subcommand("agent-log", "dev.RunAgentLog", "docs/12_Agents.md '토큰 측정값' 표에 한 줄(날짜 · 일 · 조각 · 에이전트 · 토큰 · 도구 호출)을 더한다"),
    Subcommand("status-board", "dev.RunStatusBoard", "README 의 '지금 진행 상황' 표를 계획 문서 · 로드맵 · 후보 문서에서 만든다(--write · --check)"),
    Subcommand("deps-outdated", "dev.ListOutdatedDeps", "vcpkg 의존성의 지금 판과 레지스트리 최신 판을 견준다(오버레이 포함)"),
    Subcommand("defender", "setup.AddDefenderExclusions", "Windows Defender 빌드 디렉터리 제외 등록"),
)

_kSubcommandByName: dict[str, Subcommand] = {command.name: command for command in _kSubcommand}

#: 폴더를 훑어 이름으로 부르는 명령 — `py -3 -m Scripts gate CheckEngineLayers --files a.h`. 목록은 폴더다(LintCatalog).
_kFolderCommand: dict[str, str] = {"gate": "gate", "fix": "fixer", "report": "report", "selftest": "selftest"}


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


def printScriptList(listArgument: list[str]) -> int:
    """`--list [폴더]` — 파일마다 docstring 첫 줄, 이름 있는 명령 · 폴더 명령이면 부르는 법을 붙인다. import 하지 않는다(AST)."""
    folder = listArgument[0].strip("/") if listArgument else ""
    try:
        listSummary = readScriptSummaries(Path(__file__).resolve().parent, folder)
    except FileNotFoundError as error:
        print(f"[Scripts] {error}", file=sys.stderr)
        return 1
    mapModuleToCommand = {command.modulePath: command.name for command in _kSubcommand}
    mapFolderToCommand = {f"lint.{folderName}": commandName for commandName, folderName in _kFolderCommand.items()}
    currentGroup = None
    for entry in listSummary:
        group = entry.relPath.rpartition("/")[0] or "."
        if group != currentGroup:
            print(f"\n{group}/")
            currentGroup = group
        package, _, leaf = entry.moduleName.rpartition(".")
        if entry.moduleName in mapModuleToCommand:
            usage = f"  [py -3 -m Scripts {mapModuleToCommand[entry.moduleName]}]"
        elif package in mapFolderToCommand and leaf != "__init__":
            usage = f"  [py -3 -m Scripts {mapFolderToCommand[package]} {leaf}]"
        else:
            usage = ""
        print(f"  {entry.relPath.rpartition('/')[2]:<36} {entry.summary}{usage}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="py -3 -m Scripts",
        description="SW Engine 통합 개발 도구체인 CLI",
        epilog="\n".join([*(f"  {command.name:<15} {command.description}" for command in _kSubcommand),
                          f"  {'|'.join(_kFolderCommand)} <이름>  lint/ 의 폴더(gate · fixer · report · selftest)에서 이름으로 — 이름 없이 부르면 목록",
                          "  --list [폴더]    파일마다 한 줄 설명과 부르는 명령(docstring 에서 만든다)"]),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("command", choices=[*_kSubcommandByName, *_kFolderCommand], help="실행할 하위 도구 명령")
    parser.add_argument("args", nargs=argparse.REMAINDER, help="선택한 명령에 전달할 추가 인수")

    listArgument = list(sys.argv[1:] if argv is None else argv)
    if not listArgument:
        parser.print_help()
        return 1
    if listArgument[0] == "--list":
        return printScriptList(listArgument[1:])

    parsed = parser.parse_args(listArgument[:1])
    if parsed.command in _kFolderCommand:
        return runFolderCommand(_kFolderCommand[parsed.command], listArgument[1:])
    return _kSubcommandByName[parsed.command].run(listArgument[1:])


if __name__ == "__main__":
    sys.exit(main())
