"""
Scripts/generate/configure_files.py

configure 생성기(`Generator` 하위 클래스, `Scripts/generate/*.py`)를 한 프로세스로 모두 돈다. 생성기 하나마다 파이썬 시작 값을 따로 내면 configure 시간의
3 할 가까이가 파이썬이었다(Ninja-Debug 실측: 생성기 일곱 1007 ms / configure 3486 ms). 무엇을 쓰는지는 각 생성기 파일이 정한다.
도는 동안 읽은 파일을 `ConfigureFiles.deps` 로 적어 CMake 가 configure 의존으로 삼는다.

ToolchainVars.cmake(toolchain_cmake)는 여기 없다 — Environment 가 toolchain_config.json 을 쓴 뒤에 돌아야 한다.

  python Scripts/generate/configure_files.py <출력 폴더> --game <게임 이름>
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts — common · generate

from common.command import Command, ScriptContext  # noqa: E402
from common.exit_code import ExitCode  # noqa: E402
from generate.generator_output import find_generators, record_read_files, write_input_list  # noqa: E402

INPUT_LIST_FILE_NAME = "ConfigureFiles.deps"   # GenerateConfigConstants.cmake 가 읽어 CMAKE_CONFIGURE_DEPENDS 에 넣는다


class ConfigureFilesCommand(Command):
    name = "configure-files"
    help = "configure 생성기를 모두 돌고 읽은 입력 목록을 쓴다(GenerateConfigConstants.cmake 가 부른다)"

    def add_arguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("output_dir", type=Path, help="쓸 폴더(<빌드>/generated/sw/config)")
        parser.add_argument("--game", required=True, help="활성 게임(SW_ACTIVE_GAME) — 프리셋 Config/Game/<게임>.json")

    def run(self, context: ScriptContext, args: argparse.Namespace) -> int:
        with record_read_files(context.root, args.output_dir) as read_files:
            for generator in find_generators():
                generator.generate(args.output_dir, args.game)
        write_input_list(args.output_dir / INPUT_LIST_FILE_NAME, read_files, tag="ConfigureFiles")
        return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(ConfigureFilesCommand.main())
