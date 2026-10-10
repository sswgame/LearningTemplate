#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file MakePackage.py
@brief 배포 패키지 하나를 만든다 — Shipping 빌드, 리소스 팩 쿠킹, 스테이징(실행 파일 · DLL · 팩 · 서버 설정 · 서드파티 고지), 검사.

에디터의 Packaging 창과 사람이 같은 진입점을 쓴다(언리얼 Package Project, 유니티 Build Settings 의 Build). 단계마다
`[package] step k/4 <단계>` 한 줄을 찍고, 끝나면 `[package] done <폴더> <바이트>` 또는 `[package] FAILED <단계> <이유>` 와 종료 코드(0 · 1)다.
창은 이 줄을 읽어 진행과 결과를 보인다(`PackagingProgressParser`).

| 타깃 | 프리셋 | 실행 파일 | 쿠킹에서 빠지는 것 |
| --- | --- | --- | --- |
| `Client` | `Ninja-Shipping` | `App.exe` | 없음 |
| `Server` | `Ninja-Shipping-Server` | `Server.exe` | `CookContract.json` 의 `target_excluded_asset_kinds`(텍스처 · 셰이더 바이너리 · 오디오) |

스테이징은 빌드 폴더 `Bin` 의 맨 위 DLL 과 그 타깃의 실행 파일, `Packs/`, `THIRD_PARTY_NOTICES.txt` 만 베낀다.
Dev 산출물(`Modules/` · `Saved/` · `Symbols/`)과 다른 실행 파일(시험 · 부하 봇)은 들이지 않는다. 서버는 `Config/Server/<게임>.json` 을 함께 둔다
(서버는 Shipping 에서도 운영 설정을 디스크에서 읽는다).

사용법:
    py -3 Scripts/dev/MakePackage.py --target Client --game Empty
    py -3 Scripts/dev/MakePackage.py --target Server --game Shooter3D --rhi DirectX12 --output Saved/Packages
    py -3 Scripts/dev/MakePackage.py --target Client --skip-build --skip-cook --bin-dir build/Ninja-Debug/Bin   # 스테이징만(창의 시나리오)
