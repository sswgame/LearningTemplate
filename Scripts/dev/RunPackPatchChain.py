#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
패치 사슬 한 판 — 빌드 1.0 팩을 굽고, 파일 하나를 더해 1.1 로 다시 굽고, 패치를 만들어 `Packs/` 에 둔 뒤 App 이 시작해 사슬을 얹는지 본다.

    py -3 -m Scripts pack-patch-run --app build/Ninja-Debug/Bin/App.exe --stage-dir Saved/Packages/Client_Empty --game Empty

단계(`[pack-patch] step k/6 <이름>` 한 줄씩): cook-base(`-cook-all -pack-build=1.0`) → add-probe(원본 트리에 시험 파일 하나) → cook-new(`-pack-build=1.1`) →
make-patch(`-make-patch`) → stage(`<stage-dir>/Packs` 를 기본 팩 + 패치로 바꾼다) → start(App 을 짧게 띄우고 `Applied 1 pack patch(es): build 1.0 -> 1.1` 줄을 본다).
시험 파일은 만들었다가 어떤 길로 끝나도 지운다(`Resource/common/data/packpatch_probe.txt` — 같은 트리에서 다른 쿠킹이 도는 중이면 겹치므로 혼자 돌릴 때만 쓴다).

`--stage-dir` 은 배포 패키지 폴더(`MakePackage.py` 의 산출물, 안에 App 과 `Packs/`)다. App 은 실행 파일 옆 `Packs/` 를 먼저 찾으므로 빌드 `Bin` 을 주면 안 된다 —
`Bin/Packs` 를 지우기 때문에 빌드 폴더 안은 거절한다. 판정: 모든 단계 성공, 시작이 종료 코드 0 · `[Error]` 0 줄, 위 로그 줄이 있다.
"""

from __future__ import annotations

import argparse
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.AppRun import kBackendSwitch, loadGameTable, runApp  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.QualityPreset import SettingsIsolation, readTableQuality, writePresetSettingsFile  # noqa: E402

kListStepName: tuple[str, ...] = ("cook-base", "add-probe", "cook-new", "make-patch", "stage", "start")
kBaseBuild = "1.0"
kNewBuild = "1.1"
kBuildRecordFileName = "base.packbuild.xml"  # `PackPatchUtil::kBuildRecordFileName` 과 같은 이름
kProbeRelPath = "Resource/common/data/packpatch_probe.txt"
kAppliedLine = f"Applied 1 pack patch(es): build {kBaseBuild} -> {kNewBuild}"  # `ResourcePakManager::mountPatchChain` 의 로그 줄


class ChainError(RuntimeError):
    """단계 하나가 실패했다. 메시지가 `[pack-patch] FAILED` 줄의 이유다."""


def printStep(index: int) -> None:
    print(f"[pack-patch] step {index + 1}/{len(kListStepName)} {kListStepName[index]}", flush=True)


def runHeadless(appPath: Path, listArgument: list[str], stepName: str) -> list[str]:
    """쿠킹 · 패치 만들기 같은 헤드리스 명령을 돌린다. 종료 코드가 0 이 아니거나 `[Error]` 줄이 있으면 실패다. 출력 줄을 돌려준다."""
    result = runApp(appPath, listArgument, settings=SettingsIsolation.NOT_APPLICABLE, timeoutSeconds=1800.0)
    if not result.bLaunched or not result.bClean:
        detail = result.listErrorLine[:3] if result.listErrorLine else [f"exit {result.exitCode}, timed out={result.bTimedOut}"]
        raise ChainError(f"{stepName}: App did not finish cleanly: {detail}")
    return result.listLine


def findStageApp(stageDir: Path) -> Path:
    """배포 폴더의 실행 파일. 없으면 멈춘다(다른 이름으로 대신하지 않는다)."""
    name = "App.exe" if sys.platform == "win32" else "App"
    appPath = stageDir / name
    if not appPath.is_file():
        raise ChainError(f"{stageDir} has no {name} - pass the folder MakePackage.py staged")
    return appPath


def checkStageDirOutsideBuild(stageDir: Path, projectRoot: Path) -> None:
    """빌드 폴더 안이면 거절한다 — 아래에서 `Packs/` 를 지우므로 빌드 `Bin/Packs` 를 잃는다."""
    buildRoot = (projectRoot / "build").resolve()
    resolved = stageDir.resolve()
    if resolved == buildRoot or buildRoot in resolved.parents:
        raise ChainError(f"{stageDir} is inside the build folder - stage a package with MakePackage.py and pass that folder")


def replacePacksFolder(stageDir: Path, listSourceDir: list[Path]) -> Path:
    """`<stageDir>/Packs` 를 비우고 원본 폴더들의 팩 · 목록 파일을 옮겨 둔다. 만든 폴더를 돌려준다."""
    packsDir = stageDir / "Packs"
    if packsDir.exists():
        shutil.rmtree(packsDir)
    packsDir.mkdir(parents=True)
    for sourceDir in listSourceDir:
        for source in sorted(sourceDir.iterdir()):
            if source.is_file() and (source.suffix == ".pack" or source.name.endswith(".packpatch.xml") or source.name == kBuildRecordFileName):
                shutil.copy2(source, packsDir / source.name)
    return packsDir


def runChain(args: argparse.Namespace, workDir: Path, projectRoot: Path) -> None:
    appPath: Path = args.app
    stageDir: Path = args.stage_dir
    checkStageDirOutsideBuild(stageDir, projectRoot)
    stageApp = findStageApp(stageDir)
    baseDir, newDir, patchDir = workDir / "base", workDir / "new", workDir / "patch"
    gameArgument = f"-game={args.game}"

    printStep(0)
    runHeadless(appPath, [gameArgument, "-cook-all", f"-cooked-dir={workDir / 'cooked_base'}", f"-pack-dir={baseDir}", f"-pack-build={kBaseBuild}"], "cook-base")
    if not (baseDir / kBuildRecordFileName).is_file():
        raise ChainError(f"cook-base: {baseDir} has no {kBuildRecordFileName}")

    probePath = projectRoot / kProbeRelPath
    if probePath.exists():
        raise ChainError(f"{probePath} already exists - another run is using it (delete it if that run is gone)")
    printStep(1)
    probePath.write_text("pack patch probe\n", encoding="utf-8")
    try:
        printStep(2)
        runHeadless(appPath, [gameArgument, "-cook-all", f"-cooked-dir={workDir / 'cooked_new'}", f"-pack-dir={newDir}", f"-pack-build={kNewBuild}"], "cook-new")
    finally:
        probePath.unlink(missing_ok=True)

    printStep(3)
    listMakeArgument = [gameArgument, "-make-patch", f"-pack-dir={newDir}", f"-patch-base={baseDir / kBuildRecordFileName}", f"-pack-build={kNewBuild}", f"-patch-dir={patchDir}"]
    listLine = runHeadless(appPath, listMakeArgument, "make-patch")
    if not any(path.name.endswith(".packpatch.xml") for path in patchDir.iterdir()):
        raise ChainError(f"make-patch: no patch list in {patchDir}: {listLine[-3:]}")

    printStep(4)
    packsDir = replacePacksFolder(stageDir, [baseDir, patchDir])
    print(f"[pack-patch] staged {sorted(path.name for path in packsDir.iterdir())} in {packsDir}", flush=True)

    printStep(5)
    gameTable = loadGameTable(projectRoot)
    listStartArgument = [gameArgument, *gameTable["games"][args.game]["arguments"], "-gv_profileFrames=3", kBackendSwitch[args.backend],
                         *writePresetSettingsFile(projectRoot, readTableQuality(gameTable), workDir / "usersettings.json")]
    result = runApp(stageApp, listStartArgument, settings=SettingsIsolation.PRESET_FILE, cwd=stageDir, timeoutSeconds=300.0)
    if result.bBackendUnusable:
        raise ChainError(f"start: {args.backend} is not usable on this machine")
    if not result.bClean:
        detail = result.listErrorLine[:3] if result.listErrorLine else [f"exit {result.exitCode}, timed out={result.bTimedOut}"]
        raise ChainError(f"start: App did not finish cleanly: {detail}")
    if not any(kAppliedLine in line for line in result.listLine):
        raise ChainError(f"start: the log has no '{kAppliedLine}' line - the chain was not applied")


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="패치 사슬(1.0 구움 → 1.1 구움 → 패치 만들기 → Packs/ 에 두고 App 시작)을 한 명령으로 돌린다")
    parser.add_argument("--app", type=Path, required=True, help="쿠킹 · 패치 만들기에 쓸 빌드된 App 실행 파일")
    parser.add_argument("--stage-dir", type=Path, required=True, help="배포 패키지 폴더(App 과 Packs/ 가 있다). 안의 Packs/ 를 바꾼다")
    parser.add_argument("--game", default="Empty", help="게임 이름(Test/QA/Games.json 에 있어야 한다)")
    parser.add_argument("--backend", default="dx12", choices=list(kBackendSwitch), help="시작 때 쓸 백엔드")
    args = parser.parse_args(listArgument)

    projectRoot = getProjectRoot()
    if not args.app.is_file():
        print(f"[pack-patch] App 이 없다: {args.app}", file=sys.stderr)
        return int(ExitCode.PrerequisiteMissing)
    if args.game not in loadGameTable(projectRoot)["games"]:
        print(f"[pack-patch] 모르는 게임 '{args.game}' - Test/QA/Games.json 에 더하십시오", file=sys.stderr)
        return int(ExitCode.Violation)
    with tempfile.TemporaryDirectory(prefix="sw_packpatch_") as tempDir:
        try:
            runChain(args, Path(tempDir), projectRoot)
        except ChainError as error:
            print(f"[pack-patch] FAILED {error}", flush=True)
            return int(ExitCode.Violation)
    print(f"[pack-patch] done - build {kBaseBuild} + patch -> {kNewBuild} applied by {args.stage_dir}", flush=True)
    return int(ExitCode.Passed)


if __name__ == "__main__":
    sys.exit(main())
