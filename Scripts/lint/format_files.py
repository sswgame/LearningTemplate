"""
Scripts/lint/format_files.py

포맷 대상 고르기와 일괄 실행 — 고치는 명령(`format`) · 커밋 훅 · 이름 바꾸기 도구가 함께 쓴다. 대상은 git 이 아는 파일이다(빌드 산출물 · 받아 온 외부 트리를 줍지 않는다).
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path
from typing import Sequence

from common.clang_format import resolve_clang_format
from common.exit_code import ExitCode
from common.paths import get_project_root
from common.project_constants import CPP_ALL_EXTENSIONS, LINT_TARGET_REL_DIRS, NOT_OUR_DIR_NAMES, SHADER_SOURCE_EXTENSIONS
from common.run_process import run_process
from lint.lint_scope import LintScope
from lint.parallel import run_until_non_zero

CPP_SUFFIXES = tuple(sorted(CPP_ALL_EXTENSIONS))
SHADER_SUFFIXES = tuple(sorted(SHADER_SOURCE_EXTENSIONS))


def select_format_files(scope: LintScope) -> list[str]:
    """clang-format 이 볼 파일 — C++ 은 `LINT_TARGET_REL_DIRS`, 셰이더는 남의 코드 · 산출물 폴더 밖. 받아 온 남의 헤더는 고치지 않는다."""
    list_cpp = scope.select(LINT_TARGET_REL_DIRS, CPP_SUFFIXES)
    list_shader = [path for path in scope.select(suffixes=SHADER_SUFFIXES) if not any(part in NOT_OUR_DIR_NAMES for part in path.split("/")[:-1])]
    return list_cpp + list_shader


def select_vcpkg_manifests(scope: LintScope) -> list[str]:
    return [path for path in scope.list_file if path.rsplit("/", 1)[-1] == VCPKG_MANIFEST_NAME]


def run_clang_format_batch(files: Sequence[Path | str],
                        *,
                        check_only: bool = False,
                        cwd: Path | None = None,
                        clang_format_path: str | None = None,
                        batch_size: int = 64) -> int:
    """
    지정된 파일 목록에 대해 clang-format을 배치 단위로 병렬 실행합니다.

    Args:
        files: 포맷팅할 파일 경로 목록
        check_only: True이면 --dry-run --Werror 모드로 검사만 수행, False이면 -i 모드로 파일 직접 수정
        cwd: 실행 기준 디렉터리 (기본값: 프로젝트 루트)
        clang_format_path: 사용할 clang-format 실행 파일 경로 (None이면 자동 탐색)
        batch_size: 1회 배치당 처리할 파일 개수 (기본 64개)

    Returns:
        모든 배치가 성공하면 0, 실패 시 해당 프로세스 종료 코드 반환
    """
    if not files:
        return ExitCode.PASSED
    project_root = cwd or get_project_root()
    clang_format_exe = clang_format_path or resolve_clang_format()
    if not clang_format_exe:
        sys.stderr.write(
            "[FormatClangFormat] clang-format을 찾을 수 없습니다. "
            "'py -3 Scripts/setup/environment.py'를 먼저 실행해주세요.\n"
        )
        return ExitCode.VIOLATION

    file_path_strings = [str(f) for f in files]
    batches = [
        file_path_strings[start_index : start_index + batch_size]
        for start_index in range(0, len(file_path_strings), batch_size)
    ]

    def _run_single_batch(batch: list[str]) -> int:
        command = [clang_format_exe]
        if check_only:
            command.extend(["--dry-run", "--Werror"])
        else:
            command.append("-i")
        command.extend(batch)
        return run_process(command, cwd=project_root, capture=False).return_code

    # clang-format 은 자식 프로세스라 코어 수를 넘겨 띄우면 서로 경합한다 (Parallel 의 프로세스 정책).
    return run_until_non_zero(_run_single_batch, batches)


VCPKG_MANIFEST_NAME = "vcpkg.json"


def run_vcpkg_manifest_format(files: Sequence[Path], *, check_only: bool, vcpkg_command: Sequence[str | Path] | None = None) -> int:
    """
    `vcpkg.json` 들을 `vcpkg format-manifest` 모양(의존 이름 순 · 필드 순)으로 맞춘다. `check_only` 면 고치지 않고 다른 파일만 알린다.

    명령에 검사 방식이 없어 사본을 고쳐 견준다(한 번 띄워 모든 파일을 본다). vcpkg 실행 파일이 없으면 오류다 — 건너뛰면 어긋난 매니페스트가 통과한다.
    `vcpkg_command` 는 `format-manifest` 앞의 명령이다(없으면 `setup.vcpkg` 가 찾는다).
    """
    if not files:
        return ExitCode.PASSED
    if vcpkg_command is None:
        from setup.vcpkg import find_vcpkg_executable  # 매니페스트를 볼 때만
        executable = find_vcpkg_executable()
        if executable is None:
            print("[FormatVcpkgManifest] vcpkg 실행 파일을 찾지 못했습니다 — 'py -3 -m Scripts vcpkg --install' 로 설치하십시오.", file=sys.stderr)
            return ExitCode.VIOLATION
        vcpkg_command = [executable]

    has_difference = False
    with tempfile.TemporaryDirectory() as folder:
        map_copy: dict[Path, Path] = {}
        for index, path in enumerate(files):
            copy = Path(folder) / str(index) / VCPKG_MANIFEST_NAME
            copy.parent.mkdir()
            copy.write_bytes(path.read_bytes())
            map_copy[path] = copy
        result = run_process([*vcpkg_command, "format-manifest", *map_copy.values()])
        if not result.succeeded:
            names = ", ".join(str(path) for path in files)
            print(f"[FormatVcpkgManifest] vcpkg format-manifest 실패({names}): {result.stderr.strip() or result.stdout.strip()}", file=sys.stderr)
            return ExitCode.VIOLATION
        for path, copy in map_copy.items():
            formatted = copy.read_bytes()
            if formatted == path.read_bytes():
                continue
            has_difference = True
            if check_only:
                print(f"[FormatVcpkgManifest] {path} 가 vcpkg format-manifest 모양과 다릅니다.", file=sys.stderr)
            else:
                path.write_bytes(formatted)
    return ExitCode.VIOLATION if has_difference and check_only else ExitCode.PASSED
