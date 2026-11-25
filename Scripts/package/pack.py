#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file pack.py
@brief 배포 패키지 하나를 만든다 — Shipping 빌드, 리소스 팩 쿠킹, 스테이징(실행 파일 · DLL · 팩 · 서버 설정 · 서드파티 고지), 검사.

에디터의 Packaging 창과 사람이 같은 진입점을 쓴다(언리얼 Package Project, 유니티 Build Settings 의 Build). 단계마다
`[package] step k/4 <단계>` 한 줄을 찍고, 끝나면 `[package] done <폴더> <바이트>` 또는 `[package] FAILED <단계> <이유>` 와 종료 코드(0 · 1)다.
창은 이 줄을 읽어 진행과 결과를 보인다(`PackagingProgress::parseLine`).

| 타깃 | 프리셋 | 실행 파일 | 쿠킹에서 빠지는 것 |
| --- | --- | --- | --- |
| `Client` | `Ninja-Shipping` | `App.exe` | 없음 |
| `Server` | `Ninja-Shipping-Server` | `Server.exe` | `CookContract.json` 의 `target_excluded_asset_kinds`(텍스처 · 셰이더 바이너리 · 오디오) |

| 플랫폼 | 프리셋 | 묶는 도구 | 패키지 |
| --- | --- | --- | --- |
| `Desktop` | 위 표(이 호스트) | 이 스크립트 | `<산출 폴더>/<게임>-<타깃>` |
| `Web` | `Web-Shipping` | `web_package.make_web_package` | `<산출 폴더>/<게임>-Web`(브라우저 폴더) |
| `Android` | `Android-Shipping` | `android_apk.make_apk` | `<산출 폴더>/<게임>-Android`(서명한 디버그 APK) |

Web · Android 는 클라이언트뿐이고 백엔드가 프리셋에 묶여 있다 — `--target Server` · `--rhi` 는 실패다. 쿠킹은 데스크톱과 같은 길(호스트 개발 빌드)이다.

스테이징은 빌드 폴더 `Bin` 의 맨 위 DLL(쿠킹 전용 셰이더 컴파일러는 뺀다)과 그 타깃의 실행 파일, `THIRD_PARTY_NOTICES.txt`, 서버면 `ClientOnlyTypes.json`, 그리고 `Packs/` 가운데 공용 팩과 그 게임의 팩만 베낀다
(`Bin/Packs` 에는 모든 게임의 팩이 구워져 있다 — 이름은 `list_pack_name_of_game` 이 게임 프리셋의 `_packRoot` 로 정한다). 쿠킹은 빌드의 `CookAssets`
타깃이 호스트 개발 빌드의 `--cook-all` 로 한다(DevTools 의 `PackCooker`, 팩 파일 이름 규칙 `PackCooker::makePackFileName`). 이름이 어긋나면 스테이징이 빠진 팩을 알리고 멈춘다.
Dev 산출물(`Modules/` · `Saved/` · `Symbols/`)과 다른 실행 파일(테스트 · 부하 봇)은 들이지 않는다. 서버는 `Config/Server/<게임>.json` 을 함께 둔다
(서버는 Shipping 에서도 운영 설정을 디스크에서 읽는다).
모드 설치 폴더 `Mods/`(실행 파일 옆 — `ModSession::getInstallFolder`)는 늘 만든다. `--mods a,b` 로 고른 모드는 묶은 배포 형태(`mod.xml` + `content.pack`)만 담는다 —
배포 게임은 소스 모드(`content/`)를 읽지 않는다. 묶은 형태는 개발 빌드의 개발 명령 `mod.pack <id>` 가 `Saved/ModPacks/<id>/` 에 만든다(없으면 실패 — 짐작해 묶지 않는다).
서버 패키지에도 같은 모드를 담는다 — 서버와 클라이언트의 구성 해시가 같아야 핸드셰이크가 통한다.

