#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ServerUIIncludes.py

전용 서버도 짓는 게임 코드(GameFramework 공유 모듈 · 공유 키트 · 게임)가 클라이언트 전용 기능(런타임 UI · 오디오 · 렌더러 · `Client/` 소스)을 직접 부르는지 본다.

**왜 린트인가.** 전용 서버는 `Engine/UI` 와 클라이언트 전용 소스(`…/Client/…`)를 짓지 않는다(`SW_UNBUILT_ENGINE_FOLDERS` · `SW_CLIENT_ONLY_SOURCE_PATTERNS`).
서버 구성의 include 게이트(`TargetLayers.toml` 의 `serverbuild`)가 `Engine/UI` 헤더를 막지만, 같은 DLL(GameFramework · 게임 모듈) 안의 `Client/` 폴더는
타깃 단위 게이트가 가르지 못하고 클라이언트 구성에서는 둘 다 컴파일된다 — 서버를 짓기 전에 커밋에서 잡으려고 둔다. 게임 코드는 UI 대신 창구
(`IInputCaptureQuery` · `IPlayerNoticeSink` · `ISubtitleSink` · `IUILayerVisibility` · `IHUDViewModelBinder` · `IDamageNumberSink` · `IGameInstanceClientHook`,
소리는 `GameSound`(`ISoundPlayer`), 어빌리티 큐 연출은 `IAbilityCueSink`, 게임의 그리는 부분은 `IGameClientPart`)를 쓴다.
오디오 · 렌더러도 서버가 짓지 않고 게이트가 막는다 — 같은 규칙으로 커밋에서 먼저 잡는다.
`#if defined( SW_WITH_CLIENT_CODE )` 구역도 쓰지 않는다 — 클라이언트에서만 도는 일은 `Client/` 소스로 옮기고 창구로 부른다.

볼 곳: `Source/GameFramework/**`(에디터 플러그인 · `Client/` 폴더 제외), `Source/Games/**`. 클라이언트 전용 소스(`…/Client/…`)의 헤더도 같은 까닭으로 막는다 — 서버는 그 소스를 짓지 않는다.
예외 표는 없다 — 클라이언트에서만 도는 줄은 `Client/` 폴더로 옮긴다.

`py -3 -m Scripts lint server-ui [--self-check]` 로 돌리고, 커밋 훅(`Rules.py` 표)이 돌린다.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kExtensions = (".h", ".cpp", ".inl")
# GPU 칸 배치(`Engine/Graphics/RHI` · `Engine/Graphics/Shader` — 정점 · 인스턴스 · 빛 종류 칸)도 게임 코드가 직접 쓰지 않는다 — 게임 쪽 형식(`MeshBuildVertex` ·
# `InstanceHighlight` · `LightKind`)을 쓰고 GPU 칸으로 옮기는 일은 엔진 · 렌더러가 한다(`refactor_layers.md` 5 · 16 번).
kUIInclude = re.compile(r'^\s*#\s*include\s+"(Engine/(?:UI|Audio|Renderer|Graphics/RHI|Graphics/Shader)/[^"]+|[^"]*/Client/[^"]+)"', re.M)
kClientGuard = re.compile(r'^\s*#\s*(?:if|ifdef|elif)\b[^\n]*SW_WITH_CLIENT_CODE', re.M)
kScanRoots = ("Source/GameFramework/", "Source/Games/")
kSkipParts = ("/Plugin/Editor/", "/Client/", "/Editor/")  # 테스트도 서버 구성(Dev)이 짓는다 — 보지 않을 까닭이 없다


def isScanned(path: str) -> bool:
    return path.startswith(kScanRoots) and not any(part in path for part in kSkipParts)


def findProblems(listSource: Iterable[tuple[str, str]]) -> list[str]:
    listProblem: list[str] = []
    for path, text in listSource:
        if not isScanned(path):
            continue
        for match in kUIInclude.finditer(text):
            listProblem.append(f"{path}: 서버도 짓는 게임 코드가 클라이언트 전용 헤더 `{match.group(1)}` 를 include 한다 — 창구(GameSound · IHUDViewModelBinder · IDamageNumberSink · ISubtitleSink · IGameClientPart …)를 쓰거나 클라이언트 전용 소스(`Client/`)로 옮긴다")
        if kClientGuard.search(text):
            listProblem.append(f"{path}: 서버도 짓는 게임 코드에 `SW_WITH_CLIENT_CODE` 구역이 있다 — 그 일을 클라이언트 전용 소스(`Client/`)로 옮기고 창구로 부른다")
    return listProblem


