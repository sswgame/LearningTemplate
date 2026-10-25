#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
시나리오 하나를 그 시나리오가 선언한 인자 그대로 돌리는 단일 명령 — `App.exe -scenario=…` 를 손으로 조립하다 인자를 빠뜨리는 일(예: `-gv_firstScene` 이 없어 읽기 오류 11)을 없앤다.

    py -3 -m Scripts scenario motionblur                                # 이름 조각으로(하나로 정해질 때만) — dx12, 이 저장소의 첫 구성된 빌드
    py -3 -m Scripts scenario game/empty/automation/fog.scenario.xml --backend all --config Release
    py -3 -m Scripts scenario fog --backend vulkan --screenshot-dir out/fog   # 사진을 백엔드별 폴더(out/fog/vk/)로 모은다
    py -3 -m Scripts scenario fog --dry-run                             # 조립한 명령만 찍는다

시나리오 파일의 `<Launch>`(게임 팩의 게임 이름 · `-gv_firstScene` 같은 기동 변수)와 루트 선언(에디터 · 전경 창)에서 인자를 만들고, 거기에 `-unattended`
`-scenario=` `-scenario-report=` 백엔드 스위치를 붙인다(`Test/AppTest/AppTestUtil` 의 조립과 같은 규칙). 백엔드마다 GPU 칸을 잡고(공유 · 전경 창이 필요한 시나리오는 독점 — 작업 방식 2 절) 한 번씩 돌려
종료 코드를 뜻으로 풀고, 보고 JSON 의 `failures[]` · `unconfirmed[]` · `unhandledLogs[]` 를 표로 보인다(처리하지 않은 로그는 줄 그대로).
종료 코드: 0 모든 백엔드 통과, 1 하나라도 통과가 아님, 2 돌릴 수 없다(이름이 없거나 모호함 · 선언을 인자로 풀 수 없음 · 빌드 없음 …).

풀 수 없는 시나리오는 폴백 없이 이유를 말하고 2 로 끝난다: 부하(`<Load>`) 시나리오(실행기가 메시 수 · 회선을 명령줄로 옮긴다 → `py -3 -m Scripts load`),
앞 실행이 남긴 저장 상태를 잇는 묶음 일원(`<묶음>.<n>.scenario.xml`), 배포(Shipping) 빌드가 가질 수 없는 인자(`<Launch>` 의 개발 변수 · 에디터 · 다른 게임),
`<Launch>` 형식이 틀린 시나리오, 시나리오를 한 게임 팩의 게임으로 풀 수 없는 경우(`Config/Game` 에 그 팩이 없음).
`<Launch>` 가 없는 시나리오는 기동 변수가 필요 없다는 선언이므로 변수 없이 돈다(게임 · 백엔드 · 보고 인자만 붙는다).
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.AppRun import findUsableBackends, kAppRhiUnusableExitCode, kBackendSwitch, runApp  # noqa: E402
from common.BuildTree import BuildTree  # noqa: E402
from common.CookContract import CookContractSpec  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.ScenarioFile import (ScenarioDeclaration, ScenarioFileError, findResourceRoot, findScenarioCandidates,  # noqa: E402
                                 readScenarioDeclaration)
from common.ScenarioOutcome import (ScenarioRunReport, describeOutcomeExitCode, kExitCodeHeaderRelPath, readOutcomeExitCodes,  # noqa: E402
                                    readRunReport)
from common.SharedLock import holdLock, kDefaultWaitSeconds  # noqa: E402
from common.Worktree import findMainRoot  # noqa: E402

#: App 한 판의 시한(초) — `AppTestUtil::kScenarioTimeoutSeconds` 와 같다. 느린 구성은 `--timeout` 으로 늘린다.
kDefaultTimeoutSeconds = 180
#: 구성 이름 → 빌드 폴더 프리셋(`build/<프리셋>`).
kMapConfigPreset: dict[str, str] = {"Release": "Ninja-Release", "Shipping": "Ninja-Shipping", "Debug": "Ninja-Debug"}
#: App 작업 폴더(`Bin`) 아래 보고 · 산출물 폴더 — 실행기가 `Saved/Automation/<시나리오 name>/` 에 사진을 쓴다(`AutomationRunner::_outputDirectory`).
kAutomationFolder = "Saved/Automation"
#: 게임 프리셋 폴더(리소스 루트의 부모 기준) · 팩 키 — `Config/Game/<게임>.json` 의 `_packRoot`.
kGamePresetFolder = "Config/Game"
kPackRootKey = "_packRoot"
#: 종료 코드가 없는 칸(띄우지 못함 · 시한 초과 · 잠금 실패)의 표 글.
kNoExitCodeText = "-"
#: 보고가 없을 때 보일 App 출력의 끝 줄 수.
kTailLineCount = 8


