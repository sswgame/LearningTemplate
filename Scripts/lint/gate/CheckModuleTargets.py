#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""모듈이 들어가는 빌드 타깃(매니페스트 `_listTarget` — Client · Server)이 이름 · 의존 · include 방향을 지키는지 검사한다.

  1) 모든 모듈 매니페스트(`Source/**/<이름>.module.json`)는 `_listTarget` 을 갖고, 값은 `Client` · `Server` 중 하나 이상이다.
  2) 이름 규칙 — 서버 전용 키트는 `GF_Server_<X>`, 클라이언트 전용 키트는 `GF_Client_<X>`, 공유는 `GF_<X>`(사용자 결정 2026-10-06).
     `GF_Server_` 접두면 `_listTarget` 은 `["Server"]` 뿐이고, `["Server"]` 뿐인 키트는 `GF_Server_` 로 시작한다(Client 도 같다).
     에디터 · RHI 모듈은 종류가 곧 클라이언트 전용이라 접두를 요구하지 않는다(`["Client"]` 여야 한다).
  3) 의존 방향 — 모듈이 들어가는 타깃마다 그 의존 모듈도 들어가야 한다. 공유 모듈이 서버 전용 모듈에 의존하면 클라이언트 타깃이
     configure 에서 서거나, 의존을 맞추려고 서버 모듈을 클라이언트로 넓히게 된다.
  4) include 방향 — 파일이 들어가는 타깃(소속 모듈의 `_listTarget`)마다 그 파일이 include 한 헤더의 소속 모듈도 들어가야 한다.
     헤더 전용(인라인 · 템플릿) 서버 코드는 링크 오류 없이 클라이언트에 들어간다 — CMake 의 켜짐 검사가 못 보는 유일한 길이다.
     매니페스트 밖 폴더: `Source/Core` · `Source/Engine` · `Source/RuntimeAPI` 는 둘 다(에디터 진입점 접착제만 Client), `Source/App` 은 Client,
     App · Server 가 같이 쓰는 `Source/ModuleHost` 는 둘 다, `Source/Server` 는 Server.
     소속은 그 파일을 품은 가장 깊은 매니페스트 폴더다(`Source/Engine/Graphics/RHI/Modules/DX12/` 은 RHI_DX12).
     `Test/` · `Tools/` 는 보지 않는다 — 시험 소스는 CMake 가 꺼진 모듈의 것을 뺀다(`sw_excludeSourcesOfInactiveKits`).

  python Scripts/lint/gate/CheckModuleTargets.py [--root <repo>]
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import IncludeResolver, iterIncludes, normalizePath  # noqa: E402
from LintGate import GateError, GateResult, LintGate  # noqa: E402

_kTargetWords = ("Client", "Server")
_kBoth = frozenset(_kTargetWords)
_kManifestSuffix = ".module.json"
#: 대상이 이름 접두로 정해지는 키트 — (접두, 그 접두가 뜻하는 대상).
_kListNamePrefixTarget = (
    ("GF_Server_", frozenset({"Server"})),
    ("GF_Client_", frozenset({"Client"})),
)
#: 종류가 곧 클라이언트 전용인 모듈(창 · GPU) — 접두 없이 `["Client"]` 여야 한다.
_kClientOnlyKinds = ("Editor", "RHI")
#: 매니페스트가 없는 폴더의 대상입니다.
_kListFolderTarget = (
    ("Source/App/", frozenset({"Client"})),
    # ModuleHost(모듈 호스트 · 매니페스트 해석 · 핫 리로드) — App · Server 가 같이 링크한다(Source/ModuleHost/CMakeLists.txt).
    ("Source/ModuleHost/", _kBoth),
    ("Source/Server/", frozenset({"Server"})),
    ("Source/Core/", _kBoth),
    ("Source/Engine/", _kBoth),
    ("Source/RuntimeAPI/", _kBoth),
    # 에디터 모듈 쪽 접착제(에디터 진입점 매크로) — EditorModule 의 `.cpp` 만 include 한다(파일 머리 주석).
    ("Source/RuntimeAPI/Export/EditorModuleExports.h", frozenset({"Client"})),
)
_kSourceSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")


