#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Engine 레이어 금지 include 검사.

강제 규칙:
  1) Source/Engine/** 에서 Editor / GameFramework / Games 경로 include 금지.
  2) Source/Games/**, Source/GameFramework/** 에서 Engine/Common/EngineServices.h 금지
     (게임 쪽은 GameFramework/Base/Foundation/Framework/GameService.h 의 game:: 만 사용).
  3) Engine 내부 티어: 아래 티어가 위 티어를 include 하지 못한다 (_kEngineTier). 레이어는 Engine 최상위 폴더 하나다.
  4) Source/RuntimeAPI/** 에서 Engine / App / ModuleHost / Games 경로 include 금지 — 호스트 ↔ 모듈 계약이 구현을 알면 안 된다.
     (Export/ 의 모듈 매크로가 Editor · GameFramework 로케이터를 끌어오는 것은 계약이라 막지 않는다.)
  5) 최상위 소스 폴더 사이의 방향(include 경로의 **앞부분**으로 본다): Core 는 아무것도 모르고, App 은 Engine · RuntimeAPI · ModuleHost 만(게임 · 에디터는
     C-ABI 로만), Server 는 Engine · RuntimeAPI · ModuleHost 만, ModuleHost 는 Engine · RuntimeAPI 만 알고, 에디터는 게임 · 키트 · 호스트를 모르며,
     Engine · GameFramework · 게임은 호스트(App · ModuleHost · Server)를 모른다.
  6) 게임은 다른 게임을 include 하지 않는다 — 나눠 쓸 것은 키트 · 기반으로 내린다.

티어는 **include 그래프에서 계산한 것**이다 — 손으로 고른 금지 쌍은 늘릴 기준이 없다. 전체 그래프를
Tarjan SCC 로 줄이고 위상 순서를 티어로 쓰며, 위반은 경고가 아니라 실패다.

  python Scripts/lint/gate/CheckEngineLayers.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import functools
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import (  # noqa: E402
    collectSourceFiles,
    kDirSourceApp,
    kDirSourceModuleHost,
    kDirSourceCore,
    kDirSourceEditor,
    kDirSourceEngine,
    kDirSourceGameFramework,
    kDirSourceGames,
    kDirSourceRuntimeAPI,
    kDirSourceServer,
    kFileEngineServices,
    mapConcurrent,
    normalizePath,
    startsWithPathComponent,
)
from LintGate import GateError, GateResult, LintGate  # noqa: E402
_kIncludeRe = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


@functools.lru_cache(maxsize=None)
def splitNormalizedPathInternal(pathText: str) -> tuple[str, ...]:
    """`normalizePath` 한 경로를 `/` 로 나눈 조각. include 마다 금지 패턴 수만큼 불리므로 같은 글자는 한 번만 푼다."""
    return tuple(normalizePath(pathText).split("/"))


def includeHitsBanInternal(includePath: str, bannedPattern: str) -> bool:
    normalizedParts = list(splitNormalizedPathInternal(includePath))
    bannedParts = [part for part in splitNormalizedPathInternal(bannedPattern) if part]
    if not bannedParts:
        return False
    for partIndex in range(len(normalizedParts) - len(bannedParts) + 1):
        if normalizedParts[partIndex : partIndex + len(bannedParts)] == bannedParts:
            return True
    return False


_kForbiddenRules: list[tuple[str, tuple[str, ...]]] = [
    (
        kDirSourceEngine,
        (
            "Editor/",
            "Source/Editor/",
            "GameFramework/",
            "Games/",
            kDirSourceGames,
        ),
    ),
    (
        kDirSourceGames,
        (kFileEngineServices, "EngineServices.h"),
    ),
    (
        kDirSourceGameFramework,
        (kFileEngineServices, "EngineServices.h"),
    ),
    (
        # 계약이 구현을 알면 안 된다 — 서비스 표도 RuntimeAPI 에 있다. Export/ 의 모듈 매크로는 모듈 쪽 로케이터(Editor · GameFramework)를 끌어오는 것이
        # 계약이라(그 본문은 모듈 .cpp 에서 펼쳐진다) 막지 않는다.
        kDirSourceRuntimeAPI,
        ("Engine/", kDirSourceEngine, "App/", kDirSourceApp, "ModuleHost/", kDirSourceModuleHost, "Games/", kDirSourceGames),
    ),
]

# 최상위 소스 폴더 사이의 방향 — include 경로의 **앞부분**으로 본다(`GameFramework/Kits/Feature/Online/Chat/Server/` 같은 하위 폴더 이름과 헷갈리지 않게).
#   Core 는 아무것도 모른다(생성 설정 `sw/` 만). App 은 Engine · RuntimeAPI · ModuleHost 만 — 게임 · 에디터는 C-ABI 로만 안다(CLAUDE.md "Target graph").
#   Server 도 Engine · RuntimeAPI · ModuleHost 만이고, 두 실행 파일이 같이 쓰는 ModuleHost 는 Engine · RuntimeAPI 만 안다(실행 파일을 모른다).
#   에디터는 게임 · 키트 · 호스트를 모른다(RuntimeAPI · 위임 · 이벤트로). Engine · GameFramework · 게임은 호스트(App · ModuleHost · Server)를 모른다.
_kForbiddenPrefixRules: tuple[tuple[str, tuple[str, ...]], ...] = (
    (kDirSourceCore, ("Engine/", "Editor/", "GameFramework/", "Games/", "App/", "ModuleHost/", "Server/", "RuntimeAPI/")),
    (kDirSourceApp, ("Editor/", "GameFramework/", "Games/", "Server/")),
    (kDirSourceModuleHost, ("Editor/", "GameFramework/", "Games/", "App/", "Server/")),
    (kDirSourceEditor, ("GameFramework/", "Games/", "App/", "ModuleHost/", "Server/")),
    (kDirSourceServer, ("Editor/", "GameFramework/", "Games/", "App/")),
    (kDirSourceEngine, ("App/", "ModuleHost/", "Server/")),
    (kDirSourceGameFramework, ("App/", "ModuleHost/", "Server/")),
    (kDirSourceGames, ("App/", "ModuleHost/", "Server/", "Editor/")),
)

# Engine 최상위 폴더를 담지 않는 파일(EngineLoop.cpp 등)의 가상 티어 이름.
_kRootLayerName = "<root>"

# ------------------------------------------------------------------------------
# Engine 내부 티어 — 숫자가 큰 쪽이 위다. 같은 티어끼리는 서로 참조해도 된다.
#
# 이 표는 include 그래프를 위상 정렬해서 얻었다(`Scripts/lint/report/RunEngineLayerGraph.py` 가 같은
# 규칙으로 다시 계산한다). 손으로 고른 순서가 아니므로, 코드가 바뀌면 표도 다시 계산해야 한다.
#
# **강결합 묶음은 없다.** 그래프가 DAG 라 모든 폴더에 참인 순서가 있다. 묶음이 다시 생기면 대개 "위층 것을
# 아래층이 드는" 모양이다 — Object 가 SceneManager 에게 활성 씬을 묻거나, RHI 디바이스가 렌더 패스 에셋 캐시를
# 소유하거나, RHI 가 IWindow 전역을 읽는 식. 처방은 Source/Engine/README.md "상용 엔진과의 대조".
#
# `Renderer`(FrameRenderer · RenderGraph · GPUScene · RenderThread · Cook)는 씬과 컴포넌트를 **읽어서 그리는 쪽**이라
# 그 위이고, `Graphics`(RHI · Shader · Material · Mesh · Texture · Upload)는 컴포넌트가 드는 **디바이스와 GPU 에셋**이라
# 그 아래다 — 언리얼의 RHI/RenderCore 와 Renderer 사이의 선이다. 그래서 RHI · Shader 가 Renderer 를 include 하면 실패한다.
# ------------------------------------------------------------------------------
_kEngineTier: dict[str, int] = {
    # 토대 — Engine 의 어느 것도 참조하지 않는다.
    "Common": 0,
    # 외부 압축 라이브러리(lz4·zstd) 코덱. Core 의 ICompressionCodec 만 구현하고 Engine 것은 안 본다
    # — Core 를 압축 라이브러리에 종속시키지 않으려고 여기 둔다(Source/Engine/CMakeLists.txt 주석 참고).
    "Compression": 0,
    # 서버 운영 관측(지표 등록부 · 상태 확인 · 운영 HTTP 끝점). Core(로그 · 스트림 전송)만 보고 Engine 의 다른 폴더는 안 본다 — 전용 서버 실행 파일이
    # GameFramework DLL 없이 들고, 기반 Online 의 서비스 지표 묶음이 그 위에 선다.
    "Observability": 0,
    # 리플렉션 — 토대 위의 타입 레지스트리.
    "Reflection": 1,
    # 개발 명령 레지스트리(`SW_DEV_COMMAND`)와 콘솔 한 줄 해석기 — 언리얼 `IConsoleManager` 처럼 어디서나 명령을 등록하므로 바닥 가까이 둔다.
    "Console": 1,
    # 토대 위의 잎 헬퍼(편집 명령 스택 · 디버그 값 · 게임 시간 배율 · 자동 플레이 계약 · 키-값 파일).
    "Utility": 2,
    # 직렬화 — 리플렉션 위에 올라간다.
    "Serialization": 2,
    # 엔진 프로파일러(FrameProfiler · Tracy 출력 · 메모리 예산). 렌더러 · 오브젝트 · 리소스가 구간을 남기므로 바닥 가까이 둔다.
    "Profiling": 3,
    # 2D 타일맵 데이터(타일셋 에셋 · 맵 문서 XML · 격자 도우미). 배치(Environment/Placement)는 이것을 표면으로 감싸 쓴다.
    "TileMap": 3,
    # 설정 — 리플렉션 · 직렬화로 읽힌다.
    "Config": 3,
    # 물리 — 설정 표 · 물리 에셋 · 셰이프 서술자가 리플렉션 데이터다.
    "Physics": 3,
    # 에셋 데이터베이스 · 팩 · 캐시 등록부(IAssetCache). 기능 캐시는 각 기능 폴더가 이것을 구현한다.
    "Resource": 4,
    # 공간 분할 — 물리의 AABB 위에 선다.
    "Spatial": 4,
    # 내비메시(인터페이스 · 베이크 입력 · 설정 표 · Recast 백엔드). 물리의 셰이프 서술자 · AABB 를 읽어 베이크하고, 씬의 내비게이션(Object)과
    # 컴포넌트가 쓴다 — 공간 분할과 같은 자리.
    "Navigation": 4,
    # 애니메이션 데이터와 그 에셋 캐시(AnimationAssetCache · SpriteClipCache 가 Resource 의 IAssetCache 를 구현한다).
    "Animation": 5,
    # 문자열 테이블 · 문화권과 로컬라이제이션 파일 핫 리로드 캐시(LocalizationReloadCache).
    "Localization": 5,
    # 오디오 — 믹서 그래프 · 이벤트 · 음악 데이터를 리플렉션 · 직렬화로 읽고, 립싱크 가져오기가 애니메이션 표정 트랙을 쓴다.
    "Audio": 6,
    # 대화 그래프 — 로컬라이즈된 글을 든다.
    "Dialogue": 6,
    # 글자 — 글꼴 파일(Resource)을 읽어 글리프 · SDF 아틀라스(CPU 바이트) · 줄 바꿈을 만든다. GPU 를 모른다 — 아틀라스 업로드는 렌더러의 캔버스가 한다.
    "Text": 6,
    # 디바이스와 GPU 에셋(RHI · Shader · Material · Mesh · Texture · Upload).
    "Graphics": 7,
    # 창 — RHI 가 정한 IRenderSurface 를 구현하므로 Graphics 위다(언리얼 Slate 가 RHI 위인 것과 같다).
    "Window": 8,
    # 컴포넌트 모델. 컴포넌트가 머티리얼 · 메시(Graphics)를 든다.
    "Object": 8,
    # 입력 장치와 액션 맵 — 창의 메시지를 읽는다.
    "Input": 9,
    # 월드 — 월드는 액터를 알고 액터는 월드를 모른다.
    "Scene": 9,
    # 오브젝트 위에서 도는 기능 모듈.
    "Sequencer": 9,
    # 캐릭터 외형 형상(소켓 · 피팅 · 병합 · 절단 · 체형)과 소켓 부착 컴포넌트. 컴포넌트 모델 위의 기능 모듈이라 Sequencer 와 같은 자리다.
    "Character": 9,
    # 지형 · 식생 · 물 — 컴포넌트가 메시 · 머티리얼로 그리는 월드 기능. 씬을 모르고 오브젝트 매니저만 본다.
    "Environment": 9,
    # 플레이어 옵션 — 입력 · 오디오 · 언어 · 창 방식 값을 그 서브시스템에 넣는다(위층은 렌더러를 모른다 — 화면 변경은 호스트가 한다).
    "UserSettings": 10,
    # 핫 리로드 — 씬과 컴포넌트를 읽는다.
    "Module": 10,
    # 파괴(파쇄 · 연결 그래프 · 피해 · 조각 컴포넌트). 캐릭터 형상의 자르기 도구와 컴포넌트 모델 위에 선다 — 렌더러는 모른다.
    "Destruction": 10,
    # 텔레메트리 — 동의를 사용자 설정에서 읽는다. 엔진의 다른 곳은 이것을 모른다(EngineLoop 가 프레임 시간을 넘긴다).
    "Telemetry": 11,
    # 그리는 쪽 — 씬과 컴포넌트를 읽는다.
    "Renderer": 11,
    # 런타임(게임) UI — 위젯 트리 · 레이아웃 · 사건 · 포커스 · 스타일 · 문서 · 바인딩. 입력 · 글자 · 캔버스 그리기 목록(Graphics) · 사용자 설정을 쓴다.
    # 렌더러와는 서로 include 하지 않는다 — 사이의 값은 Graphics/Canvas 의 그리기 목록뿐이다(언리얼 Slate ↔ SlateRHIRenderer 의 선).
    "UI": 11,
    # 자동화 시나리오(실행기 · 탐침 · 단계 등록표). 입력 · 씬 · 창을 내려다보고, 스크린샷 · 종료는 EngineLoop 가 넘긴 창구로 한다.
    "Automation": 11,
    # 전부를 엮는 자리.
    _kRootLayerName: 12,
}

#: 티어 예외 — `prelude:<include 경로>` 는 어느 티어에서 include 해도 되는 헤더, `wiring:<파일>` 은 모든 서브시스템을 알아야 해
#: 위 티어를 include 해도 되는 배선 파일이다(`CheckEngineLayersGate.mapExemption`).
_kPreludePrefix = "prelude:"
_kWiringPrefix = "wiring:"


def engineTierOfInternal(folderName: str) -> int | None:
    """Engine 최상위 폴더 이름의 티어. 표에 없으면 None (새 폴더 = 검사 실패)."""
    return _kEngineTier.get(folderName)


def engineLayerOfInternal(engineRelativePath: str) -> str:
    """Engine/ 아래 상대 경로 → 레이어 이름(최상위 폴더, 루트 파일은 `_kRootLayerName`)."""
    parts = engineRelativePath.split("/")
    if len(parts) == 1:
        return _kRootLayerName
    return parts[0]


def processFile(filePath: Path, repositoryRoot: Path) -> list[str]:
    """파일 하나의 금지 include 와 티어 위반을 돌려줍니다. 읽지 못하면 `GateError` 입니다."""
    relativeFilePath = filePath.relative_to(repositoryRoot).as_posix()
    try:
        text = filePath.read_text(encoding="utf-8", errors="strict")
    except UnicodeDecodeError as exception:
        raise GateError(f"UTF-8 인코딩 오류: {relativeFilePath}: {exception}") from exception
    except OSError as exception:
        raise GateError(f"읽기 실패: {relativeFilePath}: {exception}") from exception

    fileViolations: list[str] = []

    for rulePrefix, bannedList in _kForbiddenRules:
        if not startsWithPathComponent(relativeFilePath, rulePrefix):
            continue
        for includePath in _kIncludeRe.findall(text):
            normalizedInclude = normalizePath(includePath)
            for bannedPattern in bannedList:
                if includeHitsBanInternal(normalizedInclude, bannedPattern):
                    fileViolations.append(f'{relativeFilePath}: #include "{includePath}"  (금지: {bannedPattern})')

    listInclude = [normalizePath(includePath) for includePath in _kIncludeRe.findall(text)]
    for rulePrefix, listBannedPrefix in _kForbiddenPrefixRules:
        if not startsWithPathComponent(relativeFilePath, rulePrefix):
            continue
        for include in listInclude:
            if include.startswith(listBannedPrefix):
                fileViolations.append(f'{relativeFilePath}: #include "{include}"  (금지: {rulePrefix} 는 {include.split("/")[0]}/ 를 모른다)')
    # 게임은 다른 게임을 모른다 — 나눠 쓸 것은 키트 · 기반으로 내린다.
    if startsWithPathComponent(relativeFilePath, kDirSourceGames):
        listPart = relativeFilePath.split("/")
        gameName = listPart[2] if len(listPart) >= 4 else ""
        for include in listInclude:
            parts = include.split("/")
            if len(parts) > 2 and parts[0] == "Games" and gameName and parts[1] != gameName:
                fileViolations.append(f'{relativeFilePath}: #include "{include}"  (게임 {gameName} -> 다른 게임 {parts[1]})')

    if startsWithPathComponent(relativeFilePath, kDirSourceEngine) is False:
        return fileViolations
    gate = CheckEngineLayersGate
    wiringKey = _kWiringPrefix + relativeFilePath
    bWiring = wiringKey in gate.mapExemption
    if bWiring:
        gate.seeExemption(wiringKey)
    tierViolations: list[str] = []

    enginePrefixLen = len(kDirSourceEngine) + 1
    engineRelativePath = relativeFilePath[enginePrefixLen:]
    sourceLayer = engineLayerOfInternal(engineRelativePath)
    sourceTier = engineTierOfInternal(sourceLayer)
    if sourceTier is None:
        fileViolations.append(f"{relativeFilePath}: Engine 최상위 폴더 '{sourceLayer}' 가 티어 표(_kEngineTier)에 없습니다.")
        return fileViolations

    for includePath in _kIncludeRe.findall(text):
        normalizedInclude = normalizePath(includePath)
        if normalizedInclude.startswith("Engine/") is False:
            continue
        if _kPreludePrefix + normalizedInclude in gate.mapExemption:
            gate.seeExemption(_kPreludePrefix + normalizedInclude)
        destRelative = normalizedInclude[len("Engine/") :]
        destLayer = engineLayerOfInternal(destRelative)
        if destLayer == sourceLayer:
            continue
        destTier = engineTierOfInternal(destLayer)
        if destTier is None:
            fileViolations.append(f'{relativeFilePath}: #include "{includePath}"  (티어 표에 없는 폴더 \'{destLayer}\')')
            continue
        if destTier > sourceTier:
            preludeKey = _kPreludePrefix + normalizedInclude
            if preludeKey in gate.mapExemption:
                gate.useExemption(preludeKey)
                continue
            tierViolations.append(
                f'{relativeFilePath}: #include "{includePath}"  (티어 {sourceLayer}(T{sourceTier}) -> {destLayer}(T{destTier}))'
            )

    if bWiring and tierViolations:
        gate.useExemption(wiringKey)
        return fileViolations
    return fileViolations + tierViolations


class CheckEngineLayersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    #: 티어 예외 → 이유. prelude 는 위 티어 헤더를 include 할 때, 배선은 그 파일이 위 티어를 include 할 때 쓰인다.
    mapExemption = {
        "prelude:Engine/EngineMinimal.h": "타입 별칭과 전방 선언만 모은 우산 헤더 — 어느 티어에서 include 해도 된다",
        "prelude:Engine/Resource/ResourceUtil.h": "리소스 경로 해석 static 헬퍼(Core 만 include) — loadFromResource 진입점이 쓴다",
        "wiring:Source/Engine/Reflection/ReflectGenerated.h": ".gen.cpp 전용 preamble — 생성 코드가 쓰는 모든 형식을 모은다",
        "wiring:Source/Engine/Resource/AssetManager.cpp": "리소스 파사드 구현 — 캐릭터 · 파괴 · 그래픽스 캐시를 소유하는 조립점(UE 의 에셋 매니저 자리)",
    }

    description = "Engine 레이어 금지 include 검사"
    buildComment = "Checking Engine layer include rules..."
    timeoutSeconds = 15
    preCommitPattern = ("*.cpp", "*.cc", "*.cxx", "*.c", "*.h", "*.hpp", "*.inl")
    violationHeader = "레이어 위반"
    selfTestCases = [
        {
            "name": "Engine 이 Editor 를 include",
            "files": {
                "Source/Engine/Scene/Probe.cpp": '#include "pch.h"\n\n#include "Editor/Common/Workspace/EditorContext.h"\n',
            },
        },
        {
            # 액터 층이 월드 관리자를 아는 방향 — 가장 되돌아오기 쉬운 엣지다.
            "name": "Object 가 Scene 을 include (아래층이 위층을)",
            "files": {
                "Source/Engine/Object/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Scene/SceneManager.h"\n',
            },
        },
        {
            # 디바이스 · 셰이더가 그리는 쪽을 알면 안 된다.
            "name": "Graphics(RHI)가 Renderer 를 include",
            "files": {
                "Source/Engine/Graphics/RHI/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Renderer/Frame/FrameRenderer.h"\n',
            },
        },
        {
            # 계약이 구현을 아는 방향 — 서비스 표를 Engine 에 두면 이 모양이 된다.
            "name": "RuntimeAPI 가 Engine 을 include",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/RuntimeAPI/Service/Probe.h": '#pragma once\n\n#include "Engine/Common/EngineServices.h"\n',
            },
        },
        {
            "name": "Core 가 Engine 을 include",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/Core/Probe/Probe.cpp": '#include "pch.h"\n\n#include "Engine/Scene/SceneManager.h"\n',
            },
        },
        {
            # App 은 게임 · 에디터를 C-ABI 로만 안다.
            "name": "App 이 게임을 include",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/App/Probe.cpp": '#include "pch.h"\n\n#include "Games/Alpha/AlphaGame.h"\n',
            },
        },
        {
            "name": "게임이 다른 게임을 include",
            "files": {
                "Source/Engine/Common/Probe.h": "#pragma once\n",
                "Source/Games/Alpha/AlphaProbe.cpp": '#include "pch.h"\n\n#include "Games/Beta/BetaProbe.h"\n',
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        engineDir = repositoryRoot / kDirSourceEngine
        if not engineDir.is_dir():
            raise GateError(f"Engine 경로 없음: {engineDir}")

        scanRoots = [
            engineDir,
            repositoryRoot / kDirSourceGames,
            repositoryRoot / kDirSourceGameFramework,
            repositoryRoot / kDirSourceRuntimeAPI,
            repositoryRoot / kDirSourceCore,
            repositoryRoot / kDirSourceApp,
            repositoryRoot / kDirSourceModuleHost,
            repositoryRoot / kDirSourceEditor,
            repositoryRoot / kDirSourceServer,
        ]
        scanRoots = [root for root in scanRoots if root.is_dir()]
        allFiles = collectSourceFiles(scanRoots)

        violations: list[str] = []
        for fileViolations in mapConcurrent(lambda path: processFile(path, repositoryRoot), allFiles):
            violations.extend(fileViolations)
        return GateResult(listViolation=violations, summary=f"{len(allFiles)} files scanned in parallel")


main = CheckEngineLayersGate.run


if __name__ == "__main__":
    sys.exit(main())