class ScenarioRunError(Exception):
    """돌릴 수 없다 — 이유가 문장에 든다(종료 코드 2)."""


@dataclass(frozen=True)
class Backend:
    """한 판의 백엔드 — 이름(`dx12`)과 App 스위치(`-dx12`, 배포 빌드는 링크한 하나뿐이라 스위치 없음)."""

    name: str
    switch: str


@dataclass(frozen=True)
class RunPlan:
    """돌릴 일 하나 — 어떤 시나리오를 어떤 App 으로 어떤 백엔드들에서."""

    declaration: ScenarioDeclaration
    appPath: Path
    listBackend: tuple[Backend, ...]
    gameName: str          # 빈 글이면 `-game=` 를 붙이지 않는다(엔진 시나리오 · 배포 빌드)
    bShipping: bool
    timeoutSeconds: int

    @property
    def binDir(self) -> Path:
        """App 의 작업 폴더 — App 은 거기서 위로 `Resource/` 를 찾고 보고 · 사진을 `Saved/Automation/` 에 쓴다."""
        return self.appPath.resolve().parent


@dataclass
class BackendResult:
    """한 백엔드의 결과 한 줄."""

    backend: str
    exitCode: int | None = None
    meaning: str = ""
    seconds: float = 0.0
    bPassed: bool = False
    report: ScenarioRunReport | None = None
    listNote: list[str] = field(default_factory=list)
    listCopied: list[Path] = field(default_factory=list)


# ------------------------------------------------------------------------------
# 풀기 — App · 시나리오 · 백엔드 · 게임
# ------------------------------------------------------------------------------
def resolveAppPath(appArgument: Path | None, configName: str | None, repositoryRoot: Path) -> Path:
    """돌릴 App — `--app` > `--config` 의 빌드 폴더 > 이 저장소의 첫 구성된 빌드(프리셋 순서). 없으면 `ScenarioRunError`."""
    if appArgument is not None and configName is not None:
        raise ScenarioRunError("--app 과 --config 는 함께 줄 수 없다 — App 경로가 구성을 이미 정한다")
    if appArgument is not None:
        if not appArgument.is_file():
            raise ScenarioRunError(f"--app {appArgument} 가 없다")
        return appArgument
    if configName is not None:
        appPath = BuildTree.fromPreset(kMapConfigPreset[configName], repositoryRoot).appExecutable
        if appPath is None:
            raise ScenarioRunError(f"build/{kMapConfigPreset[configName]} 에 App 이 없다 — 그 구성을 먼저 빌드한다(작업자는 통합에 맡긴다)")
        return appPath
    appPath = BuildTree.findAppExecutable(repositoryRoot)
    if appPath is None:
        raise ScenarioRunError("빌드된 App 이 없다 — --app 이나 --config 로 가리키거나 먼저 빌드한다")
    return appPath


def resolveScenario(resourceRoot: Path, query: str) -> ScenarioDeclaration:
    """이름 · 경로 조각을 시나리오 하나로 정한다. 없으면 `ScenarioRunError`, 둘 이상이면 후보를 문장에 담아 `ScenarioRunError`(모호하면 멈춘다)."""
    try:
        listCandidate = findScenarioCandidates(resourceRoot, query)
        if not listCandidate:
            raise ScenarioRunError(f"'{query}' 에 맞는 시나리오가 없다(찾은 곳: {resourceRoot.as_posix()}/engine/automation · game/*/automation)")
        if len(listCandidate) > 1:
            raise ScenarioRunError(f"'{query}' 가 모호하다 — 후보 {len(listCandidate)} 개 중 하나로 더 길게 적는다:\n" + "\n".join(f"  {candidate}" for candidate in listCandidate))
        return readScenarioDeclaration(resourceRoot, listCandidate[0])
    except ScenarioFileError as error:
        raise ScenarioRunError(str(error)) from error


