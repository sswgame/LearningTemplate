"""
배포 패키지가 담는 팩 파일 고르기 — 패키지 만들기(`pack`)와 플랫폼 묶기(`android-apk` · `web-package`), 배포 확인(`shipping-check`)이 함께 쓴다.
"""

from __future__ import annotations

from pathlib import Path

from common.project_constants import DIR_RUNTIME_GAME_PRESET
from common.toolchain_config import ConfigError, require_config

#: Bin 에서 베끼는 폴더 · 파일.
PACK_FOLDER_NAME = "Packs"
#: 미룬 청크(씬 청크 · 공유 청크)의 목록 — 쿠커가 팩 도메인마다 팩 폴더에 쓴다(`PackChunkManifest::makeFileName` — `packchunks.<도메인 줄기>.xml`).
#: 런타임은 폴더의 목록을 모두 합쳐 목록에 든 팩을 기동 때 올리지 않고 씬을 열 때 올린다.
CHUNK_MANIFEST_PREFIX = "packchunks."
CHUNK_MANIFEST_SUFFIX = ".xml"


def is_chunk_manifest(path: Path) -> bool:
    """`packchunks.<줄기>.xml` 이면 참입니다(대소문자 무시)."""
    name = path.name.lower()
    return name.startswith(CHUNK_MANIFEST_PREFIX) and name.endswith(CHUNK_MANIFEST_SUFFIX) and len(name) > len(CHUNK_MANIFEST_PREFIX) + len(CHUNK_MANIFEST_SUFFIX)


#: 런타임이 늘 올리는 공용 팩. 게임 팩은 `game_<팩 폴더>.pack` 이다(`PackCooker::makePackFileName` 과 같은 이름).
LIST_SHARED_PACK_NAME: tuple[str, ...] = ("engine.pack", "common.pack")


def list_pack_name_of_game(project_root: Path, game: str) -> list[str]:
    """그 게임의 배포 패키지가 담는 팩 파일 이름들입니다 — 공용 팩과 그 게임의 팩 하나.

    팩 폴더는 게임 프리셋(`Config/Game/<게임>.json`)의 `_packRoot`(`game/<폴더>`)에서 읽는다 — 게임 이름과 폴더 이름이 다를 수 있다.
    프리셋이나 키가 없으면 ConfigError.
    """
    preset_path = project_root / DIR_RUNTIME_GAME_PRESET / f"{game}.json"
    pack_root = str(require_config(preset_path, preset_path.name).get("_packRoot", ""))
    folder_name = pack_root.removeprefix("game/")
    if not pack_root.startswith("game/") or not folder_name or "/" in folder_name:
        raise ConfigError(f"{preset_path}: `_packRoot` 는 `game/<폴더>` 입니다 (`{pack_root}`)")
    return [*LIST_SHARED_PACK_NAME, f"game_{folder_name.lower()}.pack"]


class PackageError(Exception):
    """단계 하나가 실패했다. 메시지가 `[package] FAILED` 줄의 이유다."""


def select_staged_packs(project_root: Path, pack_dir: Path, game: str) -> list[Path]:
    """패키지에 담는 팩입니다 — 공용 팩과 그 게임의 팩(나눈 형제 팩 포함)만(`list_pack_name_of_game`), 그리고 그 팩 도메인들의 청크 목록(있으면).

    Bin/Packs 는 Dev · Shipping 빌드가 모든 게임의 팩을 굽고 지난 쿠킹의 팩도 남는 폴더라 통째로 베끼지 않는다.
    이름은 게임 프리셋에서 고르고, 그 팩이 없으면 실패다(빠진 팩을 둔 패키지는 기동하지 못한다).
    """
    try:
        list_name = list_pack_name_of_game(project_root, game)
    except ConfigError as error:
        raise PackageError(str(error)) from error
    list_missing = [name for name in list_name if (pack_dir / name).is_file() is False]
    if list_missing:
        raise PackageError(f"{pack_dir} has no {', '.join(list_missing)} for game {game} - cook first")
    # 나눈 도메인은 형제 팩(`<도메인>.<번호>.pack` · `<도메인>.<청크>.<번호>.pack` — `PackCooker::makePackFileName`)을 함께 싣는다. 쿠커가 쓰지 않은 형제는 지운다.
    list_pack: list[Path] = []
    for name in list_name:
        list_pack.append(pack_dir / name)
        list_pack.extend(sorted(path for path in pack_dir.glob(f"{Path(name).stem}.*.pack") if path.is_file()))
    # 청크 목록은 팩과 같은 폴더에 함께 간다(도메인마다 하나) — 빠지면 런타임이 미룬 청크까지 기동 때 올리고, 씬이 기다릴 청크를 모른다.
    for name in list_name:
        manifest = pack_dir / f"{CHUNK_MANIFEST_PREFIX}{Path(name).stem}{CHUNK_MANIFEST_SUFFIX}"
        if manifest.is_file():
            list_pack.append(manifest)
    return list_pack
