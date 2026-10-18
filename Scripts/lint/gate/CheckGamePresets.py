#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckGamePresets.py

게임마다 프리셋(`Config/Game/<게임 폴더 이름>.json`) · 매니페스트(`SWGame.module.json`)가 있고, 프리셋이 실제 팩을 가리키는지 검사합니다.

Dev 는 실행할 때 `-game=<게임>` 으로 프리셋 파일을 골라 팩 루트를 정하고(`GameSelection`), Shipping 은 `SW_ACTIVE_GAME` 의 파일을 구워 넣습니다.
프리셋이 없으면 그 게임을 고른 기동 · configure 가 멈추고, 팩 루트가 틀리면 그 게임은 다른 게임의 팩이나 빈 팩을 읽는다.
매니페스트가 없으면 그 게임을 함께 짓는 구성(`SW_GAMES`)이 멈춘다 — 어느 키트를 링크할지 매니페스트가 정한다.

검사 규칙:
- `Source/Games/<G>/CMakeLists.txt` 가 있으면 `Config/Game/<G>.json` 이 있어야 한다.
- 프리셋에는 게임 폴더가 있어야 한다(지운 게임의 프리셋이 남지 않게).
- `_packRoot` 는 `game/<폴더>` 이고 `Resource/<_packRoot>/` 가 있어야 한다.
- `_userSettingsSchema` · `_telemetrySchema` 가 있으면 그 파일이 팩(`Resource/<_packRoot>/`) 아래에 있어야 한다.
- 게임 폴더마다 `SWGame.module.json`(게임 매니페스트)이 있다.
- CMake 프리셋이 `SW_ACTIVE_GAME` 을 정하면 있는 게임이어야 한다(게임별 프리셋은 없다 — `Ninja-Debug` 가 게임 전부를 짓고 `-game=` 으로 고른다).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kDirRuntimeGamePreset, kDirSourceGames  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

_kPresetFolder = kDirRuntimeGamePreset
_kGamesFolder = kDirSourceGames
#: 게임 매니페스트 파일 이름 — 모듈 이름 `SWGame`(엔진과의 계약)이다.
_kGameManifestName = "SWGame.module.json"


def findCMakePresetViolations(repositoryRoot: Path, listGame: list[str]) -> list[str]:
    """CMake 프리셋이 정한 `SW_ACTIVE_GAME` 이 있는 게임인지 봅니다."""
    presetPath = repositoryRoot / "CMakePresets.json"
    if presetPath.is_file() is False:
        return ["[Game Preset] CMakePresets.json 이 없습니다 — 프리셋이 고르는 게임을 검사할 수 없습니다"]
    try:
        data = json.loads(presetPath.read_text(encoding="utf-8"))
    except ValueError as error:
        return [f"[Game Preset] CMakePresets.json 을 읽지 못했습니다 ({error})"]
    mapConfigure = {preset.get("name"): preset for preset in data.get("configurePresets", [])}
    violations: list[str] = []
    for name, preset in mapConfigure.items():
        activeGame = preset.get("cacheVariables", {}).get("SW_ACTIVE_GAME")
        if activeGame is not None and activeGame not in listGame:
            violations.append(f"[Game Preset] CMake 프리셋 {name} 이 없는 게임 `{activeGame}` 을 고릅니다")
    return violations