def resolveBackends(backendText: str | None, tree: BuildTree) -> tuple[Backend, ...]:
    """`--backend` 를 백엔드 목록으로. 개발 빌드는 `all` 이 넷 다이고 기본은 dx12, 배포 빌드는 링크한 하나뿐이다."""
    listUsable = findUsableBackends(tree)
    if tree.bShipping:
        if backendText not in (None, "all"):
            spec = CookContractSpec.load().findBackend(backendText)
            if spec is None or spec.commandLineName not in listUsable:
                raise ScenarioRunError(f"이 배포(Shipping) 빌드는 {listUsable[0]} 하나만 링크했다 — --backend {backendText} 를 돌릴 수 없다")
        return tuple(Backend(name, "") for name in listUsable)
    if backendText == "all":
        return tuple(Backend(name, kBackendSwitch[name]) for name in listUsable)
    spec = CookContractSpec.load().findBackend(backendText or "dx12")
    if spec is None:
        raise ScenarioRunError(f"모르는 백엔드 '{backendText}' (dx12 · dx11 · vulkan · gl · all)")
    return (Backend(spec.commandLineName, kBackendSwitch[spec.commandLineName]),)


def resolveGameName(resourceRoot: Path, declaration: ScenarioDeclaration) -> str:
    """게임 팩 시나리오의 게임 이름(`Config/Game/<게임>.json` 의 `_packRoot` 가 팩 폴더인 것). 엔진 시나리오는 빈 글."""
    if not declaration.packFolder:
        return ""
    presetFolder = resourceRoot.parent / kGamePresetFolder
    for presetPath in sorted(presetFolder.glob("*.json")):
        try:
            packRoot = json.loads(presetPath.read_text(encoding="utf-8-sig")).get(kPackRootKey, "")
        except (OSError, ValueError) as error:
            raise ScenarioRunError(f"{presetPath.as_posix()} 를 읽지 못했다: {error}") from error
        if packRoot == declaration.packFolder:
            return presetPath.stem
    raise ScenarioRunError(f"{declaration.resourceId}: 팩 {declaration.packFolder} 를 팩으로 가진 게임이 {presetFolder.as_posix()} 에 없다 — 어느 게임으로 띄울지 정할 수 없다")


# 결정: `<Launch>` 가 없는 시나리오는 "기동 변수가 필요 없다" 는 선언이다(시나리오 187 개 중 168 개가 그렇다) — 거절하지 않고 변수 없이 돌린다.
# 근거: `AppScenarioTest` 가 같은 시나리오를 변수 없이 돈다. 거절하면 대부분의 시나리오를 못 돌려 이 명령이 쓸모없어진다.
def checkRunnable(declaration: ScenarioDeclaration, tree: BuildTree, gameName: str) -> None:
    """선언을 인자로 풀 수 없는 시나리오는 이유와 함께 멈춘다(폴백 없음)."""
    if declaration.bLoad:
        raise ScenarioRunError(f"{declaration.resourceId}: 부하(<Load>) 시나리오는 메시 수 · 회선을 실행기가 명령줄로 옮긴다 — `py -3 -m Scripts load run <이름>` 으로 돌린다")
    if declaration.bGroupMember:
        raise ScenarioRunError(f"{declaration.resourceId}: <묶음>.<n> 일원은 앞 실행이 남긴 저장 상태를 이어 읽는다 — 한 판으로 풀 수 없다(AppScenarioTest 의 묶음 실행이 같은 상태 폴더로 이어 돈다)")
    if not tree.bShipping:
        return
    if declaration.listLaunch or declaration.bEditor:
        raise ScenarioRunError(f"{declaration.resourceId}: 배포(Shipping) 빌드는 개발 변수(<Launch> 의 gv_)와 에디터를 받지 않는다 — Release 이하 구성으로 돌린다")
    activeGame = tree.readCacheValue("SW_ACTIVE_GAME") or ""
    if gameName and gameName.lower() != activeGame.lower():
        raise ScenarioRunError(f"{declaration.resourceId}: 배포(Shipping) 빌드는 게임 {activeGame or '(미상)'} 하나를 구웠다 — {gameName} 는 돌릴 수 없다")


# ------------------------------------------------------------------------------
# 인자 조립
# ------------------------------------------------------------------------------
def makeEditorStateFolder(plan: RunPlan, backend: Backend) -> Path:
    """에디터를 켠 실행의 상태 폴더 — 시나리오 · 백엔드마다 따로(`AppScenarioTest::makeEditorArguments` 와 같은 자리)."""
    return plan.binDir / kAutomationFolder / "EditorState" / f"{plan.declaration.stem}_{backend.name}"


