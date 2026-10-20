#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Editor 폴더 층 검사 — `Source/Editor` 의 폴더가 층이고, 아래 층은 위 층을 include 하지 못한다.

  0 Common(루트) · Common/Backend · Common/Config → 1 Common/Asset · Common/Localization · Common/SourceControl → 2 Common/Workspace
  → 3 Common/Commands → 4 Common/GUI · Common/Widgets → 5 Viewport · Popups → 6 Panels · AssetActions → 7 SelfTest → 8 루트(ImGuiEditor)

강제 규칙:
  1) 층은 `Common/<하위 폴더>`(Common 바로 아래 파일은 `Common`), 그 밖의 최상위 폴더(`Panels` · `Viewport` …), 그리고 `Source/Editor` 바로 아래 파일(`<root>`)이다.
  2) 파일은 **자기 층 이하**의 `Editor/…` 헤더만 include 한다. 같은 티어의 다른 층은 서로 include 해도 된다(`Common/GUI` ↔ `Common/Widgets`).
     `.cpp` 도 센다 — 아래 층의 구현이 위 층을 부르면 그 층을 따로 테스트하거나 떼어 낼 수 없다.
  3) 티어 표에 없는 층은 실패다 — 새 폴더는 티어를 정하고 넣는다.
  예외 표의 키는 셋이다: `prelude:<include 경로>`(어느 층에서 include 해도 되는 잎 헤더 — 위 층을 include 하지 않는다),
  `wiring:<파일>`(모든 매니저를 만들어야 해서 위 층을 include 하는 조립 파일), `violation:<파일> -> <include 경로>`(아직 고치지 않은 위반 — 줄일 대상).

티어 표(`[tier]`)와 예외 표(`[exemption]`)는 `Scripts/lint/rules/CheckEditorLayers.toml` 에 있다.

  python Scripts/lint/gate/CheckEditorLayers.py [--root <repo>] [--files a.h b.cpp]
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import iterIncludes  # noqa: E402
from common.Constants import kDirSourceEditor  # noqa: E402
from common.Paths import normalizePath  # noqa: E402
from common.RuleData import kKindInteger  # noqa: E402
from lint.LintGate import GateResult, LintGate  # noqa: E402
from lint.SourceScan import FileFindings, SourceFile, SourceScanResult, joinFindings  # noqa: E402

_kEditorIncludePrefix = "Editor/"
_kSourceEditorPrefix = kDirSourceEditor + "/"
_kCommonFolder = "Common"
#: `Source/Editor` 바로 아래 파일(셸)의 가상 층 이름.
_kRootLayerName = "<root>"

_kSourceSuffixes = (".h", ".hpp", ".inl", ".cpp")

#: `rules/CheckEditorLayers.toml` 에서 예외 표 말고 읽는 것 — 층 → 티어(숫자가 큰 쪽이 위).
_kRuleSchema = {"tier": kKindInteger}
_kEditorTier: dict[str, int] = LintGate.readRules("CheckEditorLayers", _kRuleSchema, requiredKeys=("tier",))["tier"]

_kPreludePrefix = "prelude:"
_kWiringPrefix = "wiring:"
_kViolationPrefix = "violation:"


def layerOfEditorPath(editorRelative: str) -> str:
    """`Source/Editor/` (또는 include 의 `Editor/`) 아래 상대 경로 → 층 이름."""
    parts = editorRelative.split("/")
    if len(parts) == 1:
        return _kRootLayerName
    if parts[0] == _kCommonFolder:
        return _kCommonFolder if len(parts) == 2 else f"{_kCommonFolder}/{parts[1]}"
    return parts[0]


def findViolationsInFile(relative: str, text: str) -> list[str]:
    """파일 하나(`Source/Editor/…`)의 층 위반을 돌려줍니다."""
    gate = CheckEditorLayersGate
    sourceLayer = layerOfEditorPath(relative[len(_kSourceEditorPrefix):])
    sourceTier = _kEditorTier.get(sourceLayer)
    if sourceTier is None:
        return [f"{relative}: 층 '{sourceLayer}' 가 티어 표(rules/CheckEditorLayers.toml 의 [tier])에 없습니다"]
    wiringKey = _kWiringPrefix + relative
    bWiring = wiringKey in gate.mapExemption
    if bWiring:
        gate.seeExemption(wiringKey)
    listViolation: list[str] = []
    bWiringUsed = False
    for lineNumber, rawInclude in iterIncludes(text, bQuotedOnly=True):
        if rawInclude.startswith(_kEditorIncludePrefix) is False:
            continue
        includePath = normalizePath(rawInclude)
        preludeKey = _kPreludePrefix + includePath
        if preludeKey in gate.mapExemption:
            gate.seeExemption(preludeKey)
        destLayer = layerOfEditorPath(includePath[len(_kEditorIncludePrefix):])
        if destLayer == sourceLayer:
            continue
        destTier = _kEditorTier.get(destLayer)
        if destTier is None:
            listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 티어 표에 없는 층 '{destLayer}'")
            continue
        if destTier <= sourceTier:
            continue
        if preludeKey in gate.mapExemption:
            gate.useExemption(preludeKey)
            continue
        violationKey = f"{_kViolationPrefix}{relative} -> {includePath}"
        if violationKey in gate.mapExemption:
            gate.useExemption(violationKey)
            continue
        if bWiring:
            bWiringUsed = True
            continue
        listViolation.append(f"{relative}:{lineNumber}: <{includePath}> -> 티어 {sourceTier}('{sourceLayer}')가 티어 {destTier}('{destLayer}')를 include 합니다")
    if bWiringUsed:
        gate.useExemption(wiringKey)
    return listViolation