사용법:
    py -3 Scripts/package/pack.py --target Client --game Playground
    py -3 Scripts/package/pack.py --target Server --game Playground --rhi DirectX12 --output Saved/Packages
    py -3 Scripts/package/pack.py --platform Web --game Playground
    py -3 Scripts/package/pack.py --target Client --skip-build --skip-cook --bin-dir build/Ninja-Debug/Bin   # 스테이징만(창의 시나리오)
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common.command import Command, ScriptContext  # noqa: E402
from common.paths import get_bin_dir  # noqa: E402
from common.run_process import run_cmake  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from common.host_platform import executable_name, HOST  # noqa: E402
from package.pack_files import PACK_FOLDER_NAME, PackageError, select_staged_packs  # noqa: E402

#: 단계 이름 — 창이 `[package] step k/n <이름>` 으로 읽는다. 순서가 진행 순서다.
LIST_STEP_NAME: tuple[str, ...] = ("build", "cook", "stage", "verify")
#: 타깃마다 Shipping 프리셋과 실행 파일 이름.
MAP_TARGET_PRESET: dict[str, str] = {"Client": "Ninja-Shipping", "Server": "Ninja-Shipping-Server"}
#: 플랫폼마다 타깃의 Shipping 프리셋. Desktop 은 이 호스트다.
DESKTOP_PLATFORM = "Desktop"
WEB_PLATFORM = "Web"
ANDROID_PLATFORM = "Android"
MAP_PLATFORM_PRESET: dict[str, dict[str, str]] = {
    DESKTOP_PLATFORM: MAP_TARGET_PRESET,
    WEB_PLATFORM: {"Client": "Web-Shipping"},
    ANDROID_PLATFORM: {"Client": "Android-Shipping"},
}
MAP_TARGET_EXECUTABLE: dict[str, str] = {"Client": executable_name("App"), "Server": executable_name("Server")}
#: 실행 파일 옆에 두는 공유 라이브러리 확장자.
LIST_LIBRARY_SUFFIX: tuple[str, ...] = (HOST.shared_library_suffix,)
#: 쿠킹에만 쓰는 공유 라이브러리(파일 이름의 `lib` 를 뗀 줄기) — 셰이더 컴파일러(DXC). 개발 빌드만 Bin 에 두고(`sw_deploy_runtime_dependencies`),
#: 배포 App 은 미리 쿠킹한 셰이더만 읽어 패키지에는 넣지 않는다(옛 빌드 폴더 · 개발 Bin 을 스테이징할 때를 거른다). 폴더로 가르지 않는 이유: dxcompiler 가 dxil 을 이름으로 다시 찾아 실행 파일 폴더 밖에 두면 서명하지 못한다.
LIST_COOK_ONLY_LIBRARY_STEM: tuple[str, ...] = ("dxcompiler", "dxil", "dxildll")
#: Bin 에서 베끼는 파일.
NOTICE_FILE_NAME = "THIRD_PARTY_NOTICES.txt"
#: 서버 구성 빌드가 실행 파일 옆에 쓰는 클라이언트 전용 형식 이름 표(`ClientOnlyTypeTable::kTableFile`, `Scripts/generate/client_only_types.py`) — 서버 패키지에 반드시 든다.
CLIENT_ONLY_TYPE_TABLE = "ClientOnlyTypes.json"
#: 모드 설치 폴더(실행 파일 옆)와 배포 형태의 파일 — `ModSession` · `ModManifest::kFileName` · `ModPackBuilder::kPackFileName` 과 같은 이름.
MOD_FOLDER_NAME = "Mods"
MOD_EXPORT_FOLDER = Path("Saved") / "ModPacks"
LIST_MOD_FILE_NAME: tuple[str, ...] = ("mod.xml", "content.pack")
#: 모드 id 의 형식(`ModManifest::isValidID` — 소문자 · 숫자 · `_`, 64 자까지). 경로를 id 로만 만들므로 형식부터 본다.
MOD_ID_PATTERN = re.compile(r"^[a-z0-9_]{1,64}$")


