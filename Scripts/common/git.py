"""
Scripts/common/git.py

git 실행 파일 탐색과 git 이 알려 주는 파일 목록 — setup(vcpkg 고정)과 lint(staged 파일)가 함께 쓴다.
"""

from __future__ import annotations

import contextlib
import os
import shutil
import subprocess
from pathlib import Path
from typing import Iterator, Sequence

from . import HOOK_GIT_ENVIRONMENT
from .host_platform import is_windows_host
from .run_process import ProcessResult, run_process


def resolve_git_executable() -> str | None:
    """
    시스템 PATH 및 Windows 기본 설치 폴더(Program Files, LocalAppData 등)에서 git 실행 파일(git.exe)을 찾습니다.
    (CMake Tools나 IDE 터미널 등 PATH 환경변수가 축소된 환경에서도 Git for Windows를 안정적으로 찾아냅니다.)
    """
    if found_path := shutil.which("git"):
        return found_path

    if is_windows_host():
        # 설치 폴더는 환경 변수가 정본이다 — 경로 리터럴로 대신하면 다른 드라이브에 깐 PC 에서 엉뚱한 곳을 본다.
        # ProgramFiles 는 Windows 가 늘 정의하므로 없으면 환경이 깨진 것이다. 나머지 둘은 없는 PC 가 있다(32비트 · 사용자 설치 안 함).
        if "ProgramFiles" not in os.environ:
            raise RuntimeError("[Git] 환경 변수 ProgramFiles 가 없어 Git 설치 폴더를 찾을 수 없습니다")
        list_install_root = [Path(os.environ["ProgramFiles"])]
        if program_files_x86 := os.environ.get("ProgramFiles(x86)"):
            list_install_root.append(Path(program_files_x86))
        if local_app_data := os.environ.get("LocalAppData"):
            list_install_root.append(Path(local_app_data) / "Programs")
        candidate_list = [root / "Git" / folder / "git.exe" for root in list_install_root for folder in ("cmd", "bin")]
        return next((str(cand.resolve()) for cand in candidate_list if cand.is_file()), None)
    return None


def ensure_git_on_path() -> str | None:
    """
    git 실행 파일을 찾아 프로세스 환경변수(PATH)에 등록하고 실행 파일 경로를 반환합니다.
    (git을 찾지 못한 경우 None을 반환합니다.)
    """
    git_path = resolve_git_executable()
    if git_path is None:
        return None

    git_dir = str(Path(git_path).parent)
    path_parts = os.environ["PATH"].split(os.pathsep)
    path_lower = {part.lower() for part in path_parts if part}
    if git_dir.lower() not in path_lower:
        os.environ["PATH"] = git_dir + os.pathsep + os.environ["PATH"]
    return git_path


def make_hook_environment() -> dict[str, str]:
    """커밋 훅이 넘긴 저장소 환경 변수(`common.HOOK_GIT_ENVIRONMENT` — 프로세스 시작에 떼어 둔 것)를 되돌린 환경."""
    return {**os.environ, **HOOK_GIT_ENVIRONMENT}


@contextlib.contextmanager
def outside_hook_repository() -> Iterator[None]:
    """`run_git_in_hook_repository` 가 커밋 훅의 저장소 변수를 되돌려 쓰지 않는 구간 — 임시 저장소를 훅 입구(`make_staged_scope`)로 읽는 자체 검사가 쓴다."""
    saved = dict(HOOK_GIT_ENVIRONMENT)
    HOOK_GIT_ENVIRONMENT.clear()
    try:
        yield
    finally:
        HOOK_GIT_ENVIRONMENT.update(saved)


def run_git(args: Sequence[str], *, cwd: Path | None = None) -> ProcessResult:
    """
    Git 명령어를 실행하고 결과를 반환합니다(출력은 UTF-8 — 한글 경로 · 커밋 메시지가 시스템 코드 페이지로 깨지지 않게).

    저장소는 `cwd`(없으면 이 프로세스의 폴더)가 고른다 — 물려받은 `GIT_DIR` · `GIT_INDEX_FILE` · `GIT_WORK_TREE` 등은 `common/__init__.py` 가 프로세스
    시작에 떼어 두었다. Scripts 는 git 을 이 입구(또는 `run_git_in_hook_repository` · `run_git_bytes`)로만 부른다 — platform-chains 가 subprocess 로 직접 부르는 것을 막는다.
    """
    git_exe = resolve_git_executable() or "git"
    return run_process([git_exe, *args], cwd=cwd)


def run_git_or_raise(args: Sequence[str], *, cwd: Path | None = None) -> str:
    """`run_git` 의 표준 출력 — 지면 멈춘다(`subprocess.check_output` 을 대신한다)."""
    result = run_git(args, cwd=cwd)
    if result.return_code != 0:
        raise RuntimeError(f"git {' '.join(args)} 실패: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def get_main_working_tree(project_root: Path) -> Path:
    """메인 작업 폴더(공통 `.git` 의 부모) — 워크트리에서도 빌드 · 검증 기록(`build/`)은 이 폴더 하나에 있다."""
    return Path(run_git_or_raise(["rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=project_root).strip()).parent


def run_git_in_hook_repository(args: Sequence[str], *, cwd: Path) -> ProcessResult:
    """
    물려받은 git 환경 변수를 **그대로 둔** git — 커밋 훅 린트가 지금 커밋하는 인덱스를 볼 때만 쓴다. `git commit -a` · `git commit <경로>` 는 임시 인덱스를
    `GIT_INDEX_FILE` 로 넘기므로, 빼면 스테이지 목록 · 블롭이 커밋 내용과 어긋난다. 훅 밖에서는 `run_git` 과 같다.
    """
    git_exe = resolve_git_executable() or "git"
    return run_process([git_exe, *args], cwd=cwd, environment=make_hook_environment())


def run_git_bytes(args: Sequence[str], *, cwd: Path, input_bytes: bytes, hook_repository: bool) -> tuple[int, bytes, str]:
    """표준 입력을 넘기고 바이트 출력을 받는 git(`cat-file --batch`) — (종료 코드, 출력, 오류 글). 환경은 `hook_repository` 가 고른다(위 두 입구와 같은 뜻)."""
    completed = subprocess.run([resolve_git_executable() or "git", *args], input=input_bytes, cwd=cwd, capture_output=True, check=False,
                               env=make_hook_environment() if hook_repository else None)
    return completed.returncode, completed.stdout, completed.stderr.decode("utf-8", "replace")


def list_git_files(project_root: Path, list_git_argument: list[str], extensions: set[str] | None) -> set[Path]:
    """
    git 명령이 한 줄에 하나씩 찍는 저장소 기준 경로를 절대 경로로 풀어 모읍니다. 지금 있는 파일만, `extensions` 가 있으면 그 확장자만.

    staged 전체 · staged C++ · 수정된 C++ · untracked C++ 네 곳이 이 여덟 줄을 각자 적고 있었습니다.
    """
    file_set: set[Path] = set()
    git_result = run_git(list_git_argument, cwd=project_root)
    if git_result.return_code != 0:
        # 목록을 못 얻으면 "바뀐 파일 없음" 이 아니다 — 빈 목록이면 커밋 훅이 검사 없이 통과한다.
        raise RuntimeError(f"git {' '.join(list_git_argument)} 실패: {git_result.stderr.strip()}")
    for raw_line in git_result.stdout.splitlines():
        if line := raw_line.strip():
            resolved_path = (project_root / line).resolve()
            if resolved_path.is_file() and (extensions is None or resolved_path.suffix.lower() in extensions):
                file_set.add(resolved_path)
    return file_set
