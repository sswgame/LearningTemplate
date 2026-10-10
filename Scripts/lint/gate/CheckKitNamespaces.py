#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
여러 장르 키트를 한 게임에 섞을 때 소리 없이 부딪히는 이름 공간을 막는다.

  1) 상태 형식 표(`k...Tag = FourCcUtil::make( "ABCD" )`)는 린트 대상(`kLintTargetRelDirs`)에서 하나뿐이다. 한 상태에 키트 여럿의 구간이 실리면 표가
     곧 구간 이름이다 — 같은 표 둘이면 한쪽 구간을 다른 쪽이 읽는다. 16 진 리터럴 표(`k...Tag = 0x…`)는 바이트 순서를 손으로 맞추는 것이라 금지
     (`FourCcUtil::make` 하나 — constants D2).
  2) 키트(`Source/GameFramework/Kits/`)는 키를 직접 읽지 않고(`isKeyDown` · `Key::W`) 입력 맵 액션 이름을 글자로 박지 않는다 — 액션 이름은
     키트 설정 칸으로 받는다(`PlayerControllerSettings::_moveAction`). 입력 맵은 게임에 하나라 키트가 이름을 정하면 다른 키트와 부딪힌다.
  3) 키트가 읽는 게임 설정의 사용자 칸(`GameSettings::getCustomProperty*`)은 `<키트>.` 로 시작한다(`Farming.startingGold`).
  4) 키트가 평판 세력 id 로 쓰는 글자 리터럴(`changeValue( "x"` · `…Faction{ "x" }` · `k…FactionId = "x"`)은 키트 이름의 소문자 낱말 접두
     `<접두>.` 로 시작한다(`restaurant.guests` · `western.honor`) — 평판을 나눠 쓰면 세력 id 가 한 이름 공간이다.
  5) 키트 클래스는 기반 공유 상태(지갑 · 가방 · 플래그 · 일지 · 시계 · 날씨 · 평판 · 땅 · 값 목록)를 값으로 들지 않는다 — 빌린다(`GameStateRefs`).
     예외는 이유와 함께 `mapExemption`(`<파일>:<멤버>`) 에(참가자마다의 가방 · 팔릴 목록 · 시뮬레이션 수치), 정의 구조체(`…Def` · `…Recipe` · `…Reward`) 안의 값 목록은 보지 않는다.
  6) 키트는 빌린 객체의 알림을 꺼내지 않는다(`_pWallet->drainEvents(` · `refs._pX->drainEvents(`) — 알림 버퍼는 소비자 하나라 게임 화면의 것이다.
  7) 로그 범주(`SW_LOG_CALLER( "x" )`)는 GameFramework · Games 안에서 파일 하나에만 — 키트 · 게임을 섞으면 같은 범주의 로그가 어디서 왔는지 갈리지 않는다.
     (엔진 · RHI 는 모듈마다 한 범주를 여러 파일이 나눠 쓰는 것이 설계라 보지 않는다.)
  8) 키트는 전역 변수를 정의하지 않는다(`SW_GLOBAL_VARIABLE…` · `SW_TEST_GLOBAL_VARIABLE…`) — 스위치는 게임 · 엔진의 것이다(섞으면 키트 둘이 같은 이름을 다툰다).
  9) 게임(`Source/Games/`)도 키를 직접 읽지 않는다 — 입력 맵 액션으로(디버그 키는 엔진 `Debug` 레이어).

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

from common import blankComments, kLintTargetRelDirs  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kTagRe = re.compile(r"\b(k[A-Z]\w*Tag)\s*=\s*(?:sw::)?FourCcUtil::make\s*\(\s*\"([^\"\n]{4})\"\s*\)")
_kHexTagRe = re.compile(r"\b(k[A-Z]\w*Tag)\s*=\s*0x[0-9A-Fa-f]{8}u?\s*;")
_kRawKeyRe = re.compile(r"\b(?:isKeyDown|wasKeyPressed|wasKeyReleased)\s*\(|\bKey::[A-Z]\w*")
_kActionLiteralRe = re.compile(r"\b(?:wasActionTriggered|isActionDown|wasActionPressed|wasActionReleased|isActionToggled|getActionHoldDuration|"
                               r"getVector2D|getAxis1D|isChordDown|wasChordTriggered)\s*\(\s*(?:hashed_string\s*\(\s*)?\"")
