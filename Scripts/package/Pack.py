#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file Pack.py
@brief 배포 패키지 하나를 만든다 — Shipping 빌드, 리소스 팩 쿠킹, 스테이징(실행 파일 · DLL · 팩 · 서버 설정 · 서드파티 고지), 검사.

에디터의 Packaging 창과 사람이 같은 진입점을 쓴다(언리얼 Package Project, 유니티 Build Settings 의 Build). 단계마다
`[package] step k/4 <단계>` 한 줄을 찍고, 끝나면 `[package] done <폴더> <바이트>` 또는 `[package] FAILED <단계> <이유>` 와 종료 코드(0 · 1)다.
창은 이 줄을 읽어 진행과 결과를 보인다(`PackagingProgressParser`).

| 타깃 | 프리셋 | 실행 파일 | 쿠킹에서 빠지는 것 |
| --- | --- | --- | --- |
| `Client` | `Ninja-Shipping` | `App.exe` | 없음 |
| `Server` | `Ninja-Shipping-Server` | `Server.exe` | `CookContract.json` 의 `target_excluded_asset_kinds`(텍스처 · 셰이더 바이너리 · 오디오) |

스테이징은 빌드 폴더 `Bin` 의 맨 위 DLL 과 그 타깃의 실행 파일, `THIRD_PARTY_NOTICES.txt`, 그리고 `Packs/` 가운데 공용 팩과 그 게임의 팩만 베낀다
(`Bin/Packs` 에는 모든 게임의 팩이 구워져 있다 — 이름은 `listPackNameOfGame` 이 게임 프리셋의 `_packRoot` 로 정한다). 쿠킹은 그 타깃의 실행 파일이
`--cook-all` 로 한다(엔진의 `PackCooker`, 팩 파일 이름 규칙 `PackCooker::makePackFileName`). 이름이 어긋나면 스테이징이 빠진 팩을 알리고 멈춘다.
Dev 산출물(`Modules/` · `Saved/` · `Symbols/`)과 다른 실행 파일(테스트 · 부하 봇)은 들이지 않는다. 서버는 `Config/Server/<게임>.json` 을 함께 둔다
(서버는 Shipping 에서도 운영 설정을 디스크에서 읽는다).

사용법:
    py -3 Scripts/package/Pack.py --target Client --game Empty
    py -3 Scripts/package/Pack.py --target Server --game Shooter3D --rhi DirectX12 --output Saved/Packages
    py -3 Scripts/package/Pack.py --target Client --skip-build --skip-cook --bin-dir build/Ninja-Debug/Bin   # 스테이징만(창의 시나리오)
