#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/GenerateConfigureFiles.py

configure 가 파이썬 생성기 다섯을 **한 프로세스**로 부른다 — 생성기 하나마다 파이썬 시작 · `import common` 값을 따로 내면 configure 시간의
3 할 가까이가 파이썬이었다(Ninja-Debug 실측: 생성기 일곱 1007 ms / configure 3486 ms). 각 생성기는 그대로 단독 실행도 된다 — 이 파일은 그
`generate*()` 함수를 차례로 부를 뿐이다(규칙 · 출력은 각 생성기 한 자리).

  - ConfigVars.cmake       (GenerateCMakeConstants)      — Constants.py → CMake set()
  - PackFormat.gen.h       (GeneratePackFormat)          — PackFormat.json → C++
  - CookContract.gen.h · CookContract.cmake (GenerateCookContract) — CookContract.json → C++ · CMake
  - ShippingHostDefaults.h (GenerateShippingHostDefaults) — 엔진 설정 + 게임 프리셋 → C++ (프리셋이 없으면 건너뛴다 — CMake 가 그 자리에서 멈춘다)
  - LintTargets.cmake      (GenerateLintTargets)         — lint/gate · selftest 폴더 → CMake 린트 타깃

ToolchainVars.cmake(GenerateToolchainCMake)는 여기 없다 — SetupEnvironment 가 toolchain_config.json 을 쓴 **뒤**에 돌아야 한다.

  python Scripts/generate/GenerateConfigureFiles.py <출력 폴더> --game <게임 이름>
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common · generate

from common import getProjectRoot, kDirRuntimeGamePreset, runGenerator  # noqa: E402
from generate.GenerateCMakeConstants import generateCMakeConstants  # noqa: E402
from generate.GenerateCookContract import generateCookContract  # noqa: E402
from generate.GenerateLintTargets import generateLintTargets  # noqa: E402
from generate.GeneratePackFormat import generatePackFormatHeader  # noqa: E402
from generate.GenerateShippingHostDefaults import generateShippingHostDefaults  # noqa: E402

kTag = "GenerateConfigureFiles"


def generateConfigureFiles(outputDir: Path, gameName: str) -> None:
    """configure 산출물 여섯을 `outputDir` 에 — 각 생성기를 단독으로 돌린 것과 같은 바이트."""
    generateCMakeConstants(outputDir / "ConfigVars.cmake")
    generatePackFormatHeader(outputDir / "PackFormat.gen.h")
    generateCookContract(outputDir / "CookContract.gen.h", outputDir / "CookContract.cmake")
    gamePreset = f"{kDirRuntimeGamePreset}/{gameName}.json"
    if (getProjectRoot() / gamePreset).is_file():
        generateShippingHostDefaults(outputDir / "ShippingHostDefaults.h", gamePreset)
    generateLintTargets(outputDir / "LintTargets.cmake")


def addArgumentsInternal(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("outputDir", type=Path, help="쓸 폴더(<빌드>/generated/sw/config)")
    parser.add_argument("--game", required=True, help="활성 게임(SW_ACTIVE_GAME) — 프리셋 Config/Game/<게임>.json")


def main(argv: Sequence[str] | None = None) -> int:
    return runGenerator(argv, tag=kTag, description="configure 생성기 다섯을 한 프로세스로",
                        addArguments=addArgumentsInternal,
                        generate=lambda args: generateConfigureFiles(args.outputDir, args.game))


if __name__ == "__main__":
    sys.exit(main())
