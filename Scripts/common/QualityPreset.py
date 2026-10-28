#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
골든 · 기준을 뜨는 러너가 App 에 품질 프리셋을 명시하는 자리.

App 은 사용자 설정 파일이 없고 사람이 켠 실행이면 사양(HardwareProbe)으로 프리셋을 고르고, 있으면 그 PC 의 파일을 읽는다. 무인 실행(`-unattended` ·
`-scenario`)은 자동 선택을 하지 않지만 `-gv_screenshot` · `-gv_profileFrames` 로 도는 러너는 무인 인자가 없어 기계마다 다른 프리셋으로 그렸다(벤치 골든 8 장이
이 PC 의 'low' 로 잘못 떠졌다 — 862ad03f3). 그래서 러너는 프리셋 이름을 표(`Test/QA/Games.json` 의 `quality`)에서 읽어 사용자 설정 파일로 써 주고
`-gv_userSettingsFile` 로 가리키며, 골든 · 기준 메타에 그 이름을 적고 견줄 때 다르거나 없으면 거절한다. 표에 이름이 없으면 오류로 멈춘다(기본값을 두지 않는다).

새 러너가 같은 틈을 다시 열지 못하게 App 을 띄우는 공통 입구(`AppRun.runApp`)가 `SettingsIsolation` 을 필수 키워드로 받고 `checkSettingsIsolation` 으로 인자와 맞는지
본다 — 고르지 않으면 TypeError, 말과 인자가 어긋나면 ValueError 라 "프리셋 없이 띄우는" 호출을 쓸 수 없다.
"""

from __future__ import annotations

import enum
import json
import xml.etree.ElementTree as ElementTree
from pathlib import Path

#: 사용자 설정 파일을 가리키는 App 인자의 이름(`UserSettingsVariables.cpp` 의 `gv_userSettingsFile`).
kUserSettingsArgument = "-gv_userSettingsFile"
#: 무인 실행을 알리는 App 인자(`Source/Core/Predefined/ArgumentList.xxx` 의 `-unattended` · `-scenario`) — 엔진은 이때 사양으로 프리셋을 고르지 않는다(a0521e79a).
kAutomatedArgumentPrefixes: tuple[str, ...] = ("-unattended", "-scenario")


class SettingsIsolation(enum.Enum):
    """`runApp` 호출이 사용자 설정(그 PC 의 파일 · 사양으로 고른 프리셋)에서 어떻게 떨어졌는지의 선언."""

    PRESET_FILE = "preset-file"   #: 품질 프리셋을 쓴 설정 파일을 `-gv_userSettingsFile` 로 건넨다(`writePresetSettingsFile`). 그리는 측정 · 골든은 이것이다.
    ISOLATED_AUTOMATED = "isolated-automated"   #: 무인 인자(`-unattended`/`-scenario`) + 없는 격리 설정 파일 — 프리셋은 스키마 기본값이다.
    UNATTENDED_ONLY = "unattended-only"   #: 무인 인자만 있다 — 엔진이 사양으로 프리셋을 고르지는 않지만 이 PC 의 사용자 폴더 설정 파일은 읽는다(시나리오 실행기가 쓴다. 되도록 위 둘로 좁힌다).
    NOT_APPLICABLE = "not-applicable"  #: 사용자 설정을 읽지 않거나 결과가 그림 · 품질에 달리지 않는 실행(테스트 실행 파일 · 서버 · 쿠킹 · 쓰기 위치 조사). 호출부에 이유를 적는다.


def checkSettingsIsolation(isolation: SettingsIsolation, listArgument: list[str]) -> None:
    """선언이 실제 인자와 맞지 않으면 ValueError — `runApp` 이 App 을 띄우기 전에 부른다."""
    bSettingsFile = any(argument.startswith(f"{kUserSettingsArgument}=") and len(argument) > len(kUserSettingsArgument) + 1 for argument in listArgument)
    bAutomated = any(argument == prefix or argument.startswith(f"{prefix}=") for argument in listArgument for prefix in kAutomatedArgumentPrefixes)
    if isolation is SettingsIsolation.PRESET_FILE and not bSettingsFile:
        raise ValueError(f"PRESET_FILE 인데 인자에 {kUserSettingsArgument}=<경로> 가 없다: {listArgument}")
    if isolation is SettingsIsolation.ISOLATED_AUTOMATED and not (bSettingsFile and bAutomated):
        raise ValueError(f"ISOLATED_AUTOMATED 인데 인자에 무인 인자와 {kUserSettingsArgument}=<경로> 가 함께 있지 않다: {listArgument}")
    if isolation is SettingsIsolation.UNATTENDED_ONLY and not bAutomated:
        raise ValueError(f"UNATTENDED_ONLY 인데 인자에 {' · '.join(kAutomatedArgumentPrefixes)} 가 없다: {listArgument}")


def readScalabilityPreset(repositoryRoot: Path, quality: str) -> dict[str, str]:
    """엔진 설정 스키마(`Resource/engine/settings/engine.settings.xml` 의 <Scalability>)에서 품질 프리셋이 묶는 설정 값들 — 프리셋 표를 여기 다시 적지 않는다."""
    root = ElementTree.parse(repositoryRoot / "Resource/engine/settings/engine.settings.xml").getroot()
    for scalability in root.iter("Scalability"):
        for preset in scalability.findall("Preset"):
            if preset.get("name") == quality:
                mapValue = {value.attrib["setting"]: value.attrib["value"] for value in preset.findall("Value")}
                return {scalability.attrib["setting"]: quality, **mapValue}
    raise ValueError(f"quality '{quality}' is not a preset in engine.settings.xml")


def readTableQuality(table: dict) -> str:
    """게임 표(`Test/QA/Games.json`)가 정한 골든 · 기준의 품질 프리셋 이름입니다. 표에 없으면 KeyError — 기본값을 두지 않는다."""
    return table["quality"]


def writePresetSettingsFile(repositoryRoot: Path, quality: str, settingsPath: Path) -> list[str]:
    """프리셋 값을 담은 사용자 설정 파일을 쓰고 App 에 줄 인자(`-gv_userSettingsFile=<경로>`)를 돌려줍니다. 그 파일이 있으면 App 은 사양으로 프리셋을 고르지 않는다."""
    values = readScalabilityPreset(repositoryRoot, quality)
    settingsPath.write_text(json.dumps({"version": 1, "values": values}) + "\n", encoding="utf-8")
    return [f"{kUserSettingsArgument}={settingsPath.as_posix()}"]


def describeQualityMismatch(recordedQuality: str | None, wantedQuality: str) -> str:
    """기준에 적힌 프리셋이 이번 실행의 것과 다르면 거절 글, 같으면 빈 글입니다. 기준에 프리셋이 없으면 어느 프리셋으로 떴는지 모르므로 거절이다."""
    if recordedQuality is None:
        return f"the reference does not record its quality preset (wanted '{wantedQuality}') - re-record it"
    if recordedQuality != wantedQuality:
        return f"the reference was captured at quality '{recordedQuality}', this run uses '{wantedQuality}' - re-record it"
    return ""