@dataclass
class PackageRequest:
    """한 번의 패키징 요청."""

    target: str
    game: str
    rhi: str
    output_root: Path
    platform: str = DESKTOP_PLATFORM
    bin_dir: Path | None = None
    skip_build: bool = False
    skip_cook: bool = False
    list_mod_id: list[str] = field(default_factory=list)
    current_step: str = LIST_STEP_NAME[0]
    list_copied: list[Path] = field(default_factory=list)


def begin_step(request: PackageRequest, step_index: int) -> None:
    """`[package] step k/n <이름>` 한 줄을 찍고 지금 단계를 적는다(창의 진행 막대가 읽고, 실패 줄이 단계 이름을 쓴다)."""
    request.current_step = LIST_STEP_NAME[step_index]
    print(f"[package] step {step_index + 1}/{len(LIST_STEP_NAME)} {request.current_step}", flush=True)


def make_staging_dir(request: PackageRequest, output_root: Path) -> Path:
    """`<산출 폴더>/<게임>-<타깃>` 입니다. 데스크톱이 아니면 타깃 대신 플랫폼 이름이다(그 플랫폼은 클라이언트뿐이다)."""
    return output_root / f"{request.game}-{request.target if request.platform == DESKTOP_PLATFORM else request.platform}"


def find_preset(request: PackageRequest) -> str:
    """요청의 Shipping 프리셋입니다. 그 플랫폼이 짓지 않는 타깃 · 백엔드를 고르면 실패다(조용히 다른 것으로 짓지 않는다)."""
    preset = MAP_PLATFORM_PRESET[request.platform].get(request.target)
    if preset is None:
        raise PackageError(f"platform {request.platform} does not build the {request.target} target")
    if request.platform != DESKTOP_PLATFORM and request.rhi:
        raise PackageError(f"platform {request.platform} has its own backend - leave --rhi empty (got '{request.rhi}')")
    return preset


def is_cook_only_library(path: Path) -> bool:
    """쿠킹에만 쓰는 공유 라이브러리(`LIST_COOK_ONLY_LIBRARY_STEM`)면 True 입니다."""
    return path.stem.lower().removeprefix("lib") in LIST_COOK_ONLY_LIBRARY_STEM


def select_staged_entries(bin_dir: Path, target: str) -> list[Path]:
    """스테이징이 베끼는 Bin 의 항목입니다(그 타깃의 실행 파일, 쿠킹 전용을 뺀 맨 위 공유 라이브러리, 서드파티 고지). 팩은 `select_staged_packs` 가 고른다."""
    list_entry: list[Path] = []
    executable = bin_dir / MAP_TARGET_EXECUTABLE[target]
    if executable.is_file():
        list_entry.append(executable)
    for path in sorted(bin_dir.iterdir()):
        if path.is_file() and path.suffix.lower() in LIST_LIBRARY_SUFFIX and is_cook_only_library(path) is False:
            list_entry.append(path)
    notice = bin_dir / NOTICE_FILE_NAME
    if notice.exists():
        list_entry.append(notice)
    client_only_table = bin_dir / CLIENT_ONLY_TYPE_TABLE
    if target == "Server" and client_only_table.is_file():
        list_entry.append(client_only_table)
    return list_entry


def select_staged_mods(project_root: Path, list_mod_id: list[str]) -> list[tuple[str, list[Path]]]:
    """패키지에 담는 모드입니다 — 모드마다 배포 형태(`mod.xml` + `content.pack`)의 파일. 형식이 틀린 id · 묶지 않은 모드는 실패다."""
    list_mod: list[tuple[str, list[Path]]] = []
    for mod_id in list_mod_id:
        if MOD_ID_PATTERN.match(mod_id) is None:
            raise PackageError(f"'{mod_id}' is not a mod id (lowercase letters, digits and _)")
        folder = project_root / MOD_EXPORT_FOLDER / mod_id
        list_file = [folder / name for name in LIST_MOD_FILE_NAME]
        list_missing = [path.name for path in list_file if path.is_file() is False]
        if list_missing:
            raise PackageError(f"mod '{mod_id}' is not packed ({', '.join(list_missing)} missing in {folder}) - run the dev command `mod.pack {mod_id}` first")
        list_mod.append((mod_id, list_file))
    return list_mod


