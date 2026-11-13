#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/configure_files.py

configure 가 파이썬 생성기 넷을 **한 프로세스**로 부른다 — 생성기 하나마다 파이썬 시작 · `import common` 값을 따로 내면 configure 시간의
3 할 가까이가 파이썬이었다(Ninja-Debug 실측: 생성기 일곱 1007 ms / configure 3486 ms). 각 생성기는 그대로 단독 실행도 된다 — 이 파일은 그
`generate*()` 함수를 차례로 부를 뿐이다(규칙 · 출력은 각 생성기 한 자리).

  - ConfigVars.cmake       (CMakeConstants)      — project_constants.py → CMake set()
  - CookContract.gen.h · CookContract.cmake (CookContract) — CookContract.json → C++ · CMake
  - ProcessExitCodes.gen.h · ProcessExitCodes.cmake (ProcessExitCodes) — ProcessExitCodes.json → C++ · CMake
  - ShippingHostDefaults.h · ShippingGameDefaults.h (ShippingHostDefaults) — 엔진 설정 · 게임 프리셋 → C++ (프리셋이 없으면 건너뛴다 — CMake 가 그 자리에서 멈춘다)
  - EditorLayers.cmake     (EditorLayers)        — 에디터 층 표 → CMake 변수(층별 부분 · 위 층 헤더 껍데기)
  - EngineLayers.cmake · GameFrameworkLayers.cmake (ModuleLayers) — 모듈 층 표 → CMake 변수(층별 소스 · 헤더)
  - TargetLayers.cmake     (TargetLayers)        — 타깃 경계 표 · 매니페스트 → 타깃별 `#error` 껍데기와 그 폴더(CMake 변수)

ToolchainVars.cmake(ToolchainCMake)는 여기 없다 — Environment 가 toolchain_config.json 을 쓴 **뒤**에 돌아야 한다.

  python Scripts/generate/configure_files.py <출력 폴더> --game <게임 이름>
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common · generate

from common.project_constants import DIR_RUNTIME_GAME_PRESET  # noqa: E402
from generate.generator_output import run_generator  # noqa: E402
from common.paths import get_project_root  # noqa: E402
from generate.cmake_constants import generate_cmake_constants  # noqa: E402
from generate.cook_contract import generate_cook_contract  # noqa: E402
from generate.process_exit_codes import generate_process_exit_codes  # noqa: E402
from generate.editor_layers import generate_editor_layers  # noqa: E402
from generate.module_layers import LIST_LAYER_TABLE, generate_module_layers  # noqa: E402
from generate.shipping_host_defaults import generate_shipping_host_defaults  # noqa: E402
from generate.target_layers import generate_target_layers  # noqa: E402

TAG = "ConfigureFiles"


def generate_configure_files(output_dir: Path, game_name: str) -> None:
    """configure 산출물 넷을 `outputDir` 에 — 각 생성기를 단독으로 돌린 것과 같은 바이트."""
    generate_cmake_constants(output_dir / "ConfigVars.cmake")
    generate_cook_contract(output_dir / "CookContract.gen.h", output_dir / "CookContract.cmake")
    generate_process_exit_codes(output_dir / "ProcessExitCodes.gen.h", output_dir / "ProcessExitCodes.cmake")
    game_preset = f"{DIR_RUNTIME_GAME_PRESET}/{game_name}.json"
    if (get_project_root() / game_preset).is_file():
        generate_shipping_host_defaults(output_dir / "ShippingHostDefaults.h", output_dir / "ShippingGameDefaults.h", game_preset)
    generate_editor_layers(output_dir / "EditorLayers.cmake")
    for table in LIST_LAYER_TABLE:
        generate_module_layers(table._module, output_dir / f"{table._module}Layers.cmake")
    generate_target_layers(output_dir / "TargetLayers.cmake")


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("output_dir", type=Path, help="쓸 폴더(<빌드>/generated/sw/config)")
    parser.add_argument("--game", required=True, help="활성 게임(SW_ACTIVE_GAME) — 프리셋 Config/Game/<게임>.json")


def main(argv: Sequence[str] | None = None) -> int:
    return run_generator(argv, tag=TAG, description="configure 생성기 넷을 한 프로세스로",
                        add_arguments=_add_arguments,
                        generate=lambda args: generate_configure_files(args.output_dir, args.game))


if __name__ == "__main__":
    sys.exit(main())
