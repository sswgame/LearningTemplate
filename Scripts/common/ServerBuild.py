# -*- coding: utf-8 -*-
"""
전용 서버(`SW_TARGET_TYPE=Server`) 빌드가 짓는 소스 폴더 — 정본은 CMake 와 모듈 매니페스트이고, 이 파일은 그것을 읽을 뿐 목록을 갖지 않는다.

폴더 소속은 두 곳에서 온다.
  - 모듈 매니페스트(`Source/**/<이름>.module.json`)의 `_listTarget` — `Server` 가 있으면 서버가 짓는다(에디터 · RHI 백엔드 · 에디터 확장은 `["Client"]`).
  - `Source/<X>/` · `Test/<X>/` 의 `CMakeLists.txt` 머리 가드 — `if(SW_TARGET_TYPE STREQUAL "Server") ... return()` 이면 서버가 짓지 않고
    (`STREQUAL "Client"` 면 클라이언트가 짓지 않는다), 가드가 없으면 둘 다 짓는다.
가장 깊은 소속이 이긴다(`Source/Engine/Graphics/RHI/Modules/DX12/` 는 매니페스트가 Client 뿐이라 Engine 안에서도 서버 밖이다).

소속을 알 수 없는 최상위 폴더(`Source/<X>/` 에 CMakeLists 도 매니페스트도 없음)는 `listUnclassified` 로 돌려준다 — 새 폴더가 조용히 서버 검사에서
빠지지 않게 부르는 쪽이 실패로 다룬다.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path

from common.Constants import kNotOurDirNames
from common.Paths import normalizePath
from common.Search import collectRepositoryFiles

_kManifestSuffix = ".module.json"
#: 폴더 CMakeLists 머리의 타깃 가드 — 첫 열의 `if(SW_TARGET_TYPE STREQUAL "<타깃>")` 다음에 `sw_*(...)` 선언만 거쳐 `return()` 인 모양.
_kGuardPattern = re.compile(r'^if\(\s*SW_TARGET_TYPE\s+STREQUAL\s+"(Server|Client)"\s*\)\s*(?:sw_\w+\([^)]*\)\s*)*return\(\)', re.MULTILINE)


_kTargetBoth = frozenset({"Client", "Server"})
#: 폴더 가드 · 매니페스트로 읽히지 않는 **파일 단위** 예외 — (저장소 기준 파일, 짓는 타깃, 이유). 가드는 폴더 단위라 이 줄만 손으로 둔다.
_kFileTargetException: tuple[tuple[str, frozenset[str], str], ...] = (
    ("Source/RuntimeAPI/Export/EditorModuleExports.h", frozenset({"Client"}),
     "에디터 모듈 쪽 접착제(에디터 진입점 매크로) — EditorModule 의 `.cpp` 만 include 한다(파일 머리 주석). RuntimeAPI 폴더는 둘 다 짓는다."),
)


class ServerBuildError(Exception):
    """매니페스트를 읽지 못하는 것처럼 소속을 정할 수 없는 상태입니다."""


@dataclass(frozen=True)
class ServerBuildMap:
    """
    - `listOwner`       : (저장소 기준 폴더 — 끝이 `/` — 또는 파일, 그것을 짓는 타깃 집합 ⊆ {Client, Server}).
    - `listUnclassified`: 소속을 알 수 없는 `Source/<X>/` 폴더(끝이 `/`).
    """

    listOwner: tuple[tuple[str, frozenset[str]], ...]
    listUnclassified: tuple[str, ...]

    def findTargets(self, relative: str) -> tuple[str, frozenset[str]] | None:
        """저장소 기준 경로의 가장 깊은 소속 (폴더 또는 파일, 타깃 집합). 소속이 없으면(`Tools/` 등) None."""
        best: tuple[str, frozenset[str]] | None = None
        for owner in self.listOwner:
            if relative.startswith(owner[0]) and (best is None or len(owner[0]) > len(best[0])):
                best = owner
        return best

    def isServerBuilt(self, relative: str) -> bool:
        """저장소 기준 경로가 서버가 짓는 폴더 안인가. 소속이 없으면(`Tools/` 등) False."""
        found = self.findTargets(relative)
        return found is not None and "Server" in found[1]


def readGuardedTarget(cmakeText: str) -> str | None:
    """CMakeLists 머리 가드가 짓지 않는 타깃(`Server` | `Client`)을 돌려줍니다. 가드가 없으면 None."""
    match = _kGuardPattern.search(cmakeText)
    return match.group(1) if match else None


def _readManifestOwners(repositoryRoot: Path) -> list[tuple[str, frozenset[str]]]:
    listOwner: list[tuple[str, frozenset[str]]] = []
    for path in collectRepositoryFiles(repositoryRoot, ("Source",), suffixes=(".json",)):
        if path.name.endswith(_kManifestSuffix) is False:
            continue
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        try:
            listTarget = json.loads(path.read_text(encoding="utf-8")).get("_listTarget")
        except (OSError, ValueError) as error:
            raise ServerBuildError(f"{relative}: JSON 을 읽지 못했습니다 ({error})") from error
        if isinstance(listTarget, list) is False or len(listTarget) == 0:
            raise ServerBuildError(f"{relative}: `_listTarget` 이 없습니다")
        listOwner.append((relative[: relative.rfind("/") + 1], frozenset(str(word) for word in listTarget)))
    return listOwner


def _readCMakeOwners(repositoryRoot: Path, topFolder: str) -> list[tuple[str, frozenset[str]]]:
    """`<topFolder>/<X>/CMakeLists.txt` 마다 가드로 소속을 정합니다(가드가 짓지 않는 타깃을 뺀 나머지)."""
    listOwner: list[tuple[str, frozenset[str]]] = []
    top = repositoryRoot / topFolder
    if top.is_dir() is False:
        return listOwner
    for child in sorted(top.iterdir()):
        cmakePath = child / "CMakeLists.txt"
        if child.name in kNotOurDirNames or cmakePath.is_file() is False:
            continue
        guarded = readGuardedTarget(cmakePath.read_text(encoding="utf-8", errors="replace"))
        listOwner.append((f"{topFolder}/{child.name}/", _kTargetBoth - {guarded} if guarded else _kTargetBoth))
    return listOwner


def readServerBuildMap(repositoryRoot: Path) -> ServerBuildMap:
    """저장소의 매니페스트와 CMakeLists 에서 서버 빌드 소속을 읽습니다. 매니페스트가 깨졌으면 `ServerBuildError`."""
    listManifest = _readManifestOwners(repositoryRoot)
    # 같은 폴더에 매니페스트와 CMakeLists 가 함께 있으면 매니페스트가 이긴다(Source/Editor 처럼 CMake 본문이 구성별로 갈리는 모듈).
    setManifestFolder = {folder for folder, _ in listManifest}
    listOwner = listManifest + [owner for owner in _readCMakeOwners(repositoryRoot, "Source") + _readCMakeOwners(repositoryRoot, "Test")
                                if owner[0] not in setManifestFolder]
    listOwner += [(path, setTarget) for path, setTarget, _reason in _kFileTargetException]
    listUnclassified: list[str] = []
    source = repositoryRoot / "Source"
    if source.is_dir():
        for child in sorted(source.iterdir()):
            folder = f"Source/{child.name}/"
            if child.is_dir() and child.name not in kNotOurDirNames and any(owner.startswith(folder) for owner, _ in listOwner) is False:
                listUnclassified.append(folder)
    return ServerBuildMap(listOwner=tuple(listOwner), listUnclassified=tuple(listUnclassified))