def measure_bytes(path: Path) -> int:
    """폴더 안 파일 크기의 합입니다."""
    return sum(file.stat().st_size for file in path.rglob("*") if file.is_file())


def _make_configure_definitions(request: PackageRequest) -> list[str]:
    """그 게임 · 백엔드를 고르는 구성 값입니다."""
    list_definition = [f"-DSW_ACTIVE_GAME={request.game}"]
    if request.rhi and request.target == "Client":
        list_definition.append(f"-DSW_SHIPPING_RHI_BACKEND={request.rhi}")
    return list_definition


def run_build(request: PackageRequest, project_root: Path) -> None:
    """Shipping 프리셋을 그 게임 · 백엔드로 구성하고 빌드한다(빌드에 리소스 팩 쿠킹이 든다)."""
    preset = find_preset(request)
    if run_cmake(["--preset", preset, *_make_configure_definitions(request)], cwd=project_root, capture=False).return_code != 0:
        raise PackageError(f"cmake --preset {preset} failed")
    if run_cmake(["--build", "--preset", preset], cwd=project_root, capture=False).return_code != 0:
        raise PackageError(f"cmake --build --preset {preset} failed")


def run_cook(request: PackageRequest, project_root: Path, bin_dir: Path) -> None:
    """리소스 팩을 그 빌드의 쿠킹 타깃(`CookAssets`)으로 다시 쿠킹해 Bin/Packs 에 둔다(빌드를 건너뛰고 쿠킹만 새로 할 때).

    배포 실행 파일에는 쿠커가 없다 — 빌드와 같은 길(호스트 개발 빌드가 굽는다, cmake/Modules/Build/AssetAndToolTargets.cmake)을 지난다. 구성되지 않은 폴더면 다른 빌드로 대신하지 않고 멈춘다.
    """
    build_dir = bin_dir.parent
    if (build_dir / "CMakeCache.txt").is_file() is False:
        raise PackageError(f"{build_dir} is not a configured build folder - configure the {request.target} preset before cooking")
    if run_cmake([str(build_dir), *_make_configure_definitions(request)], cwd=project_root, capture=False).return_code != 0:
        raise PackageError(f"cmake configure of {build_dir} failed")
    if run_cmake(["--build", str(build_dir), "--target", "CookAssets"], cwd=project_root, capture=False).return_code != 0:
        raise PackageError(f"cooking ({build_dir} CookAssets) failed")


def stage_platform_package(request: PackageRequest, project_root: Path, bin_dir: Path, staging_dir: Path) -> None:
    """플랫폼 묶음 도구로 패키지 폴더를 만든다 — 웹은 브라우저 폴더, Android 는 APK 하나. 팩은 빌드가 구운 `Bin/Packs` 다."""
    # 두 도구가 이 모듈의 팩 고르기를 쓴다 — 여기서 부를 때 읽는다.
    from package.apk_packager import ApkError, make_apk
    from package.web_packager import make_web_package, WebPackageError
    build_dir = bin_dir.parent
    pack_dir = bin_dir / PACK_FOLDER_NAME
    try:
        if request.platform == WEB_PLATFORM:
            make_web_package(request.game, build_dir, pack_dir, staging_dir, project_root)
            return
        apk = make_apk(request.game, find_preset(request), build_dir, pack_dir, project_root)
    except (WebPackageError, ApkError) as error:
        raise PackageError(str(error)) from error
    if staging_dir.exists():
        shutil.rmtree(staging_dir)
    staging_dir.mkdir(parents=True)
    shutil.copy2(apk, staging_dir / apk.name)


