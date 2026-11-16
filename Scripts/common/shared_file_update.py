"""
여러 프로세스가 함께 갱신하는 파일(빌드 폴더마다 도는 생성기가 같은 저장소 파일을 고친다)의 읽기-고치기-쓰기 한 벌.

- **잠금**: 갱신 전체(읽기 → 합치기 → 쓰기)를 프로세스 사이 배타 잠금 하나로 묶는다. 잠금 파일은 OS 임시 폴더에 대상 경로의 해시로 둔다 —
  저장소를 더럽히지 않고, 같은 대상이면 어느 빌드 폴더에서 불러도 같은 잠금이다. Windows 는 `msvcrt.locking`, 그 밖은 `fcntl.flock` 이다.
- **원자 교체**: 같은 폴더의 임시 파일에 다 쓴 뒤 `os.replace` 로 바꾼다. 읽는 쪽(린트 · `move-asset`)은 반쯤 쓴 파일을 보지 않는다.

언리얼 UBT 가 공유 매니페스트를 뮤텍스로 감싸고 임시 파일 → 이름 바꾸기로 쓰는 것과 같은 자리다(기억에 의존한 비교).
"""

from __future__ import annotations

import contextlib
import hashlib
import os
import tempfile
import time
from pathlib import Path
from typing import Callable, Iterator

from common.host_platform import is_windows_host

LOCK_POLL_SECONDS = 0.05
LOCK_TIMEOUT_SECONDS = 300.0
REPLACE_TIMEOUT_SECONDS = 10.0  # Windows 는 대상을 연 프로세스(잠금 없이 읽는 쪽 · 백신 · 색인기)가 있으면 이름 바꾸기를 막는다 — 곧 풀리는 공유 위반이다


@contextlib.contextmanager
def lock_shared_file(path: Path) -> Iterator[None]:
    """@p path 의 갱신 잠금을 쥔다. 다른 프로세스가 쥐고 있으면 풀릴 때까지 기다리고, `LOCK_TIMEOUT_SECONDS` 를 넘으면 오류로 멈춘다(잠금을 건너뛰지 않는다)."""
    key = hashlib.sha1(str(Path(path).resolve()).lower().encode("utf-8")).hexdigest()[:16]
    lock_path = Path(tempfile.gettempdir()) / f"sw-shared-{key}.lock"
    with open(lock_path, "a+b") as lock_file:
        deadline = time.monotonic() + LOCK_TIMEOUT_SECONDS
        while not try_lock_file(lock_file):
            if time.monotonic() > deadline:
                raise TimeoutError(f"{path}: could not take the update lock {lock_path} within {LOCK_TIMEOUT_SECONDS:.0f} s")
            time.sleep(LOCK_POLL_SECONDS)
        try:
            yield
        finally:
            unlock_file(lock_file)


def try_lock_file(lock_file) -> bool:
    """열린 잠금 파일의 첫 바이트를 배타로 잠근다. 다른 프로세스가 쥐고 있으면 False."""
    if is_windows_host():
        import msvcrt
        lock_file.seek(0)
        try:
            msvcrt.locking(lock_file.fileno(), msvcrt.LK_NBLCK, 1)
            return True
        except OSError:
            return False
    import fcntl
    try:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        return True
    except OSError:
        return False


def unlock_file(lock_file) -> None:
    if is_windows_host():
        import msvcrt
        lock_file.seek(0)
        msvcrt.locking(lock_file.fileno(), msvcrt.LK_UNLCK, 1)
        return
    import fcntl
    fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)


def replace_text_atomically(path: Path, text: str) -> None:
    """@p text 를 같은 폴더의 임시 파일에 LF 로 다 쓰고 @p path 로 바꾼다(반쯤 쓴 파일이 보이지 않는다)."""
    path = Path(path)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as file:
            file.write(text)
        replace_when_released(temporary_name, path)
    except BaseException:
        with contextlib.suppress(OSError):
            os.remove(temporary_name)
        raise


def replace_when_released(source: str, target: Path) -> None:
    """`os.replace` 를 하되, Windows 의 공유 위반(PermissionError)은 대상을 연 쪽이 닫을 때까지 `REPLACE_TIMEOUT_SECONDS` 안에서 다시 한다.
    시한을 넘으면 그 오류를 그대로 낸다(쓰기를 건너뛰지 않는다). 언리얼 `IFileManager::Move` 도 공유 위반을 재시도한다."""
    deadline = time.monotonic() + REPLACE_TIMEOUT_SECONDS
    while True:
        try:
            os.replace(source, target)
            return
        except PermissionError:
            if is_windows_host() is False or time.monotonic() > deadline:
                raise
            time.sleep(LOCK_POLL_SECONDS)


def update_shared_text_file(path: Path, update: Callable[[str], str]) -> bool:
    """
    잠금 안에서 @p path 를 읽어 @p update(지금 글) 로 새 글을 만들고, 바뀌었을 때만 원자 교체로 쓴다. 썼으면 True.
    다른 프로세스의 갱신 사이에 읽고 쓰므로 서로 다른 칸을 고친 둘이 서로의 갱신을 잃지 않는다.
    """
    path = Path(path)
    with lock_shared_file(path):
        current = path.read_text(encoding="utf-8")
        updated = update(current)
        if updated == current:
            return False
        replace_text_atomically(path, updated)
        return True