def markExemptionTargetsSeen(relative: str) -> None:
    """`violation:` 키의 파일을 이번 훑기가 읽었으면 본 것으로 적습니다(그 include 가 사라진 줄은 낡은 예외가 된다)."""
    for key in CheckEditorLayersGate.mapExemption:
        if key.startswith(_kViolationPrefix) is False:
            continue
        fileText = key[len(_kViolationPrefix):].split(" -> ", 1)[0]
        if fileText == relative:
            CheckEditorLayersGate.seeExemption(key)


class CheckEditorLayersGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    ruleSchema = _kRuleSchema
    description = "Editor 폴더 층(Common → Workspace → Commands → GUI → Viewport · Popups → Panels → SelfTest → 셸)을 거꾸로 include 하지 않는지 검사"
    buildComment = "Checking the Editor folder layers..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/Editor/*",)
    preCommitFileArgument = "--files"
    sourceScanRoots = (kDirSourceEditor,)
    sourceSuffixes = _kSourceSuffixes
    violationHeader = "Editor 층 위반"
    hint = (
        "  아래 층이 위 층을 알아야 하면 틀(인터페이스 · 등록부)을 아래로 내리거나, 아래 층에 작은 창구 인터페이스를 두고 위 층이 구현한다(IEditorPanelHost 처럼).\n"
        "  UI 를 만지는 명령의 등록 줄(SW_EDITOR_COMMAND · SW_DEV_COMMAND)은 그 UI 의 .cpp 에 둔다. 새 폴더는 Scripts/lint/rules/CheckEditorLayers.toml 의 [tier] 에 넣는다."
    )
    selfTestCases = [
        {
            # README 의 약속 — Commands 는 ImGui 없는 동작이고 패널을 모른다.
            "name": "Commands 가 패널을 include",
            "expectedViolation": "Source/Editor/Common/Commands/Probe.cpp:3: <Editor/Panels/Scene/HierarchyPanel.h> -> 티어 3('Common/Commands')가 티어 6('Panels')를 include 합니다",
            "files": {"Source/Editor/Common/Commands/Probe.cpp": '#include "pch.h"\n\n#include "Editor/Panels/Scene/HierarchyPanel.h"\n'},
        },
        {
            "name": "Workspace 가 GUI 를 include",
            "expectedViolation": "Source/Editor/Common/Workspace/Probe.h:2: <Editor/Common/GUI/EditorMenuBar.h> -> 티어 2('Common/Workspace')가 티어 4('Common/GUI')를 include 합니다",
            "files": {"Source/Editor/Common/Workspace/Probe.h": '#pragma once\n#include "Editor/Common/GUI/EditorMenuBar.h"\n'},
        },
        {
            "name": "패널이 자체 시험을 include",
            "expectedViolation": "Source/Editor/Panels/Probe.cpp:3: <Editor/SelfTest/EditorSelfTest.h> -> 티어 6('Panels')가 티어 7('SelfTest')를 include 합니다",
            "files": {"Source/Editor/Panels/Probe.cpp": '#include "pch.h"\n\n#include "Editor/SelfTest/EditorSelfTest.h"\n'},
        },
        {
            "name": "표에 없는 층",
            "expectedViolation": "Source/Editor/Scratch/Probe.h: 층 'Scratch' 가 티어 표(rules/CheckEditorLayers.toml 의 [tier])에 없습니다",
            "files": {"Source/Editor/Scratch/Probe.h": "#pragma once\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser)

    def checkSourceFile(self, source: SourceFile, args: argparse.Namespace) -> FileFindings | None:
        markExemptionTargetsSeen(source.relative)
        return FileFindings(listViolation=findViolationsInFile(source.relative, source.text))

    def finishSourceScan(self, repositoryRoot: Path, args: argparse.Namespace, result: SourceScanResult) -> GateResult:
        listViolation, _ = joinFindings(result.listFindings)
        return GateResult(listViolation=listViolation, summary=f"Editor 파일 {result.fileCount} 개가 폴더 층을 지킨다")


main = CheckEditorLayersGate.run

if __name__ == "__main__":
    sys.exit(main())