_kCustomPropertyRe = re.compile(r"\bgetCustomProperty\w*\s*\(\s*\"([^\"]*)\"")
_kFactionLiteralRe = re.compile(r"\bchangeValue\s*\(\s*(?:hashed_string\s*\(\s*)?\"([^\"]*)\"|\b_\w*[Ff]action\w*\s*\{\s*\"([^\"]*)\"|\bk\w*FactionId\s*=\s*\"([^\"]*)\"")
_kKitPrefix = "Source/GameFramework/Kits/"
_kGamePrefix = "Source/Games/"
_kGameFrameworkPrefix = "Source/GameFramework/"
_kHeldStateRe = re.compile(r"^[ \t]+(Wallet|Inventory|GameFlags|QuestLog|WorldClock|WeatherSystem|ReputationState|LandRegistry|ItemStackList)\s+(_\w+)", re.MULTILINE)
_kOwnerTypeRe = re.compile(r"\b(?:struct|class)\s+(?:SW_\w+\s+)?(\w+)\s*(?::[^{;]*)?\{")
_kDefinitionSuffixes = ("Def", "Recipe", "Reward")
_kBorrowedDrainRe = re.compile(r"(?:\b_p(?:Wallet|QuestLog|Reputation|Flags|Clock|Weather|Inventory|Land)\w*|\brefs\._p\w+)\s*->\s*drainEvents\s*\(")
_kLogCallerRe = re.compile(r"\bSW_LOG_CALLER\s*\(\s*\"([^\"]*)\"")
_kGlobalVariableRe = re.compile(r"\bSW_(?:TEST_)?GLOBAL_VARIABLE\w*\s*\(")


def findKitNameInternal(relativePath: str) -> str:
    """`Source/GameFramework/Kits/<성격>/<묶음>/<키트>/...` 의 키트 이름입니다. 키트 밖이거나 묶음 공용 파일이면 빈 글자입니다."""
    if not relativePath.startswith(_kKitPrefix):
        return ""
    listPart = relativePath[len(_kKitPrefix):].split("/")
    return listPart[2] if len(listPart) >= 4 else ""