def stage_package(request: PackageRequest, project_root: Path, bin_dir: Path, staging_dir: Path) -> None:
    """스테이징 폴더를 비우고 Bin 의 배포 항목, 그 게임의 팩, 서버 설정을 베낀다. 데스크톱이 아니면 그 플랫폼의 묶음 도구가 만든다."""
    if request.platform != DESKTOP_PLATFORM:
        stage_platform_package(request, project_root, bin_dir, staging_dir)
        return
    if staging_dir.exists():
        shutil.rmtree(staging_dir)
    staging_dir.mkdir(parents=True)
    for entry in select_staged_entries(bin_dir, request.target):
        destination = staging_dir / entry.name
        shutil.copy2(entry, destination)
        request.list_copied.append(destination)
    pack_dir = bin_dir / PACK_FOLDER_NAME
    if pack_dir.is_dir():
        (staging_dir / PACK_FOLDER_NAME).mkdir()
        for pack in select_staged_packs(project_root, pack_dir, request.game):
            destination = staging_dir / PACK_FOLDER_NAME / pack.name
            shutil.copy2(pack, destination)
            request.list_copied.append(destination)
    else:
        print(f"[package] warning: no {PACK_FOLDER_NAME}/ in {bin_dir} - the package runs only with loose Content/ next to it", flush=True)
    # 모드 설치 폴더는 늘 둔다(플레이어가 묶은 모드를 넣는 자리). 고른 모드는 배포 형태만 담는다.
    (staging_dir / MOD_FOLDER_NAME).mkdir()
    for mod_id, list_file in select_staged_mods(project_root, request.list_mod_id):
        mod_dir = staging_dir / MOD_FOLDER_NAME / mod_id
        mod_dir.mkdir()
        for path in list_file:
            shutil.copy2(path, mod_dir / path.name)
            request.list_copied.append(mod_dir / path.name)
    if request.target == "Server":
        server_config = project_root / "Config" / "Server" / f"{request.game}.json"
        if server_config.is_file():
            destination = staging_dir / "Config" / "Server" / server_config.name
            destination.parent.mkdir(parents=True)
            shutil.copy2(server_config, destination)
            request.list_copied.append(destination)


def verify_package(request: PackageRequest, staging_dir: Path) -> None:
    """실행 파일이 있고, 서버 패키지에 클라이언트 실행 파일이, 패키지에 쿠킹 전용 라이브러리가 들지 않았는지 본다. 웹은 첫 화면 파일, Android 는 APK 가 있는지 본다."""
    if request.platform != DESKTOP_PLATFORM:
        pattern = "index.html" if request.platform == WEB_PLATFORM else "*.apk"
        if not any(staging_dir.glob(pattern)):
            raise PackageError(f"{pattern} is missing from the {request.platform} package")
        return
    if (staging_dir / MAP_TARGET_EXECUTABLE[request.target]).is_file() is False:
        raise PackageError(f"{MAP_TARGET_EXECUTABLE[request.target]} is missing from the package")
    list_leaked = [path.name for path in staging_dir.rglob("*") if path.is_file() and is_cook_only_library(path)]
    if list_leaked:
        raise PackageError(f"cook-only libraries leaked into the package: {', '.join(list_leaked)}")
    list_source_mod = [path.parent.name for path in (staging_dir / MOD_FOLDER_NAME).glob("*/content") if path.is_dir()]
    if list_source_mod:
        raise PackageError(f"source mods leaked into the package (a shipping game reads only content.pack): {', '.join(list_source_mod)}")
    # 표가 없는 서버는 씬의 클라이언트 전용 컴포넌트마다 모르는 형식 오류를 낸다 — 서버 구성 빌드(Source/Server/CMakeLists.txt)가 쓴다.
    if request.target == "Server" and (staging_dir / CLIENT_ONLY_TYPE_TABLE).is_file() is False:
        raise PackageError(f"{CLIENT_ONLY_TYPE_TABLE} is missing from the server package - build the Server target type (it writes Bin/{CLIENT_ONLY_TYPE_TABLE})")
    other_target = "Client" if request.target == "Server" else "Server"
    if (staging_dir / MAP_TARGET_EXECUTABLE[other_target]).exists():
        raise PackageError(f"{MAP_TARGET_EXECUTABLE[other_target]} leaked into the {request.target} package")