# 엔진 층(서버도 짓는 Engine · DevTools · ModuleManager · Server) — 서버가 짓지 않는 엔진 폴더는 `cmake/Engine/BuildLayout.cmake` 의
# `SW_UNBUILT_ENGINE_FOLDERS`(서버) 한 곳에서 읽는다(층 표 `EngineLayers.toml` 의 폴더 이름과 같은 낱말). 그 폴더 헤더는 서버 게이트(`serverbuild`)가 컴파일에서 막지만,
# 클라이언트 구성은 그 헤더를 읽을 수 있어 서버를 짓기 전에는 드러나지 않는다 — 커밋에서 잡는다. `SW_WITH_CLIENT_CODE` 구역도 같은 까닭으로 막고,
# 클라이언트 일은 창구(`IEngineUIDriver` · `IEngineRenderDriver` · `IEngineAudioDriver` · `ISoundPlayer` · `IVoiceDecoder` · `IShaderCookService` · `IDevToolsClientPart`)로 부른다.
kEngineScanRoots = ("Source/Engine/", "Source/DevTools/", "Source/ModuleManager/", "Source/Server/")
kEngineConfigPath = "Config/Dev/ClientCodeGuards.json"
kUnbuiltFolderLine = re.compile(r'list\(APPEND SW_UNBUILT_ENGINE_FOLDERS ([^)]*)\)')


def readServerUnbuiltFolders(projectRoot: Path) -> list[str]:
    text = (projectRoot / "cmake/Engine/BuildLayout.cmake").read_text(encoding="utf-8")
    listFolder: list[str] = []
    for match in kUnbuiltFolderLine.finditer(text):
        listFolder.extend(name.strip('"') for name in match.group(1).split())
    if not listFolder:
        raise RuntimeError("cmake/Engine/BuildLayout.cmake: SW_UNBUILT_ENGINE_FOLDERS 를 읽지 못했다")
    return listFolder


def findEngineProblems(listSource: Iterable[tuple[str, str]], listUnbuiltFolder: list[str], table: dict) -> list[str]:
    """서버도 짓는 엔진 층이 서버가 짓지 않는 폴더의 헤더를 include 하거나 클라이언트 구역을 두는지 본다."""
    listUnbuiltPrefix = [f"Source/Engine/{folder}/" for folder in listUnbuiltFolder]
    unbuiltInclude = re.compile(r'^\s*#\s*include\s+"((?:' + "|".join(re.escape(f"Engine/{folder}/") for folder in listUnbuiltFolder) + r')[^"]+|[^"]*/Client/[^"]+)"', re.M)
    exempt = table["_exemptPaths"]
    listProblem: list[str] = []
    for path, text in listSource:
        if not path.startswith(kEngineScanRoots) or any(path.startswith(prefix) for prefix in listUnbuiltPrefix) or "/Client/" in path:
            continue
        for match in unbuiltInclude.finditer(text):
            listProblem.append(f"{path}: 서버도 짓는 엔진 코드가 서버가 짓지 않는 헤더 `{match.group(1)}` 를 include 한다 — 창구(IEngineUIDriver · IEngineRenderDriver · IEngineAudioDriver · ISoundPlayer · IShaderCookService · IDevToolsClientPart …)로 부른다")
        bExempt = any(path.startswith(prefix) for prefix in exempt)
        if not bExempt and kClientGuard.search(text):
            listProblem.append(f"{path}: 서버도 짓는 엔진 코드에 `SW_WITH_CLIENT_CODE` 구역이 있다 — 클라이언트 일은 서버가 짓지 않는 폴더 · `Client/` 에 두고 창구로 부른다(예외는 {kEngineConfigPath})")
    return listProblem


def readSources(projectRoot: Path) -> list[tuple[str, str]]:
    listSource: list[tuple[str, str]] = []
    for root in kScanRoots + kEngineScanRoots:
        for path in (projectRoot / root).rglob("*"):
            if path.suffix in kExtensions:
                try:
                    listSource.append((path.relative_to(projectRoot).as_posix(), path.read_text(encoding="utf-8")))
                except (UnicodeDecodeError, OSError):
                    continue
    return listSource