"""

from __future__ import annotations

import argparse
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common import BuildTree, getProjectRoot, runProcess  # noqa: E402

#: 단계 이름 — 창이 `[package] step k/n <이름>` 으로 읽는다. 순서가 진행 순서다.
kListStepName: tuple[str, ...] = ("build", "cook", "stage", "verify")
#: 타깃마다 Shipping 프리셋과 실행 파일 이름.
kMapTargetPreset: dict[str, str] = {"Client": "Ninja-Shipping", "Server": "Ninja-Shipping-Server"}
kMapTargetExecutable: dict[str, str] = {"Client": "App.exe" if sys.platform == "win32" else "App",
                                        "Server": "Server.exe" if sys.platform == "win32" else "Server"}
#: 실행 파일 옆에 두는 공유 라이브러리 확장자.
kListLibrarySuffix: tuple[str, ...] = (".dll",) if sys.platform == "win32" else (".so",)
#: Bin 에서 베끼는 폴더 · 파일.
kPackFolderName = "Packs"
kNoticeFileName = "THIRD_PARTY_NOTICES.txt"


class PackageError(Exception):
    """단계 하나가 실패했다. 메시지가 `[package] FAILED` 줄의 이유다."""


@dataclass
class PackageRequest:
    """한 번의 패키징 요청."""

    target: str
    game: str
    rhi: str
    outputRoot: Path
    binDir: Path | None = None
    bSkipBuild: bool = False
    bSkipCook: bool = False
    currentStep: str = kListStepName[0]
    listCopied: list[Path] = field(default_factory=list)


def beginStep(request: PackageRequest, stepIndex: int) -> None:
    """`[package] step k/n <이름>` 한 줄을 찍고 지금 단계를 적는다(창의 진행 막대가 읽고, 실패 줄이 단계 이름을 쓴다)."""
    request.currentStep = kListStepName[stepIndex]
    print(f"[package] step {stepIndex + 1}/{len(kListStepName)} {request.currentStep}", flush=True)


def makeStagingDir(outputRoot: Path, game: str, target: str) -> Path:
    """`<산출 폴더>/<게임>-<타깃>` 입니다."""
    return outputRoot / f"{game}-{target}"


def selectStagedEntries(binDir: Path, target: str) -> list[Path]:
    """스테이징이 베끼는 Bin 의 항목입니다(그 타깃의 실행 파일, 맨 위 공유 라이브러리, 팩 폴더, 서드파티 고지)."""
    listEntry: list[Path] = []
    executable = binDir / kMapTargetExecutable[target]
    if executable.is_file():
        listEntry.append(executable)
    for path in sorted(binDir.iterdir()):
        if path.is_file() and path.suffix.lower() in kListLibrarySuffix:
            listEntry.append(path)
    for name in (kPackFolderName, kNoticeFileName):
        path = binDir / name
        if path.exists():
            listEntry.append(path)
    return listEntry


def measureBytes(path: Path) -> int:
    """폴더 안 파일 크기의 합입니다."""
    return sum(file.stat().st_size for file in path.rglob("*") if file.is_file())


def runBuild(request: PackageRequest, projectRoot: Path) -> None:
    """Shipping 프리셋을 그 게임 · 백엔드로 구성하고 빌드한다(빌드에 리소스 팩 쿠킹이 든다)."""
    preset = kMapTargetPreset[request.target]
    listConfigure = ["cmake", "--preset", preset, f"-DSW_ACTIVE_GAME={request.game}"]
    if request.rhi and request.target == "Client":
        listConfigure.append(f"-DSW_SHIPPING_RHI_BACKEND={request.rhi}")
    if runProcess(listConfigure, cwd=projectRoot, bCapture=False).returnCode != 0:
        raise PackageError(f"cmake --preset {preset} failed")
    if runProcess(["cmake", "--build", "--preset", preset], cwd=projectRoot, bCapture=False).returnCode != 0:
        raise PackageError(f"cmake --build --preset {preset} failed")


def runCook(request: PackageRequest, projectRoot: Path, binDir: Path) -> None:
    """리소스 팩을 그 타깃 · 백엔드로 다시 쿠킹해 Bin/Packs 에 둔다(빌드를 건너뛰고 쿠킹만 새로 할 때)."""
    executable = binDir / kMapTargetExecutable[request.target]
    listCommand = [sys.executable, str(projectRoot / "Scripts" / "generate" / "CookAssets.py"), "--all", "--out", str(binDir / kPackFolderName),
                   "--cooked-dir", str(binDir.parent / "Cooked"), "--build-target", request.target]
    if executable.is_file():
        listCommand += ["--app", str(executable)]
    if request.rhi:
        listCommand += ["--target-rhi", request.rhi]
    if runProcess(listCommand, cwd=projectRoot, bCapture=False).returnCode != 0:
        raise PackageError("CookAssets.py failed")


def stagePackage(request: PackageRequest, projectRoot: Path, binDir: Path, stagingDir: Path) -> None:
    """스테이징 폴더를 비우고 Bin 의 배포 항목과 서버 설정을 베낀다."""
    if stagingDir.exists():
        shutil.rmtree(stagingDir)
    stagingDir.mkdir(parents=True)
    for entry in selectStagedEntries(binDir, request.target):
        destination = stagingDir / entry.name
        if entry.is_dir():
            shutil.copytree(entry, destination)
        else:
            shutil.copy2(entry, destination)
        request.listCopied.append(destination)
    if (binDir / kPackFolderName).is_dir() is False:
        print(f"[package] warning: no {kPackFolderName}/ in {binDir} - the package runs only with loose Resource/ next to it", flush=True)
    if request.target == "Server":
        serverConfig = projectRoot / "Config" / "Server" / f"{request.game}.json"
        if serverConfig.is_file():
            destination = stagingDir / "Config" / "Server" / serverConfig.name
            destination.parent.mkdir(parents=True)
            shutil.copy2(serverConfig, destination)
            request.listCopied.append(destination)


def verifyPackage(request: PackageRequest, stagingDir: Path) -> None:
    """실행 파일이 있고, 서버 패키지에 클라이언트 실행 파일이 들지 않았는지 본다."""
    if (stagingDir / kMapTargetExecutable[request.target]).is_file() is False:
        raise PackageError(f"{kMapTargetExecutable[request.target]} is missing from the package")
    otherTarget = "Client" if request.target == "Server" else "Server"
    if (stagingDir / kMapTargetExecutable[otherTarget]).exists():
        raise PackageError(f"{kMapTargetExecutable[otherTarget]} leaked into the {request.target} package")


def resolveBinDir(request: PackageRequest, projectRoot: Path) -> Path:
    """`--bin-dir` 이 있으면 그것, 없으면 타깃 프리셋의 빌드 폴더 `Bin` 입니다."""
    if request.binDir is not None:
        return request.binDir if request.binDir.is_absolute() else projectRoot / request.binDir
    return BuildTree.fromPreset(kMapTargetPreset[request.target], projectRoot).binDir


def makePackage(request: PackageRequest, projectRoot: Path) -> Path:
    """네 단계를 돌리고 스테이징 폴더를 돌려준다. 실패하면 `PackageError` 이고, 실패한 단계는 `request.currentStep` 이다."""
    binDir = resolveBinDir(request, projectRoot)
    outputRoot = request.outputRoot if request.outputRoot.is_absolute() else projectRoot / request.outputRoot
    stagingDir = makeStagingDir(outputRoot, request.game, request.target)

    beginStep(request, 0)
    if request.bSkipBuild is False:
        runBuild(request, projectRoot)
    if binDir.is_dir() is False:
        raise PackageError(f"no build output at {binDir}")

    beginStep(request, 1)
    if request.bSkipCook is False and request.bSkipBuild:
        runCook(request, projectRoot, binDir)

    beginStep(request, 2)
    stagePackage(request, projectRoot, binDir, stagingDir)

    beginStep(request, 3)
    verifyPackage(request, stagingDir)
    return stagingDir


def parseArguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Shipping 빌드 · 쿠킹 · 스테이징으로 배포 패키지 하나를 만든다")
    parser.add_argument("--target", choices=sorted(kMapTargetPreset), default="Client", help="빌드 타깃(Client · Server)")
    parser.add_argument("--game", default="Empty", help="게임 프리셋 이름(Config/Game/<이름>.json, SW_ACTIVE_GAME)")
    parser.add_argument("--rhi", default="", help="Shipping 이 정적으로 링크할 백엔드(쿠킹 표 이름 — DirectX12 · DirectX11 · Vulkan · OpenGL)")
    parser.add_argument("--output", default="Saved/Packages", help="산출 폴더(저장소 기준 상대 경로 가능). 패키지는 <산출 폴더>/<게임>-<타깃>")
    parser.add_argument("--bin-dir", default="", help="스테이징할 빌드의 Bin(기본: 타깃 프리셋의 build/<프리셋>/Bin)")
    parser.add_argument("--skip-build", action="store_true", help="구성 · 빌드를 건너뛴다(이미 빌드된 Bin 을 쓴다)")
    parser.add_argument("--skip-cook", action="store_true", help="쿠킹을 건너뛴다(Bin/Packs 를 그대로 쓴다). 빌드를 하면 빌드가 쿠킹한다")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parseArguments(argv)
    request = PackageRequest(target=args.target, game=args.game, rhi=args.rhi, outputRoot=Path(args.output),
                             binDir=Path(args.bin_dir) if args.bin_dir else None, bSkipBuild=args.skip_build, bSkipCook=args.skip_cook)
    projectRoot = getProjectRoot()
    try:
        stagingDir = makePackage(request, projectRoot)
    except (PackageError, OSError) as error:
        print(f"[package] FAILED {request.currentStep} {error}", flush=True)
        return 1
    print(f"[package] done {stagingDir} {measureBytes(stagingDir)}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
