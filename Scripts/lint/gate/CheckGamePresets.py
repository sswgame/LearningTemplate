#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckGamePresets.py

게임마다 프리셋(`Config/Game/<게임 폴더 이름>.json`)이 있고, 그것이 실제 팩을 가리키는지 검사합니다.

빌드는 `SW_ACTIVE_GAME` 으로 프리셋 파일을 골라 팩 루트 · gamesettings · 시작 씬을 정합니다(Shipping 은 그 파일을 구워 넣는다).
프리셋이 없으면 그 게임을 고른 configure 가 멈추고, 팩 루트가 틀리면 그 게임은 다른 게임의 팩이나 빈 팩을 읽는다.

검사 규칙:
- `Source/Games/<G>/CMakeLists.txt` 가 있으면 `Config/Game/<G>.json` 이 있어야 한다.
- 프리셋에는 게임 폴더가 있어야 한다(지운 게임의 프리셋이 남지 않게).
- `_packRoot` 는 `game/<폴더>` 이고 `Resource/<_packRoot>/` 가 있어야 한다.
- `_startupScene` 이 있으면 그 파일이 `Resource/` 아래에 있어야 한다.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from LintGate import GateResult, LintGate  # noqa: E402

#: 게임 프리셋 폴더(Constants.py 의 kDirRuntimeGamePreset 와 같다).
_kPresetFolder = "Config/Game"
_kGamesFolder = "Source/Games"


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
        packRoot = str(data.get("_packRoot", ""))
        if packRoot.startswith("game/") is False or "/" in packRoot[len("game/"):]:
            violations.append(f"[Game Preset] `_packRoot` 는 `game/<폴더>` 입니다: {presetPath} (`{packRoot}`)")
        elif (resourceRoot / packRoot).is_dir() is False:
            violations.append(f"[Game Preset] `_packRoot` 의 폴더가 없습니다: {presetPath} → Resource/{packRoot}/")
        startupScene = str(data.get("_startupScene", ""))
        if startupScene and (resourceRoot / startupScene).is_file() is False:
            violations.append(f"[Game Preset] `_startupScene` 파일이 없습니다: {presetPath} → Resource/{startupScene}")
    return violations, len(listGame)


class CheckGamePresetsGate(LintGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다 — 규칙과 증거가 한 자리에 있어 어긋날 수 없다."""

    description = "게임마다 Config/Game/<게임>.json 프리셋이 있고 실제 팩 · 시작 씬을 가리킨다"
    buildComment = "Checking every game has a preset pointing at its own pack..."
    timeoutSeconds = 15
    preCommitPattern = ("Config/Game/*", "Source/Games/*/CMakeLists.txt", "Resource/game/*")
    preCommitFileArgument = ""
    violationHeader = "게임 프리셋 위반"
    hint = ("  `Config/Game/<게임 폴더 이름>.json` 에 `_packRoot`(`game/<폴더>`) · `_gameSettingsFile` · `_startupScene` 을 적습니다. "
            "게임 폴더를 지웠으면 그 프리셋도 지웁니다.")
    selfTestCases = [
        {
            "name": "프리셋 없는 게임",
            "files": {
                "Source/Games/Probe/CMakeLists.txt": "sw_addGameModule(SWGame)",
                "Resource/game/probe/readme.md": "probe",
            },
        },
        {
            "name": "없는 팩을 가리키는 프리셋",
            "files": {
                "Source/Games/Probe/CMakeLists.txt": "sw_addGameModule(SWGame)",
                "Config/Game/Probe.json": '{ "_packRoot": "game/nowhere" }',
            },
        },
        {
            "name": "게임 폴더가 없는 프리셋",
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
