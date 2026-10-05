#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
여러 장르 키트를 한 게임에 섞을 때 소리 없이 부딪히는 이름 공간을 막는다.

  1) 상태 형식 표(`k...Tag = 0x........u; ///< 'ABCD'`)는 저장소(Source · Test)에서 하나뿐이고, 주석의 네 글자가 값의 바이트(작은 끝 —
     `Archive` 가 uint32 를 쓰는 순서)와 같다. 한 상태에 키트 여럿의 구간이 실리면 표가 곧 구간 이름이다 — 같은 표 둘이면 한쪽 구간을 다른 쪽이 읽는다.
  2) 키트(`Source/GameFramework/Kits/`)는 키를 직접 읽지 않고(`isKeyDown` · `Key::W`) 입력 맵 액션 이름을 글자로 박지 않는다 — 액션 이름은
     키트 설정 칸으로 받는다(`PlayerControllerSettings::_moveAction`). 입력 맵은 게임에 하나라 키트가 이름을 정하면 다른 키트와 부딪힌다.
  3) 키트가 읽는 게임 설정의 사용자 칸(`GameSettings::getCustomProperty*`)은 `<키트>.` 로 시작한다(`Farming.startingGold`).

  python Scripts/lint/gate/CheckKitNamespaces.py [--root <repo>]