def makeReportRelPath(plan: RunPlan, backend: Backend) -> str:
    """작업 폴더 기준 보고 경로 — `AppTestUtil::makeReportPath` 와 같은 이름이라 `scenario-logs` 가 그대로 읽는다."""
    return f"{kAutomationFolder}/{plan.declaration.stem}_{backend.name}.json"


def buildArguments(plan: RunPlan, backend: Backend) -> list[str]:
    """App 에 줄 인자. 순서: 무인 · 시나리오 · 보고 · 백엔드 · 게임 · `<Launch>` 변수 · 에디터."""
    listArgument = ["-unattended", f"-scenario={plan.declaration.resourceId}", f"-scenario-report={makeReportRelPath(plan, backend)}"]
    if backend.switch:
        listArgument.append(backend.switch)
    if plan.gameName and not plan.bShipping:
        listArgument.append(f"-game={plan.gameName}")
    listArgument += plan.declaration.makeLaunchArguments()
    if plan.declaration.bEditor:
        listArgument += ["-EnableEditor", f"-gv_editorStateDir={makeEditorStateFolder(plan, backend)}"]
    return listArgument


# ------------------------------------------------------------------------------
# 돌리기
# ------------------------------------------------------------------------------
def makeOutputFolder(plan: RunPlan) -> Path | None:
    """실행기가 사진 · 산출물을 쓰는 폴더(`Saved/Automation/<시나리오 name>`, `AutomationRunner::_outputDirectory`). name 이 없으면 None."""
    return plan.binDir / kAutomationFolder / plan.declaration.name if plan.declaration.name else None


def clearOutputFolder(plan: RunPlan) -> None:
    """실행 전에 이 시나리오의 산출물 폴더를 비운다 — 실행기는 이 폴더를 비우지 않아(`Source/Engine/Automation` 에 지우는 코드가 없다) 지난 판 사진이 이번 판과 섞인다."""
    folder = makeOutputFolder(plan)
    if folder is not None:
        shutil.rmtree(folder, ignore_errors=True)


def copyScreenshots(plan: RunPlan, backend: Backend, destination: Path) -> list[Path]:
    """이번 판이 쓴 산출물(`Saved/Automation/<name>/` 전부 — 실행 전에 비웠다)을 `<destination>/<백엔드>/` 로 복사한다. 복사한 파일 목록."""
    source = makeOutputFolder(plan)
    if source is None or not source.is_dir():
        return []
    listCopied: list[Path] = []
    for path in sorted(source.rglob("*")):
        if path.is_file():
            target = destination / backend.name / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
            listCopied.append(target)
    return listCopied


def readReportInternal(reportPath: Path, result: BackendResult) -> None:
    """보고를 읽어 결과에 붙인다. 없거나 틀리면 이유를 메모로 남긴다(조용히 넘기지 않는다)."""
    try:
        result.report = readRunReport(reportPath)
    except ValueError as error:
        result.listNote.append(f"보고 없음({reportPath.as_posix()}): {error}")


def judgeRun(plan: RunPlan, backend: Backend, listArgument: list[str], mapCode: dict[str, int], destination: Path | None, bExclusive: bool,
             owner: str, mainRoot: Path, waitSeconds: int) -> BackendResult:
    """백엔드 하나를 GPU 칸을 잡고 돌려 결과로 푼다."""
    result = BackendResult(backend.name)
    reportPath = plan.binDir / makeReportRelPath(plan, backend)
    reportPath.parent.mkdir(parents=True, exist_ok=True)
    reportPath.unlink(missing_ok=True)   # 낡은 보고를 이번 판의 것으로 읽지 않게
    if plan.declaration.bEditor:
        shutil.rmtree(makeEditorStateFolder(plan, backend), ignore_errors=True)   # 사용자 에디터 상태가 아니라 빈 상태에서 시작한다
    print(f"[Scenario] {backend.name}: {plan.appPath.as_posix()} {' '.join(listArgument)}", flush=True)
    with holdLock(mainRoot, "gpu", not bExclusive, owner, f"scenario {plan.declaration.stem} {backend.name}", waitSeconds) as lockCode:
        if lockCode != 0:
            result.meaning = f"GPU {'독점' if bExclusive else '공유'} 칸을 잡지 못함(종료 코드 {lockCode}) — 돌지 않았다"
            return result
        clearOutputFolder(plan)   # 잠금을 잡은 뒤에 비운다 — 같은 시나리오를 도는 다른 판의 산출물을 지우지 않게
        run = runApp(plan.appPath, listArgument, cwd=plan.binDir, timeoutSeconds=float(plan.timeoutSeconds))
    result.seconds = run.seconds
    result.exitCode = run.exitCode
    if not run.bLaunched:
        result.meaning = "App 을 띄우지 못함"
        result.listNote += run.listErrorLine
        return result
    for name in run.findIgnoredGlobals(listArgument):
        result.listNote.append(f"App 이 -{name} 를 모르는 전역 변수로 버렸다(<Launch> 가 이 빌드와 안 맞는다)")
    describeRun(run.bTimedOut, run.exitCode, plan, mapCode, result)
    readReportInternal(reportPath, result)
    if result.report is None and not result.bPassed:
        result.listNote.append("App 출력의 끝: " + " / ".join(run.listLine[-kTailLineCount:]))
    if destination is not None:
        result.listCopied = copyScreenshots(plan, backend, destination)
    return result