def resolve_bin_dir(request: PackageRequest, project_root: Path) -> Path:
    """`--bin-dir` 이 있으면 그것, 없으면 타깃 프리셋의 빌드 폴더 `Bin` 입니다."""
    if request.bin_dir is not None:
        return request.bin_dir if request.bin_dir.is_absolute() else project_root / request.bin_dir
    return get_bin_dir(find_preset(request), project_root)


def make_package(request: PackageRequest, project_root: Path) -> Path:
    """네 단계를 돌리고 스테이징 폴더를 돌려준다. 실패하면 `PackageError` 이고, 실패한 단계는 `request.currentStep` 이다."""
    bin_dir = resolve_bin_dir(request, project_root)
    output_root = request.output_root if request.output_root.is_absolute() else project_root / request.output_root
    staging_dir = make_staging_dir(request, output_root)

    begin_step(request, 0)
    if request.skip_build is False:
        run_build(request, project_root)
    if bin_dir.is_dir() is False:
        raise PackageError(f"no build output at {bin_dir}")

    begin_step(request, 1)
    if request.skip_cook is False and request.skip_build:
        run_cook(request, project_root, bin_dir)

    begin_step(request, 2)
    stage_package(request, project_root, bin_dir, staging_dir)

    begin_step(request, 3)
    verify_package(request, staging_dir)
    return staging_dir


class PackCommand(Command):
    name = "pack"
    help = "Shipping 빌드 · 쿠킹 · 스테이징으로 배포 패키지 하나를 만든다"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--platform", choices=list(MAP_PLATFORM_PRESET), default=DESKTOP_PLATFORM, help="패키지를 돌릴 플랫폼(Desktop 은 이 호스트)")
        parser.add_argument("--target", choices=sorted(MAP_TARGET_PRESET), default="Client", help="빌드 타깃(Client · Server)")
        parser.add_argument("--game", default="Playground", help="게임 프리셋 이름(Config/Game/<이름>.json, SW_ACTIVE_GAME)")
        parser.add_argument("--rhi", default="", help="Shipping 이 정적으로 링크할 백엔드(쿠킹 표 이름 — DirectX12 · DirectX11 · Vulkan · OpenGL)")
        parser.add_argument("--output", default="Saved/Packages", help="산출 폴더(저장소 기준 상대 경로 가능). 패키지는 <산출 폴더>/<게임>-<타깃>")
        parser.add_argument("--bin-dir", default="", help="스테이징할 빌드의 Bin(기본: 타깃 프리셋의 build/<프리셋>/Bin)")
        parser.add_argument("--skip-build", action="store_true", help="구성 · 빌드를 건너뛴다(이미 빌드된 Bin 을 쓴다)")
        parser.add_argument("--skip-cook", action="store_true", help="쿠킹을 건너뛴다(Bin/Packs 를 그대로 쓴다). 빌드를 하면 빌드가 쿠킹한다")
        parser.add_argument("--mods", default="", help="담을 모드 id(쉼표로 가른다). 개발 명령 `mod.pack <id>` 로 Saved/ModPacks/<id> 에 묶어 둔 것을 Mods/<id> 로 담는다")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        request = PackageRequest(target=args.target, game=args.game, rhi=args.rhi, output_root=Path(args.output), platform=args.platform,
                                 bin_dir=Path(args.bin_dir) if args.bin_dir else None, skip_build=args.skip_build, skip_cook=args.skip_cook,
                                 list_mod_id=[part.strip() for part in args.mods.split(",") if part.strip()])
        try:
            staging_dir = make_package(request, context.root)
        except (PackageError, OSError) as error:
            print(f"[package] FAILED {request.current_step} {error}", flush=True)   # 에디터 패키징 창이 이 줄을 읽는다
            return ExitCode.VIOLATION
        print(f"[package] done {staging_dir} {measure_bytes(staging_dir)}", flush=True)
        return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(PackCommand.main())