"""

from __future__ import annotations

import argparse
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ToolchainConfig import ConfigError, requireConfig  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.ProjectConstants import kDirRuntimeGamePreset  # noqa: E402
from common.RunProcess import runProcess  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402

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


#: 런타임이 늘 올리는 공용 팩. 게임 팩은 `game_<팩 폴더>.pack` 이다(`PackCooker::makePackFileName` 과 같은 이름).
kListSharedPackName: tuple[str, ...] = ("engine.pack", "common.pack")


def listPackNameOfGame(projectRoot: Path, game: str) -> list[str]:
    """그 게임의 배포 패키지가 담는 팩 파일 이름들입니다 — 공용 팩과 그 게임의 팩 하나.

    팩 폴더는 게임 프리셋(`Config/Game/<게임>.json`)의 `_packRoot`(`game/<폴더>`)에서 읽는다 — 게임 이름과 폴더 이름이 다를 수 있다(ThemeParkTycoon → themepark).
    프리셋이나 키가 없으면 ConfigError.
    """
    presetPath = projectRoot / kDirRuntimeGamePreset / f"{game}.json"
    packRoot = str(requireConfig(presetPath, presetPath.name).get("_packRoot", ""))
    folderName = packRoot.removeprefix("game/")
    if not packRoot.startswith("game/") or not folderName or "/" in folderName:
        raise ConfigError(f"{presetPath}: `_packRoot` 는 `game/<폴더>` 입니다 (`{packRoot}`)")
    return [*kListSharedPackName, f"game_{folderName.lower()}.pack"]


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
    """스테이징이 베끼는 Bin 의 항목입니다(그 타깃의 실행 파일, 맨 위 공유 라이브러리, 서드파티 고지). 팩은 `selectStagedPacks` 가 고른다."""
    listEntry: list[Path] = []
    executable = binDir / kMapTargetExecutable[target]
    if executable.is_file():
        listEntry.append(executable)
    for path in sorted(binDir.iterdir()):
        if path.is_file() and path.suffix.lower() in kListLibrarySuffix:
            listEntry.append(path)
    notice = binDir / kNoticeFileName
    if notice.exists():
        listEntry.append(notice)
    return listEntry


def selectStagedPacks(projectRoot: Path, packDir: Path, game: str) -> list[Path]:
    """패키지에 담는 팩입니다 — 공용 팩과 그 게임의 팩만(`listPackNameOfGame`).

    Bin/Packs 는 Dev · Shipping 빌드가 모든 게임의 팩을 굽고 지난 쿠킹의 팩도 남는 폴더라 통째로 베끼지 않는다.
    이름은 게임 프리셋에서 고르고, 그 팩이 없으면 실패다(빠진 팩을 둔 패키지는 기동하지 못한다).
    """
    try:
        listName = listPackNameOfGame(projectRoot, game)
    except ConfigError as error:
        raise PackageError(str(error)) from error
    listMissing = [name for name in listName if (packDir / name).is_file() is False]
    if listMissing:
        raise PackageError(f"{packDir} has no {', '.join(listMissing)} for game {game} - cook first")
    return [packDir / name for name in listName]


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
    """리소스 팩을 그 타깃의 실행 파일(`--cook-all`)로 다시 쿠킹해 Bin/Packs 에 둔다(빌드를 건너뛰고 쿠킹만 새로 할 때).

    쿠킹하는 호스트가 빌드 타깃이다 — `Server` 실행 파일이 구운 팩은 서버가 읽지 않는 종류를 뺀다. 실행 파일이 없으면 다른 빌드로 대신하지 않고 멈춘다.
    """
    executable = binDir / kMapTargetExecutable[request.target]
    if executable.is_file() is False:
        raise PackageError(f"{executable} is missing - build the {request.target} target before cooking")
    listCommand = [str(executable), "--cook-all", f"--cooked-dir={binDir.parent / 'Cooked'}", f"--pack-dir={binDir / kPackFolderName}", f"--game={request.game}"]
    if request.target == "Client":
        listCommand.append("--verify-shaders")
    if request.rhi:
        listCommand.append(f"--{findBackendSwitchInternal(request.rhi)}")
    if runProcess(listCommand, cwd=projectRoot, bCapture=False).returnCode != 0:
        raise PackageError(f"{executable.name} --cook-all failed")


def findBackendSwitchInternal(rhi: str) -> str:
    """쿠킹 표의 백엔드 이름(`DirectX12`)을 App 명령줄 이름(`dx12`)으로 바꿉니다. 모르는 이름이면 실패다."""
    from common.CookTable import CookContractSpec
    backend = CookContractSpec.load().findBackend(rhi)
    if backend is None:
        raise PackageError(f"unknown RHI backend '{rhi}'")
    return backend.commandLineName


def stagePackage(request: PackageRequest, projectRoot: Path, binDir: Path, stagingDir: Path) -> None:
    """스테이징 폴더를 비우고 Bin 의 배포 항목, 그 게임의 팩, 서버 설정을 베낀다."""
    if stagingDir.exists():
        shutil.rmtree(stagingDir)
    stagingDir.mkdir(parents=True)
    for entry in selectStagedEntries(binDir, request.target):
        destination = stagingDir / entry.name
        shutil.copy2(entry, destination)
        request.listCopied.append(destination)
    packDir = binDir / kPackFolderName
    if packDir.is_dir():
        (stagingDir / kPackFolderName).mkdir()
        for pack in selectStagedPacks(projectRoot, packDir, request.game):
            destination = stagingDir / kPackFolderName / pack.name
            shutil.copy2(pack, destination)
            request.listCopied.append(destination)
    else:
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
    return projectRoot / "build" / kMapTargetPreset[request.target] / "Bin"


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
        return ExitCode.Violation
    print(f"[package] done {stagingDir} {measureBytes(stagingDir)}", flush=True)
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