def describeRun(bTimedOut: bool, exitCode: int | None, plan: RunPlan, mapCode: dict[str, int], result: BackendResult) -> None:
    """종료 코드를 뜻으로 풀어 결과에 쓴다. 통과는 0 뿐이다(미확정 14 · 건너뜀 13 · 이 기계에서 못 돎 77 도 통과가 아니다)."""
    if bTimedOut:
        result.meaning = f"시한 {plan.timeoutSeconds} 초를 넘겨 프로세스를 죽임(--timeout 으로 늘린다)"
        result.exitCode = None
    elif exitCode == kAppRhiUnusableExitCode:
        result.meaning = "이 기계에서 이 백엔드를 못 돎(환경 — 빌드에 없거나 드라이버가 기능을 안 줌)"
    else:
        result.meaning = describeOutcomeExitCode(mapCode, exitCode if exitCode is not None else -1)
    result.bPassed = not bTimedOut and exitCode == mapCode["Passed"]


# ------------------------------------------------------------------------------
# 표
# ------------------------------------------------------------------------------
def formatSummary(listResult: list[BackendResult]) -> list[str]:
    """백엔드별 결과를 한 표로."""
    listLine = ["| 백엔드 | 종료 코드 | 결과 | 시간 |", "| --- | --- | --- | --- |"]
    for result in listResult:
        code = kNoExitCodeText if result.exitCode is None else str(result.exitCode)
        listLine.append(f"| {result.backend} | {code} | {result.meaning} | {result.seconds:.1f} 초 |")
    return listLine


def collectDetailRows(listResult: list[BackendResult]) -> list[tuple[str, str, str]]:
    """(백엔드, 종류, 내용) — `failures[]` · `unconfirmed[]` · `unhandledLogs[]` 와 보고 이유 · 메모. 처리하지 않은 로그는 줄 그대로다."""
    listRow: list[tuple[str, str, str]] = []
    for result in listResult:
        if result.report is not None and not result.bPassed and result.report.reason:
            listRow.append((result.backend, "이유", result.report.reason.replace("\n", " / ")))
        if result.report is not None:
            listRow += [(result.backend, "실패", item.replace("\n", " / ")) for item in result.report.listFailure]
            listRow += [(result.backend, "미확정", item.replace("\n", " / ")) for item in result.report.listUnconfirmed]
            listRow += [(result.backend, "처리하지 않은 로그", item) for item in result.report.listUnhandled]
        listRow += [(result.backend, "메모", note) for note in result.listNote]
    return listRow


def formatDetails(listResult: list[BackendResult]) -> list[str]:
    """세부 표. 보일 것이 없으면 빈 목록."""
    listRow = collectDetailRows(listResult)
    if not listRow:
        return []
    return ["| 백엔드 | 종류 | 내용 |", "| --- | --- | --- |", *(f"| {backend} | {kind} | {text} |" for backend, kind, text in listRow)]


# ------------------------------------------------------------------------------
# 진입
# ------------------------------------------------------------------------------
def makePlan(args: argparse.Namespace, repositoryRoot: Path) -> RunPlan:
    """인자를 돌릴 일 하나로 푼다. 풀 수 없으면 `ScenarioRunError`."""
    appPath = resolveAppPath(args.app, args.config, repositoryRoot)
    try:
        resourceRoot = findResourceRoot(appPath.resolve().parent)
    except ScenarioFileError as error:
        raise ScenarioRunError(str(error)) from error
    tree = BuildTree.ofApp(appPath)
    declaration = resolveScenario(resourceRoot, args.scenario)
    gameName = resolveGameName(resourceRoot, declaration)
    checkRunnable(declaration, tree, gameName)
    return RunPlan(declaration, appPath, resolveBackends(args.backend, tree), gameName, tree.bShipping, args.timeout)