def findGamePresetViolations(repositoryRoot: Path) -> tuple[list[str], int]:
    """(위반 줄, 검사한 게임 수)를 돌려줍니다."""
    violations: list[str] = []
    gamesRoot = repositoryRoot / _kGamesFolder
    presetRoot = repositoryRoot / _kPresetFolder
    resourceRoot = repositoryRoot / "Resource"

    listGame = sorted(path.name for path in gamesRoot.iterdir() if (path / "CMakeLists.txt").is_file()) if gamesRoot.is_dir() else []
    listPreset = sorted(path.stem for path in presetRoot.glob("*.json")) if presetRoot.is_dir() else []

    for game in listGame:
        if game not in listPreset:
            violations.append(f"[Game Preset] {_kGamesFolder}/{game} 의 프리셋이 없습니다: {_kPresetFolder}/{game}.json")
        if (gamesRoot / game / _kGameManifestName).is_file() is False:
            violations.append(f"[Game Preset] {_kGamesFolder}/{game} 의 게임 매니페스트가 없습니다: {_kGamesFolder}/{game}/{_kGameManifestName}")
    for preset in listPreset:
        presetPath = f"{_kPresetFolder}/{preset}.json"
        if preset not in listGame:
            violations.append(f"[Game Preset] 게임 폴더가 없는 프리셋입니다: {presetPath}")
            continue
        try:
            data = json.loads((presetRoot / f"{preset}.json").read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            violations.append(f"[Game Preset] JSON 을 읽지 못했습니다: {presetPath} ({error})")
            continue
        if "_packRoot" not in data:
            violations.append(f"[Game Preset] `_packRoot` 가 없습니다: {presetPath}")
            continue
        packRoot = str(data["_packRoot"])
        if packRoot.startswith("game/") is False or "/" in packRoot[len("game/"):]:
            violations.append(f"[Game Preset] `_packRoot` 는 `game/<폴더>` 입니다: {presetPath} (`{packRoot}`)")
        elif (resourceRoot / packRoot).is_dir() is False:
            violations.append(f"[Game Preset] `_packRoot` 의 폴더가 없습니다: {presetPath} → Resource/{packRoot}/")
        for schemaKey in ("_userSettingsSchema", "_telemetrySchema"):
            schemaPath = str(data.get(schemaKey, ""))
            if schemaPath and (resourceRoot / packRoot / schemaPath).is_file() is False:
                violations.append(f"[Game Preset] `{schemaKey}` 파일이 없습니다: {presetPath} → Resource/{packRoot}/{schemaPath}")
    violations.extend(findCMakePresetViolations(repositoryRoot, listGame))
    return violations, len(listGame)


class CheckGamePresetsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "게임마다 Config/Game/<게임>.json 프리셋 · SWGame.module.json 이 있고 프리셋이 실제 팩을 가리킨다"
    buildComment = "Checking every game has a preset pointing at its own pack..."
    timeoutSeconds = 15
    preCommitPattern = ("Config/Game/*", "Source/Games/*/CMakeLists.txt", "Source/Games/*/SWGame.module.json", "Resource/game/*", "CMakePresets.json")
    preCommitFileArgument = ""
    violationHeader = "게임 프리셋 위반"
    hint = ("  `Config/Game/<게임 폴더 이름>.json` 에 `_packRoot`(`game/<폴더>`)를 적습니다(시작 씬은 팩의 `data/gamesettings.xml`). "
            "게임 폴더를 지웠으면 그 프리셋도 지웁니다.")
    selfTestCases = [
        {
            "name": "프리셋 없는 게임",
            "expectedViolation": "Source/Games/Probe 의 프리셋이 없습니다: Config/Game/Probe.json",
            "files": {
                "Source/Games/Probe/CMakeLists.txt": "sw_addGameModule(SWGame)",
                "Resource/game/probe/readme.md": "probe",
            },
        },
        {
            "name": "없는 팩을 가리키는 프리셋",
            "expectedViolation": "`_packRoot` 의 폴더가 없습니다: Config/Game/Probe.json",
            "files": {
                "Source/Games/Probe/CMakeLists.txt": "sw_addGameModule(SWGame)",
                "Config/Game/Probe.json": '{ "_packRoot": "game/nowhere" }',
            },
        },
        {
            "name": "매니페스트 없는 게임",
            "expectedViolation": "Source/Games/Probe 의 게임 매니페스트가 없습니다",
            "files": {
                "Source/Games/Probe/CMakeLists.txt": "sw_addGameModule(SWGame)",
                "Config/Game/Probe.json": '{ "_packRoot": "game/probe" }',
                "Resource/game/probe/readme.md": "probe",
            },
        },
        {
            "name": "없는 게임을 고르는 CMake 프리셋",
            "expectedViolation": "[Game Preset] CMake 프리셋 Ninja-Debug 이 없는 게임 `Gone` 을 고릅니다",
            "files": {
                "Source/Games/Probe/CMakeLists.txt": "sw_addGameModule(SWGame)",
                "Source/Games/Probe/SWGame.module.json": "{}",
                "Config/Game/Probe.json": '{ "_packRoot": "game/probe" }',
                "Resource/game/probe/readme.md": "probe",
                "CMakePresets.json": '{ "configurePresets": [ { "name": "Ninja-Debug", "cacheVariables": { "SW_ACTIVE_GAME": "Gone" } } ] }',
            },
        },
        {
            "name": "없는 사용자 설정 스키마를 가리키는 프리셋",
            "expectedViolation": "`_userSettingsSchema` 파일이 없습니다: Config/Game/Probe.json",
            "files": {
                "Source/Games/Probe/CMakeLists.txt": "sw_addGameModule(SWGame)",
                "Config/Game/Probe.json": '{ "_packRoot": "game/probe", "_userSettingsSchema": "data/missing.settings.xml" }',
                "Resource/game/probe/readme.md": "probe",
            },
        },
        {
            "name": "게임 폴더가 없는 프리셋",
            "expectedViolation": "게임 폴더가 없는 프리셋입니다: Config/Game/Gone.json",
            "files": {
                "Config/Game/Gone.json": '{ "_packRoot": "game/gone" }',
                "Resource/game/gone/readme.md": "probe",
            },
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        violations, gameCount = findGamePresetViolations(repositoryRoot)
        return GateResult(listViolation=violations, summary=f"게임 {gameCount}개")


main = CheckGamePresetsGate.run


if __name__ == "__main__":
    sys.exit(main())