def checkServerUIIncludes(projectRoot: Path) -> list[str]:
    import json
    listSource = readSources(projectRoot)
    table = json.loads((projectRoot / kEngineConfigPath).read_text(encoding="utf-8"))
    return findProblems(listSource) + findEngineProblems(listSource, readServerUnbuiltFolders(projectRoot), table)


def runSelfCheck() -> None:
    bad = '#include "Engine/UI/UISystem.h"\n'
    assert len(findProblems([("Source/GameFramework/Runtime/Character/A.cpp", bad)])) == 1, "공유 런타임"
    assert len(findProblems([("Source/Games/X/A.cpp", '#include "Engine/UI/Widgets/TextWidget.h"\n')])) == 1, "게임"
    assert len(findProblems([("Source/GameFramework/Runtime/X/Tests/A.cpp", '#include "GameFramework/Client/HUD/HUDControllerComponent.h"\n')])) == 1, "테스트 · 클라이언트 헤더"
    assert len(findProblems([("Source/GameFramework/Runtime/X/A.cpp", '#include "Engine/Audio/AudioEngine.h"\n')])) == 1, "오디오"
    assert len(findProblems([("Source/Games/X/A.cpp", '#include "Engine/Renderer/Extension/RenderExtensionRegistry.h"\n')])) == 1, "렌더러"
    assert len(findProblems([("Source/Games/X/A.cpp", '#if defined( SW_WITH_CLIENT_CODE )\nint a;\n#endif\n')])) == 1, "클라이언트 구역"
    clean = [("Source/GameFramework/Client/A.cpp", bad), ("Source/GameFramework/Kits/Feature/Online/Chat/Client/A.cpp", bad),
             ("Source/GameFramework/Plugin/Editor/X/A.cpp", bad), ("Source/Games/X/Client/A.cpp", '#include "Engine/Audio/AudioEngine.h"\n'),
             ("Source/GameFramework/Runtime/X/A.cpp", '#include "Engine/Presentation/UILayer.h"\n')]
    assert findProblems(clean) == [], "클라이언트 · 에디터 · 창구는 통과한다"
    table = {"_exemptPaths": {"Source/Core/Foundation/BuildInfo.h": "이유"}}
    guard = '#if defined( SW_WITH_CLIENT_CODE )\nint a;\n#endif\n'
    assert len(findEngineProblems([("Source/Engine/Scene/A.cpp", '#include "Engine/UI/UISystem.h"\n')], ["Renderer", "UI"], table)) == 1, "엔진 → UI"
    assert len(findEngineProblems([("Source/DevTools/A.cpp", '#include "DevTools/Client/DebugHUD/DebugHUD.h"\n')], ["Renderer", "UI"], table)) == 1, "개발 도구 → Client"
    assert len(findEngineProblems([("Source/Engine/Scene/A.cpp", guard)], ["Renderer", "UI"], table)) == 1, "엔진 클라이언트 구역"
    # 플랫폼 폴더도 예외가 아니다 — 서버가 짓지 않는 플랫폼 구현은 폴더째 빠지고(`Window/Linux`), 서버 대체는 `Headless` 폴더가 정의한다.
    assert len(findEngineProblems([("Source/Engine/Window/Windows/A.cpp", guard)], ["Renderer", "UI"], table)) == 1, "플랫폼 폴더 클라이언트 구역"
    cleanEngine = [("Source/Engine/UI/A.cpp", '#include "Engine/Renderer/X.h"\n'), ("Source/Engine/Window/Linux/A.cpp", guard), ("Source/DevTools/Client/A.cpp", guard)]
    assert findEngineProblems(cleanEngine, ["Renderer", "UI", "Window/Linux"], table) == [], "짓지 않는 폴더 · Client 는 통과한다"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="서버도 짓는 게임 코드가 클라이언트 전용 기능(UI · 오디오 · 렌더러 · Client/)을 직접 부르는지 본다")
    parser.add_argument("--self-check", action="store_true")
    arguments = parser.parse_args(argv)
    if arguments.self_check:
        runSelfCheck()
        print("[ServerUIIncludes] self-check OK")
        return ExitCode.Passed
    listProblem = checkServerUIIncludes(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[ServerUIIncludes] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
