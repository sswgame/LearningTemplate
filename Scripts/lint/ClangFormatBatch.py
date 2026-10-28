"""
Scripts/lint/ClangFormatBatch.py

clang-format 을 파일 묶음 단위로 병렬 실행한다(고치기 `-i` 또는 검사 `--dry-run --Werror`).
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Sequence

from common.ClangFormat import resolveClangFormat
from common.ExitCode import ExitCode
from common.Paths import getProjectRoot
from common.RunProcess import runProcess
from lint.Parallel import runUntilNonZero


def runClangFormatBatch(files: Sequence[Path | str],
                        *,
                        checkOnly: bool = False,
                        cwd: Path | None = None,
                        clangFormatPath: str | None = None,
                        batchSize: int = 64) -> int:
    """
    지정된 파일 목록에 대해 clang-format을 배치 단위로 병렬 실행합니다.

    Args:
        files: 포맷팅할 파일 경로 목록
        checkOnly: True이면 --dry-run --Werror 모드로 검사만 수행, False이면 -i 모드로 파일 직접 수정
        cwd: 실행 기준 디렉터리 (기본값: 프로젝트 루트)
        clangFormatPath: 사용할 clang-format 실행 파일 경로 (None이면 자동 탐색)
        batchSize: 1회 배치당 처리할 파일 개수 (기본 64개)

    Returns:
        모든 배치가 성공하면 0, 실패 시 해당 프로세스 종료 코드 반환
    """
    if not files:
        return ExitCode.Passed
    projectRoot = cwd or getProjectRoot()
    clangFormatExe = clangFormatPath or resolveClangFormat()
    if not clangFormatExe:
        sys.stderr.write(
            "[FormatClangFormat] clang-format을 찾을 수 없습니다. "
            "'py -3 Scripts/setup/SetupEnvironment.py'를 먼저 실행해주세요.\n"
        )
        return ExitCode.Violation

    filePathStrings = [str(f) for f in files]
    batches = [
        filePathStrings[startIndex : startIndex + batchSize]
        for startIndex in range(0, len(filePathStrings), batchSize)
    ]

    def runSingleBatchInternal(batch: list[str]) -> int:
        command = [clangFormatExe]
        if checkOnly:
            command.extend(["--dry-run", "--Werror"])
        else:
            command.append("-i")
        command.extend(batch)
        return runProcess(command, cwd=projectRoot, bCapture=False).returnCode

    # clang-format 은 자식 프로세스라 코어 수를 넘겨 띄우면 서로 경합한다 (Parallel 의 프로세스 정책).
    return runUntilNonZero(runSingleBatchInternal, batches)