def lineOfInternal(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


class CheckKitNamespacesGate(LintGate):
    """`selfTestCases` 는 이 린트가 반드시 잡아야 하는 조각이다."""

    #: `<키트 파일>:<멤버>` → 키트가 공유 상태를 값으로 들어도 되는 까닭.
    mapExemption = {
        "Source/GameFramework/Kits/Genre/Action/BattleRoyale/Rule/BrMatch.h:_inventory": "참가자마다의 가방(멀티플레이 참가자 — 공유 상태가 아니다)",
        "Source/GameFramework/Kits/Genre/Simulation/Farming/FarmShippingBin.h:_bin": "팔릴 목록(값 목록 — 가방이 아니다)",
        "Source/GameFramework/Kits/Genre/Strategy/CityBuilder/CitySimulation.h:_stock": "도시 건물의 물자(시뮬레이션 수치 — 플레이어가 드는 것이 아니다)",
    }

    description = "키트를 섞을 때 부딪히는 이름 공간 · 소유 검사(상태 표 · 입력 · 설정 칸 · 세력 · 공유 상태 소유 · 빌린 알림 · 로그 범주 · 전역 변수)"
    buildComment = "Checking kit namespaces (state tags, input, settings keys, ownership, log callers)..."
    timeoutSeconds = 30
    preCommitPattern = ("Source/*.h", "Source/*.cpp", "Test/*.h", "Test/*.cpp")
    preCommitFileArgument = ""
    violationHeader = "키트 이름 공간 위반"
    hint = ("상태 표는 새 네 글자로(FourCcUtil::make), 입력은 입력 맵 액션으로(키트는 설정 칸), 키트 설정 칸은 `<키트>.` 접두로, "
            "공유 상태는 빌린다(`GameStateRefs`) — Source/GameFramework/Kits/README.md \"키트 여럿을 한 게임에\"")
    selfTestCases = [
        {
            "name": "두 파일이 같은 상태 표를 쓴다",
            "files": {
                "Source/Games/GameA/ADirector.cpp": "static constexpr uint32 kStateTag = FourCcUtil::make( \"FARM\" );\n",
                "Source/Games/GameB/BDirector.cpp": "static constexpr uint32 kStateTag = FourCcUtil::make( \"FARM\" );\n",
            },
        },
        {
            "name": "16 진 리터럴 상태 표",
            "files": {
                "Source/Games/GameA/ADirector.cpp": "static constexpr uint32 kStateTag = 0x4D524146u; ///< 'FARM'\n",
            },
        },
        {
            "name": "키트가 키를 직접 읽는다",
            "files": {
                "Source/GameFramework/Kits/Genre/Simulation/Probe/ProbeSimulation.cpp": "void f( const InputManager& input ) { if ( input.isKeyDown( Key::W ) ) {} }\n",
            },
        },
        {
            "name": "키트가 입력 맵 액션 이름을 박는다",
            "files": {
                "Source/GameFramework/Kits/Genre/Action/Probe/ProbeController.cpp": "bool f( const InputMap& map ) { return map.wasActionTriggered( hashed_string( \"Jump\" ) ); }\n",
            },
        },
        {
            "name": "키트 설정 칸에 키트 접두가 없다",
            "files": {
                "Source/GameFramework/Kits/Genre/RPG/Probe/ProbeSave.cpp": "int32 f( const GameSettings& settings ) { return settings.getCustomPropertyInt( \"maxPartySize\", 6 ); }\n",
            },
        },
        {
            "name": "키트 평판 세력에 키트 접두가 없다",
            "files": {
                "Source/GameFramework/Kits/Genre/Simulation/ProbeSim/ProbeSimulation.h": "struct ProbeSettings { hashed_string _reputationFaction{ \"guests\" }; };\n",
            },
        },
        {
            "name": "키트 클래스가 공유 지갑을 값으로 든다",
            "files": {
                "Source/GameFramework/Kits/Genre/Simulation/ProbeSim/ProbeShop.h": "class ProbeShop\n{\nprivate:\n    Wallet _wallet;\n};\n",
            },
        },
        {
            "name": "키트가 빌린 일지의 알림을 꺼낸다",
            "files": {
                "Source/GameFramework/Kits/Genre/RPG/Probe/ProbeTown.cpp": "void ProbeTown::tick() { vector<QuestEvent> list; _pQuestLog->drainEvents( list ); }\n",
            },
        },
        {
            "name": "두 파일이 같은 로그 범주를 쓴다",
            "files": {
                "Source/GameFramework/Kits/Genre/Action/ProbeA/ProbeA.cpp": "SW_LOG_CALLER( \"Probe\" );\n",
                "Source/Games/ProbeGame/ProbeGame.cpp": "SW_LOG_CALLER( \"Probe\" );\n",
            },
        },
        {
            "name": "키트가 전역 변수를 정의한다",
            "files": {
                "Source/GameFramework/Kits/Genre/Casual/Probe/ProbeRace.cpp": "SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_probeLaps, 3, \"laps\" );\n",
            },
        },
        {
            "name": "게임이 키를 직접 읽는다",
            "files": {
                "Source/Games/ProbeGame/ProbeDirector.cpp": "void f( const InputManager& input ) { if ( input.wasKeyPressed( Key::Space ) ) {} }\n",
            },
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        pass

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listFile = self.selectTargetFiles(repositoryRoot, None, listScanRoot=kLintTargetRelDirs, suffixes=(".h", ".cpp"))
        mapTagToSite: dict[str, list[str]] = defaultdict(list)
        mapLogCallerToSite: dict[str, list[str]] = defaultdict(list)
        listViolation: list[str] = []
        for path in listFile:
            relativePath = path.relative_to(repositoryRoot).as_posix()
            text = path.read_text(encoding="utf-8", errors="replace")
            # 1) 상태 표
            if "Tag" in text:
                code1 = blankComments(text)
                for match in _kTagRe.finditer(code1):
                    mapTagToSite[match.group(2)].append(f"{relativePath}:{lineOfInternal(code1, match.start())}")
                for match in _kHexTagRe.finditer(code1):
                    listViolation.append(f"{relativePath}:{lineOfInternal(code1, match.start())}: {match.group(1)} 를 16 진 리터럴로 적었습니다 — "
                                         f"FourCcUtil::make( \"ABCD\" ) 로")
            bKit  = relativePath.startswith(_kKitPrefix)
            bGame = relativePath.startswith(_kGamePrefix)
            if bKit is False and bGame is False and relativePath.startswith(_kGameFrameworkPrefix) is False:
                continue
            # 주석 속 예시는 보지 않는다(글자 리터럴은 남긴다).
            code = blankComments(text)
            # 7) 로그 범주 — GameFramework · Games
            for match in _kLogCallerRe.finditer(code):
                mapLogCallerToSite[match.group(1)].append(f"{relativePath}:{lineOfInternal(code, match.start())}")
            # 9) 게임도 키를 직접 읽지 않는다
            if bGame:
                for match in _kRawKeyRe.finditer(code):
                    listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 게임이 키를 직접 읽습니다({match.group(0).strip()}) — "
                                         f"팩의 입력 맵(data/<게임>.input.xml) 액션으로 읽으세요")
            if bKit is False:
                continue
            # 2) · 3) 키트
            for match in _kRawKeyRe.finditer(code):
                listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 키트가 키를 직접 읽습니다({match.group(0).strip()}) — "
                                     f"입력 맵 액션을 키트 설정 칸으로 받으세요")
            # 5) 기반 공유 상태를 값으로 든다(헤더의 멤버)
            if relativePath.endswith(".h"):
                for key in self.mapExemption:
                    if key.rsplit(":", 1)[0] == relativePath:
                        self.seeExemption(key)
                for match in _kHeldStateRe.finditer(code):
                    member = match.group(2)
                    listOwner = _kOwnerTypeRe.findall(code, 0, match.start())
                    ownerName = listOwner[-1] if listOwner else ""
                    if ownerName.endswith(_kDefinitionSuffixes):
                        continue
                    if f"{relativePath}:{member}" in self.mapExemption:
                        self.useExemption(f"{relativePath}:{member}")
                        continue
                    listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 키트가 공유 상태 {match.group(1)} 를 값으로 듭니다({member}) — "
                                         f"`const GameStateRefs&` 로 빌리세요(정말 제 것이면 mapExemption 에 까닭과 함께)")
            # 6) 빌린 객체의 알림
            for match in _kBorrowedDrainRe.finditer(code):
                listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 키트가 빌린 객체의 알림을 꺼냅니다 — "
                                     f"알림은 게임 화면의 것이다, 상태(getStatus …)를 보세요")
            # 8) 전역 변수
            for match in _kGlobalVariableRe.finditer(code):
                listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 키트가 전역 변수를 정의합니다 — "
                                     f"스위치는 게임 · 엔진에, 키트는 설정 칸으로 받으세요")
            for match in _kActionLiteralRe.finditer(code):
                listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 키트가 입력 맵 액션 이름을 글자로 박았습니다 — "
                                     f"키트 설정 칸(hashed_string)으로 받으세요")
            kitName = findKitNameInternal(relativePath)
            for match in _kCustomPropertyRe.finditer(code):
                key = match.group(1)
                if kitName and not key.startswith(kitName + "."):
                    listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 게임 설정 칸 '{key}' 에 키트 접두가 없습니다 — "
                                         f"'{kitName}.{key}'")
            for match in _kFactionLiteralRe.finditer(code):
                factionId = next(group for group in match.groups() if group is not None)
                prefix = factionId.split(".", 1)[0] if "." in factionId else ""
                if kitName and (prefix == "" or prefix.islower() is False or prefix not in kitName.lower()):
                    listViolation.append(f"{relativePath}:{lineOfInternal(code, match.start())}: 평판 세력 '{factionId}' 에 키트 접두가 없습니다 — "
                                         f"'<{kitName} 의 소문자 낱말>.{factionId}'")
        for tagText, listSite in sorted(mapTagToSite.items()):
            if len(listSite) > 1:
                listViolation.append(f"상태 표 '{tagText}' 가 둘 이상입니다: {' · '.join(listSite)}")
        for callerName, listSite in sorted(mapLogCallerToSite.items()):
            if len(listSite) > 1:
                listViolation.append(f"로그 범주 '{callerName}' 가 GameFramework · Games 의 파일 둘 이상에 있습니다: {' · '.join(listSite)}")
        return GateResult(listViolation=listViolation, summary=f"{len(listFile)} files, {len(mapTagToSite)} state tags")


main = CheckKitNamespacesGate.run


if __name__ == "__main__":
    sys.exit(main())