@dataclass(frozen=True)
class TargetOwner:
    """파일 하나가 속한 곳 — 모듈(매니페스트) 또는 매니페스트 밖 폴더."""

    label: str
    folder: str
    setTarget: frozenset[str]


def formatTargets(setTarget: frozenset[str]) -> str:
    return "·".join(word for word in _kTargetWords if word in setTarget) or "없음"


def findNamingViolationsInternal(relative: str, name: str, kind: str, setTarget: frozenset[str]) -> list[str]:
    """이름 접두 · 종류와 `_listTarget` 이 맞는지 봅니다."""
    listViolation: list[str] = []
    for prefix, setPrefixTarget in _kListNamePrefixTarget:
        if name.startswith(prefix) and setTarget != setPrefixTarget:
            listViolation.append(f"{relative}: {name} 은 `{prefix}` 로 시작하므로 `_listTarget` 이 [{formatTargets(setPrefixTarget)}] 뿐이어야 합니다"
                                 f"(지금 [{formatTargets(setTarget)}])")
    if kind in _kClientOnlyKinds:
        if setTarget != frozenset({"Client"}):
            listViolation.append(f"{relative}: {kind} 모듈 {name} 은 창 · GPU 를 쓰므로 `_listTarget` 이 [Client] 뿐이어야 합니다")
        return listViolation
    if kind != "Kit":
        return listViolation
    for prefix, setPrefixTarget in _kListNamePrefixTarget:
        if setTarget == setPrefixTarget and name.startswith(prefix) is False:
            listViolation.append(f"{relative}: [{formatTargets(setTarget)}] 뿐인 키트는 이름이 `{prefix}<X>` 입니다(지금 {name}) — "
                                 f"공유 키트는 `GF_<X>`, 서버 전용 `GF_Server_<X>`, 클라이언트 전용 `GF_Client_<X>`")
    return listViolation


def readManifests(repositoryRoot: Path) -> tuple[list[TargetOwner], dict[str, list[TargetOwner]], list[tuple[str, TargetOwner, list[str]]], list[str]]:
    """(소속 목록, 이름 → 소속, (매니페스트 경로, 소속, 의존 이름) 목록, 위반)을 돌려줍니다."""
    listOwner: list[TargetOwner] = []
    mapOwnerByName: dict[str, list[TargetOwner]] = {}
    listDependencyRow: list[tuple[str, TargetOwner, list[str]]] = []
    listViolation: list[str] = []
    sourceRoot = repositoryRoot / "Source"
    if sourceRoot.is_dir() is False:
        return listOwner, mapOwnerByName, listDependencyRow, listViolation
    for path in sorted(sourceRoot.rglob(f"*{_kManifestSuffix}")):
        relative = normalizePath(str(path.relative_to(repositoryRoot)))
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            listViolation.append(f"{relative}: JSON 을 읽지 못했습니다 ({error})")
            continue
        name = str(data.get("_name", ""))
        kind = str(data.get("_kind", ""))
        listTarget = data.get("_listTarget")
        if isinstance(listTarget, list) is False or len(listTarget) == 0:
            listViolation.append(f"{relative}: `_listTarget` 이 없습니다 — [\"Client\", \"Server\"](공유) · [\"Server\"] · [\"Client\"] 중 하나를 적습니다")
            continue
        listBad = [str(word) for word in listTarget if word not in _kTargetWords]
        if listBad:
            listViolation.append(f"{relative}: `_listTarget` 의 모르는 낱말 {listBad} — Client · Server 만 씁니다")
            continue
        setTarget = frozenset(str(word) for word in listTarget)
        listViolation.extend(findNamingViolationsInternal(relative, name, kind, setTarget))
        folder = relative[: relative.rfind("/") + 1]
        owner = TargetOwner(label=name, folder=folder, setTarget=setTarget)
        listOwner.append(owner)
        mapOwnerByName.setdefault(name, []).append(owner)
        listDependencyName = [str(entry.get("_name", "")) for entry in data.get("_listDependency", []) if isinstance(entry, dict)]
        listDependencyRow.append((relative, owner, listDependencyName))
    return listOwner, mapOwnerByName, listDependencyRow, listViolation