"""
from __future__ import annotations

import argparse
import sys
import re
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankComments  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kTagRe = re.compile(r"\b(k[A-Z]\w*Tag)\s*=\s*0x([0-9A-Fa-f]{8})u?\s*;(?:[ \t]*///<[ \t]*'([^'\n]{4})')?")
_kRawKeyRe = re.compile(r"\b(?:isKeyDown|wasKeyPressed|wasKeyReleased)\s*\(|\bKey::[A-Z]\w*")
_kActionLiteralRe = re.compile(r"\b(?:wasActionTriggered|isActionDown|wasActionPressed|wasActionReleased|isActionToggled|getActionHoldDuration|"
                               r"getVector2D|getAxis1D|isChordDown|wasChordTriggered)\s*\(\s*(?:hashed_string\s*\(\s*)?\"")
_kCustomPropertyRe = re.compile(r"\bgetCustomProperty\w*\s*\(\s*\"([^\"]*)\"")
_kKitPrefix = "Source/GameFramework/Kits/"


def decodeTagInternal(hexText: str) -> str:
    """값의 바이트를 작은 끝 순서로 읽은 네 글자입니다(`Archive` 가 uint32 를 쓰는 순서)."""
    value = int(hexText, 16)
    return "".join(chr((value >> (8 * index)) & 0xFF) for index in range(4))


def findKitNameInternal(relativePath: str) -> str:
    """`Source/GameFramework/Kits/<묶음>/<키트>/...` 의 키트 이름입니다. 키트 밖이거나 묶음 공용 파일이면 빈 글자입니다."""
    if not relativePath.startswith(_kKitPrefix):
        return ""
    listPart = relativePath[len(_kKitPrefix):].split("/")
    return listPart[1] if len(listPart) >= 3 else ""


def lineOfInternal(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


class CheckKitNamespacesGate(LintGate):
    """`selfTestCases` 는 이 린트가 반드시 잡아야 하는 조각이다."""

    description = "키트를 섞을 때 부딪히는 이름 공간 검사(상태 표 · 키트 입력 · 키트 설정 칸)"
    buildComment = "Checking kit namespaces (state tags, kit input, kit settings keys)..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/*.h", "Source/*.cpp", "Test/*.h", "Test/*.cpp")
    preCommitFileArgument = ""
    violationHeader = "키트 이름 공간 위반"
    hint = ("상태 표는 새 네 글자로(주석은 값의 작은 끝 바이트), 키트 입력은 설정 칸으로, 키트 설정 칸은 `<키트>.` 접두로 — "
            "Source/GameFramework/README.md \"키트 여럿을 한 게임에\"")
    selfTestCases = [
        {
            "name": "두 파일이 같은 상태 표를 쓴다",
            "files": {
                "Source/Games/GameA/ADirector.cpp": "static constexpr uint32 kStateTag = 0x4D524146u; ///< 'FARM'\n",
                "Source/Games/GameB/BDirector.cpp": "static constexpr uint32 kStateTag = 0x4D524146u; ///< 'FARM'\n",
            },
        },
        {
            "name": "표 주석이 값의 바이트와 다르다",
            "files": {
                "Source/Games/GameA/ADirector.cpp": "static constexpr uint32 kStateTag = 0x4D524146u; ///< 'MRAF'\n",
            },
        },
        {
            "name": "키트가 키를 직접 읽는다",
            "files": {
                "Source/GameFramework/Kits/Simulation/Probe/ProbeSimulation.cpp": "void f( const InputManager& input ) { if ( input.isKeyDown( Key::W ) ) {} }\n",
            },
        },
        {
            "name": "키트가 입력 맵 액션 이름을 박는다",
            "files": {
                "Source/GameFramework/Kits/Action/Probe/ProbeController.cpp": "bool f( const InputMap& map ) { return map.wasActionTriggered( hashed_string( \"Jump\" ) ); }\n",
            },
        },
        {
            "name": "키트 설정 칸에 키트 접두가 없다",
            "files": {
                "Source/GameFramework/Kits/Rpg/Probe/ProbeSave.cpp": "int32 f( const GameSettings& settings ) { return settings.getCustomPropertyInt( \"maxPartySize\", 6 ); }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        pass

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listFile = self.selectTargetFiles(repositoryRoot, None, listScanRoot=("Source", "Test"), suffixes=(".h", ".cpp"))
        mapTagToSite: dict[str, list[str]] = defaultdict(list)
        listViolation: list[str] = []
        for path in listFile:
            relativePath = path.relative_to(repositoryRoot).as_posix()
            text = path.read_text(encoding="utf-8", errors="replace")
            # 1) 상태 표 — 주석을 함께 봐야 하므로 원문에서 찾는다.
            if "Tag" in text:
                for match in _kTagRe.finditer(text):
                    site = f"{relativePath}:{lineOfInternal(text, match.start())}"
                    hexText = match.group(2).upper()
                    decoded = decodeTagInternal(hexText)
                    mapTagToSite[hexText].append(site)
                    comment = match.group(3)
                    if comment is None:
                        listViolation.append(f"{site}: {match.group(1)} = 0x{hexText} 옆에 네 글자 주석(///< '{decoded}')이 없습니다")
                    elif comment != decoded:
                        listViolation.append(f"{site}: {match.group(1)} 의 주석 '{comment}' 가 값의 바이트 '{decoded}' 와 다릅니다")
            if not relativePath.startswith(_kKitPrefix):
                continue
            # 2) · 3) 키트 — 주석 속 예시는 보지 않는다(글자 리터럴은 남긴다).
            code = blankComments(text)
            for match in _kRawKeyRe.finditer(code):
                listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 키트가 키를 직접 읽습니다({match.group(0).strip()}) — "
                                     f"입력 맵 액션을 키트 설정 칸으로 받으세요")
            for match in _kActionLiteralRe.finditer(code):
                listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 키트가 입력 맵 액션 이름을 글자로 박았습니다 — "
                                     f"키트 설정 칸(hashed_string)으로 받으세요")
            kitName = findKitNameInternal(relativePath)
            for match in _kCustomPropertyRe.finditer(code):
                key = match.group(1)
                if kitName and not key.startswith(kitName + "."):
                    listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 게임 설정 칸 '{key}' 에 키트 접두가 없습니다 — "
                                         f"'{kitName}.{key}'")
        for hexText, listSite in sorted(mapTagToSite.items()):
            if len(listSite) > 1:
                listViolation.append(f"상태 표 0x{hexText}('{decodeTagInternal(hexText)}')가 둘 이상입니다: {' · '.join(listSite)}")
        return GateResult(listViolation=listViolation, summary=f"{len(listFile)} files, {len(mapTagToSite)} state tags")


main = CheckKitNamespacesGate.run


if __name__ == "__main__":
    sys.exit(main())
