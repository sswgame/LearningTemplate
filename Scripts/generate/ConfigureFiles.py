#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/ConfigureFiles.py

configure 가 파이썬 생성기 넷을 **한 프로세스**로 부른다 — 생성기 하나마다 파이썬 시작 · `import common` 값을 따로 내면 configure 시간의
3 할 가까이가 파이썬이었다(Ninja-Debug 실측: 생성기 일곱 1007 ms / configure 3486 ms). 각 생성기는 그대로 단독 실행도 된다 — 이 파일은 그
`generate*()` 함수를 차례로 부를 뿐이다(규칙 · 출력은 각 생성기 한 자리).

  - ConfigVars.cmake       (CMakeConstants)      — ProjectConstants.py → CMake set()
  - CookContract.gen.h · CookContract.cmake (CookContract) — CookContract.json → C++ · CMake
  - ProcessExitCodes.gen.h · ProcessExitCodes.cmake (ProcessExitCodes) — ProcessExitCodes.json → C++ · CMake
  - ShippingHostDefaults.h · ShippingGameDefaults.h (ShippingHostDefaults) — 엔진 설정 · 게임 프리셋 → C++ (프리셋이 없으면 건너뛴다 — CMake 가 그 자리에서 멈춘다)
  - EditorLayers.cmake     (EditorLayers)        — 에디터 층 표 → CMake 변수(층별 부분 · 위 층 헤더 껍데기)
  - EngineLayers.cmake     (EngineLayers)        — 엔진 층 표 → CMake 변수(층별 소스 · 헤더)
  - (검사만) ContentModules — 게임 콘텐츠가 쓰는 형식의 모듈이 그 게임 매니페스트 의존에 있는지(없으면 configure 가 멈춘다)

ToolchainVars.cmake(ToolchainCMake)는 여기 없다 — Environment 가 toolchain_config.json 을 쓴 **뒤**에 돌아야 한다.

  python Scripts/generate/ConfigureFiles.py <출력 폴더> --game <게임 이름>
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common · generate

from common.ProjectConstants import kDirRuntimeGamePreset  # noqa: E402
from generate.GeneratorOutput import runGenerator  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from generate.CMakeConstants import generateCMakeConstants  # noqa: E402
from generate.CookContract import generateCookContract  # noqa: E402
from generate.ProcessExitCodes import generateProcessExitCodes  # noqa: E402
from generate.EditorLayers import generateEditorLayers  # noqa: E402
from generate.ContentModules import checkContentModules  # noqa: E402
from generate.EngineLayers import generateEngineLayers  # noqa: E402
from generate.ShippingHostDefaults import generateShippingHostDefaults  # noqa: E402

kTag = "ConfigureFiles"


def generateConfigureFiles(outputDir: Path, gameName: str) -> None:
    """configure 산출물 넷을 `outputDir` 에 — 각 생성기를 단독으로 돌린 것과 같은 바이트."""
    generateCMakeConstants(outputDir / "ConfigVars.cmake")
    generateCookContract(outputDir / "CookContract.gen.h", outputDir / "CookContract.cmake")
    generateProcessExitCodes(outputDir / "ProcessExitCodes.gen.h", outputDir / "ProcessExitCodes.cmake")
    gamePreset = f"{kDirRuntimeGamePreset}/{gameName}.json"
    if (getProjectRoot() / gamePreset).is_file():
        generateShippingHostDefaults(outputDir / "ShippingHostDefaults.h", outputDir / "ShippingGameDefaults.h", gamePreset)
    generateEditorLayers(outputDir / "EditorLayers.cmake")
    generateEngineLayers(outputDir / "EngineLayers.cmake")
    checkContentModules(getProjectRoot())


def addArgumentsInternal(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("outputDir", type=Path, help="쓸 폴더(<빌드>/generated/sw/config)")
    parser.add_argument("--game", required=True, help="활성 게임(SW_ACTIVE_GAME) — 프리셋 Config/Game/<게임>.json")


def main(argv: Sequence[str] | None = None) -> int:
    return runGenerator(argv, tag=kTag, description="configure 생성기 넷을 한 프로세스로",
                        addArguments=addArgumentsInternal,
                        generate=lambda args: generateConfigureFiles(args.outputDir, args.game))


if __name__ == "__main__":
    sys.exit(main())