def runPlan(plan: RunPlan, args: argparse.Namespace, repositoryRoot: Path) -> list[BackendResult]:
    """계획의 백엔드들을 차례로 돌린다(백엔드마다 GPU 칸을 잡고 놓는다)."""
    mapCode = readOutcomeExitCodes(repositoryRoot / kExitCodeHeaderRelPath)
    mainRoot = args.lock_root or findMainRoot(repositoryRoot)
    owner = args.owner or repositoryRoot.name
    bExclusive = plan.declaration.bForeground
    return [judgeRun(plan, backend, buildArguments(plan, backend), mapCode, args.screenshot_dir, bExclusive, owner, mainRoot, args.wait)
            for backend in plan.listBackend]


def printResults(plan: RunPlan, listResult: list[BackendResult]) -> None:
    """결과 표 · 세부 표 · 복사한 사진 · 마지막 한 줄."""
    print(f"\n[Scenario] {plan.declaration.resourceId} ({plan.appPath.as_posix()})")
    print("\n".join(formatSummary(listResult)))
    listDetail = formatDetails(listResult)
    if listDetail:
        print("\n" + "\n".join(listDetail))
    for result in listResult:
        for path in result.listCopied:
            print(f"[Scenario] {result.backend} 산출물: {path.as_posix()}")
    countFailed = sum(1 for result in listResult if not result.bPassed)
    print(f"\n[Scenario] 결과: {'통과' if countFailed == 0 else '통과 아님'} — 백엔드 {len(listResult)} 개 중 통과가 아닌 것 {countFailed} 개")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="시나리오 하나를 그 시나리오가 선언한(<Launch> …) 인자 그대로 백엔드마다 돌려 종료 코드를 뜻으로 풀고 결과를 한 표로 보인다.")
    parser.add_argument("scenario", help="시나리오 이름 조각 · 리소스 경로 · 파일 경로 — 부분 일치가 하나로 정해질 때만 받는다(모호하면 후보를 보이고 멈춘다)")
    parser.add_argument("--backend", default=None, help="dx12 · dx11 · vulkan(vk) · gl · all (기본 dx12, 배포 빌드는 링크한 하나)")
    parser.add_argument("--config", choices=list(kMapConfigPreset), default=None, help="build/Ninja-<구성>/Bin/App 을 쓴다(기본: 이 저장소의 첫 구성된 빌드). --app 과 함께 줄 수 없다")
    parser.add_argument("--app", type=Path, default=None, help="빌드된 App 실행 파일(이 App 의 Resource/ 가 시나리오를 찾는 곳이다)")
    parser.add_argument("--screenshot-dir", type=Path, default=None, help="이번 판이 쓴 사진 · 산출물을 <폴더>/<백엔드>/ 로 복사한다")
    parser.add_argument("--timeout", type=int, default=kDefaultTimeoutSeconds, help=f"백엔드 한 판의 시한(초, 기본 {kDefaultTimeoutSeconds})")
    parser.add_argument("--dry-run", dest="bDryRun", action="store_true", help="조립한 명령만 찍고 돌리지 않는다")
    parser.add_argument("--owner", default="", help="GPU 잠금에 적을 소유자 이름(기본: 이 워크트리 폴더 이름)")
    parser.add_argument("--wait", type=int, default=kDefaultWaitSeconds, help=f"GPU 칸을 기다릴 최대 초(기본 {kDefaultWaitSeconds})")
    parser.add_argument("--lock-root", type=Path, default=None, help="공유 잠금을 둘 main 체크아웃(기본: 이 저장소의 main). 시험용")
    args = parser.parse_args(argv)
    repositoryRoot = getProjectRoot()
    try:
        plan = makePlan(args, repositoryRoot)
        if args.bDryRun:
            for backend in plan.listBackend:
                print(f"{plan.appPath.as_posix()} {' '.join(buildArguments(plan, backend))}")
            return ExitCode.Passed
        listResult = runPlan(plan, args, repositoryRoot)
    except ScenarioRunError as error:
        print(f"[Scenario] 돌릴 수 없다: {error}", file=sys.stderr)
        return ExitCode.CheckInvalid
    printResults(plan, listResult)
    return ExitCode.Passed if all(result.bPassed for result in listResult) else ExitCode.Violation


if __name__ == "__main__":
    sys.exit(main())