def findDependencyViolations(mapOwnerByName: dict[str, list[TargetOwner]], listDependencyRow: list[tuple[str, TargetOwner, list[str]]]) -> list[str]:
    listViolation: list[str] = []
    for relative, owner, listDependencyName in listDependencyRow:
        for dependencyName in listDependencyName:
            for dependency in mapOwnerByName.get(dependencyName, []):
                missing = owner.setTarget - dependency.setTarget
                if missing:
                    listViolation.append(
                        f"{relative}: {owner.label}({formatTargets(owner.setTarget)}) 이 {dependencyName}({formatTargets(dependency.setTarget)}) 에 의존합니다 — "
                        f"{formatTargets(frozenset(missing))} 타깃에는 {dependencyName} 이 없습니다"
                    )
    return listViolation


def findOwner(listOwner: list[TargetOwner], relative: str) -> TargetOwner | None:
    best: TargetOwner | None = None
    for owner in listOwner:
        if relative.startswith(owner.folder) and (best is None or len(owner.folder) > len(best.folder)):
            best = owner
    return best


def findIncludeViolations(repositoryRoot: Path, listOwner: list[TargetOwner]) -> list[str]:
    listAllOwner = listOwner + [TargetOwner(label=folder.rstrip("/"), folder=folder, setTarget=setTarget) for folder, setTarget in _kListFolderTarget]
    resolver = IncludeResolver(repositoryRoot, "Source")
    listPath = LintGate.selectTargetFiles(repositoryRoot, None, listScanRoot=("Source",), suffixes=_kSourceSuffixes)
    listViolation: list[str] = []
    for path, text in LintGate.readFiles(listPath, mustContain="#"):
        relative = path.relative_to(repositoryRoot).as_posix()
        owner = findOwner(listAllOwner, relative)
        if owner is None:
            continue
        for lineNumber, rawInclude in iterIncludes(text, bQuotedOnly=True):
            includePath = normalizePath(rawInclude)
            includedRelative = resolver.resolveInclude(relative, includePath)
            if includedRelative is None:
                continue
            includedOwner = findOwner(listAllOwner, includedRelative)
            if includedOwner is None or includedOwner is owner:
                continue
            missing = owner.setTarget - includedOwner.setTarget
            if missing:
                listViolation.append(
                    f"{relative}:{lineNumber}: \"{includePath}\" -> {owner.label}({formatTargets(owner.setTarget)}) 가 "
                    f"{includedOwner.label}({formatTargets(includedOwner.setTarget)}) 의 헤더를 include 합니다 — "
                    f"{formatTargets(frozenset(missing))} 타깃에 들어가면 안 되는 코드입니다"
                )
    return listViolation


class CheckModuleTargetsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "모듈 대상(_listTarget — Client · Server)의 이름 · 의존 · include 방향 검사(서버 전용 코드가 클라이언트에, 클라이언트 전용 코드가 서버에 들어가지 않게)"
    buildComment = "Checking that client-only and server-only modules stay on their side..."
    timeoutSeconds = 60
    preCommitPattern = ("Source/*",)
    preCommitFileArgument = ""
    violationHeader = "모듈 대상 위반"
    hint = (
        "  서버 전용 코드(DB · 캐시 드라이버, 서비스 서버)는 `_listTarget: [\"Server\"]` 모듈(GF_Server_<X>)에, 공유 코드(메시지 · 프로토콜 · 게임플레이)는\n"
        "  `[\"Client\", \"Server\"]` 모듈(GF_<X>)에, 클라이언트 전용(UI)은 `[\"Client\"]` 모듈(GF_Client_<X>)에 둡니다.\n"
        "  의존은 서버 전용 → 공유 ← 클라이언트 전용 방향만 됩니다. 공유 코드가 서버 기능을 불러야 하면 공유 모듈에 인터페이스를 두고 서버 모듈이 구현해 등록합니다."
    )
    selfTestCases = [
        {
            "name": "매니페스트에 _listTarget 이 없다",
            "files": {"Source/GameFramework/Kits/Probe/Probe/GF_Probe.module.json":
                      '{ "_name": "GF_Probe", "_version": "1.0.0", "_kind": "Kit", "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ] }\n'},
        },
        {
            "name": "GF_Server_ 접두 키트가 클라이언트에도 들어간다",
            "files": {"Source/GameFramework/Kits/Probe/Server/Probe/GF_Server_Probe.module.json":
                      '{ "_name": "GF_Server_Probe", "_version": "1.0.0", "_kind": "Kit", "_listPlatform": [ "Windows" ],'
                      ' "_listConfiguration": [ "Dev" ], "_listTarget": [ "Client", "Server" ] }\n'},
        },
        {
            "name": "서버 전용 키트의 이름에 GF_Server_ 접두가 없다",
            "files": {"Source/GameFramework/Kits/Probe/ProbeServer/GF_ProbeServer.module.json":
                      '{ "_name": "GF_ProbeServer", "_version": "1.0.0", "_kind": "Kit", "_listPlatform": [ "Windows" ],'
                      ' "_listConfiguration": [ "Dev" ], "_listTarget": [ "Server" ] }\n'},
        },
        {
            "name": "공유 키트가 서버 전용 키트에 의존한다",
            "files": {
                "Source/GameFramework/Kits/Probe/Shared/GF_Shared.module.json":
                    '{ "_name": "GF_Shared", "_version": "1.0.0", "_kind": "Kit", "_listDependency": [ { "_name": "GF_Server_Probe" } ],'
                    ' "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ], "_listTarget": [ "Client", "Server" ] }\n',
                "Source/GameFramework/Kits/Probe/Server/Probe/GF_Server_Probe.module.json":
                    '{ "_name": "GF_Server_Probe", "_version": "1.0.0", "_kind": "Kit",'
                    ' "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ], "_listTarget": [ "Server" ] }\n',
            },
        },
        {
            "name": "공유 키트 .cpp 가 서버 전용 키트의 헤더를 include 한다",
            "files": {
                "Source/GameFramework/Kits/Probe/Shared/GF_Shared.module.json":
                    '{ "_name": "GF_Shared", "_version": "1.0.0", "_kind": "Kit",'
                    ' "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ], "_listTarget": [ "Client", "Server" ] }\n',
                "Source/GameFramework/Kits/Probe/Shared/Probe.cpp":
                    '#include "GameFramework/Kits/Probe/Server/Probe/SessionStore.h"\nint probe() { return 0; }\n',
                "Source/GameFramework/Kits/Probe/Server/Probe/GF_Server_Probe.module.json":
                    '{ "_name": "GF_Server_Probe", "_version": "1.0.0", "_kind": "Kit",'
                    ' "_listPlatform": [ "Windows" ], "_listConfiguration": [ "Dev" ], "_listTarget": [ "Server" ] }\n',
                "Source/GameFramework/Kits/Probe/Server/Probe/SessionStore.h": "#pragma once\ninline int sessionCount() { return 0; }\n",
            },
        },
        {
            "name": "App(클라이언트)이 서버 실행 파일 헤더를 include 한다",
            "files": {
                "Source/App/Probe.cpp": '#include "Server/ServerConsole.h"\nint probe() { return 0; }\n',
                "Source/Server/ServerConsole.h": "#pragma once\nint serverConsoleProbe();\n",
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listOwner, mapOwnerByName, listDependencyRow, listViolation = readManifests(repositoryRoot)
        if not listOwner and not listViolation and (repositoryRoot / "Source" / "Engine").is_dir():
            raise GateError("Source/ 아래 모듈 매니페스트를 하나도 찾지 못했습니다 — 경로를 확인합니다")
        listViolation.extend(findDependencyViolations(mapOwnerByName, listDependencyRow))
        listViolation.extend(findIncludeViolations(repositoryRoot, listOwner))
        return GateResult(listViolation=listViolation, summary=f"모듈 {len(listOwner)} 개의 대상 이름 · 방향이 맞다")


main = CheckModuleTargetsGate.run

if __name__ == "__main__":
    sys.exit(main())
