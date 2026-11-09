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

from common.HostPlatform import isWindowsHost

kLockPollSeconds = 0.05
kLockTimeoutSeconds = 300.0
kReplaceTimeoutSeconds = 10.0  # Windows 는 대상을 연 프로세스(잠금 없이 읽는 쪽 · 백신 · 색인기)가 있으면 이름 바꾸기를 막는다 — 곧 풀리는 공유 위반이다


@contextlib.contextmanager
def lockSharedFile(path: Path) -> Iterator[None]:
    """@p path 의 갱신 잠금을 쥔다. 다른 프로세스가 쥐고 있으면 풀릴 때까지 기다리고, `kLockTimeoutSeconds` 를 넘으면 오류로 멈춘다(잠금을 건너뛰지 않는다)."""
    key = hashlib.sha1(str(Path(path).resolve()).lower().encode("utf-8")).hexdigest()[:16]
    lockPath = Path(tempfile.gettempdir()) / f"sw-shared-{key}.lock"
    with open(lockPath, "a+b") as lockFile:
        deadline = time.monotonic() + kLockTimeoutSeconds
        while not tryLockFile(lockFile):
            if time.monotonic() > deadline:
                raise TimeoutError(f"{path}: could not take the update lock {lockPath} within {kLockTimeoutSeconds:.0f} s")
            time.sleep(kLockPollSeconds)
        try:
            yield
        finally:
            unlockFile(lockFile)


def tryLockFile(lockFile) -> bool:
    """열린 잠금 파일의 첫 바이트를 배타로 잠근다. 다른 프로세스가 쥐고 있으면 False."""
    if isWindowsHost():
        import msvcrt
        lockFile.seek(0)
        try:
            msvcrt.locking(lockFile.fileno(), msvcrt.LK_NBLCK, 1)
            return True
        except OSError:
            return False
    import fcntl
    try:
        fcntl.flock(lockFile.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        return True
    except OSError:
        return False


def unlockFile(lockFile) -> None:
    if isWindowsHost():
        import msvcrt
        lockFile.seek(0)
        msvcrt.locking(lockFile.fileno(), msvcrt.LK_UNLCK, 1)
        return
    import fcntl
    fcntl.flock(lockFile.fileno(), fcntl.LOCK_UN)


def replaceTextAtomically(path: Path, text: str) -> None:
    """@p text 를 같은 폴더의 임시 파일에 LF 로 다 쓰고 @p path 로 바꾼다(반쯤 쓴 파일이 보이지 않는다)."""
    path = Path(path)
    descriptor, temporaryName = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as file:
            file.write(text)
        replaceWhenReleased(temporaryName, path)
    except BaseException:
        with contextlib.suppress(OSError):
            os.remove(temporaryName)
        raise


def replaceWhenReleased(source: str, target: Path) -> None:
    """`os.replace` 를 하되, Windows 의 공유 위반(PermissionError)은 대상을 연 쪽이 닫을 때까지 `kReplaceTimeoutSeconds` 안에서 다시 한다.
    시한을 넘으면 그 오류를 그대로 낸다(쓰기를 건너뛰지 않는다). 언리얼 `IFileManager::Move` 도 공유 위반을 재시도한다."""
    deadline = time.monotonic() + kReplaceTimeoutSeconds
    while True:
        try:
            os.replace(source, target)
            return
        except PermissionError:
            if isWindowsHost() is False or time.monotonic() > deadline:
                raise
            time.sleep(kLockPollSeconds)


def updateSharedTextFile(path: Path, update: Callable[[str], str]) -> bool:
    """
    잠금 안에서 @p path 를 읽어 @p update(지금 글) 로 새 글을 만들고, 바뀌었을 때만 원자 교체로 쓴다. 썼으면 True.
    다른 프로세스의 갱신 사이에 읽고 쓰므로 서로 다른 칸을 고친 둘이 서로의 갱신을 잃지 않는다.
    """
    path = Path(path)
    with lockSharedFile(path):
        current = path.read_text(encoding="utf-8")
        updated = update(current)
        if updated == current:
            return False
        replaceTextAtomically(path, updated)
        return True
