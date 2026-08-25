"""
Scripts/common/Archive.py

네트워크 다운로드, SHA-256 해시 검증, Zip/Tar 안전 압축 해제(Zip Slip 방지) 및 캐시 유틸리티.
"""

from __future__ import annotations

import hashlib
import shutil
import tarfile
import urllib.request
import zipfile
from pathlib import Path
from typing import Any

from .Config import loadSearchPaths
from .Constants import kDirToolsCache
from .Paths import getProjectRoot


def toolsCacheDir() -> Path:
    """
    외부 도구 다운로드 아카이브를 보관할 캐시 디렉터리(Tools/_cache) 경로를 반환합니다.
    """
    cacheDirectory = getProjectRoot() / kDirToolsCache
    cacheDirectory.mkdir(parents=True, exist_ok=True)
    return cacheDirectory


def resolveToolsSubdir(subdirKey: str,
                       default: str,
                       search: dict[str, Any] | None = None) -> Path:
    """
    search_paths.json에서 도구 설치 하위 경로(예: Tools/Ninja)를 읽어 프로젝트 루트 기준 절대 경로로 반환합니다.
    """
    searchConfig = search if search is not None else loadSearchPaths()
    subdir = searchConfig.get(subdirKey, default)
    return (getProjectRoot() / str(subdir)).resolve()


def fileSha256(path: Path) -> str:
    """
    파일의 무결성 검증을 위해 SHA-256 해시값을 계산하여 소문자 16진수 문자열로 반환합니다.
    (Python 3.11+ hashlib.file_digest 사용)

    Args:
        path: 해시를 계산할 파일 경로

    Returns:
        64자리 16진수 SHA-256 해시 문자열
    """
    with path.open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()


def downloadUrl(url: str, destPath: Path, *, label: str = "Download") -> None:
    """
    지정된 URL에서 파일을 다운로드하여 대상 경로에 저장하고 진행률(%)을 콘솔에 표시합니다.
    """
    destPath.parent.mkdir(parents=True, exist_ok=True)
    print(f"[{label}] Downloading {url}")
    lastPercent = -1

    def hookInternal(blockNumber: int, blockSize: int, totalSize: int) -> None:
        nonlocal lastPercent
        if totalSize <= 0:
            return
        downloadedBytes = blockNumber * blockSize
        percent = min(100, int(downloadedBytes * 100 / totalSize))
        if percent >= lastPercent + 5 or percent == 100:
            lastPercent = percent
            print(
                f"[{label}]   {percent}% "
                f"({downloadedBytes // (1024 * 1024)} / {totalSize // (1024 * 1024)} MiB)"
            )

    urllib.request.urlretrieve(url, destPath, reporthook=hookInternal)


def isSafeArchiveMember(name: str, destRoot: Path) -> bool:
    """
    압축 해제 시 상위 디렉터리 탈출 공격(Zip Slip)을 방지하기 위해 파일 경로가 대상 폴더 내부인지 검사합니다.

    Args:
        name: 아카이브 내부 항목 이름
        destRoot: 압축 해제 대상 루트 디렉터리 경로

    Returns:
        안전한 경로이면 True, 상위 경로 탈출 시도시 False
    """
    normalizedName = name.replace("\\", "/")
    if normalizedName.startswith("/") or ".." in Path(normalizedName).parts:
        return False
    try:
        target = (destRoot / normalizedName).resolve()
        destRoot = destRoot.resolve()
        return destRoot == target or destRoot in target.parents
    except OSError:
        return False


def extractZipSafe(archivePath: Path, destPath: Path) -> None:
    """
    Zip 아카이브를 대상 디렉터리에 안전하게 압축 해제합니다 (상위 경로 탈출 보안 검사 포함).

    Args:
        archivePath: 해제할 Zip 파일 경로
        destPath: 저장할 대상 디렉터리 경로
    """
    destPath.mkdir(parents=True, exist_ok=True)
    destResolved = destPath.resolve()
    with zipfile.ZipFile(archivePath, "r") as zipHandle:
        for info in zipHandle.infolist():
            if not isSafeArchiveMember(info.filename, destResolved):
                continue
            zipHandle.extract(info, destResolved)


def extractTarSafe(archivePath: Path, destPath: Path, *, mode: str = "r:*") -> None:
    """
    Tar 아카이브(.tar.gz, .tar.xz 등)를 대상 디렉터리에 안전하게 압축 해제합니다.

    Args:
        archivePath: 해제할 Tar 파일 경로
        destPath: 저장할 대상 디렉터리 경로
        mode: tarfile 모드 (기본값: "r:*")
    """
    destPath.mkdir(parents=True, exist_ok=True)
    destResolved = destPath.resolve()
    with tarfile.open(archivePath, mode) as tarHandle:
        members = [
            member
            for member in tarHandle.getmembers()
            if isSafeArchiveMember(member.name, destResolved)
        ]
        tarHandle.extractall(path=destResolved, members=members)


def ensureCachedDownload(url: str,
                         destPath: Path,
                         *,
                         minSize: int = 10_000,
                         label: str = "Download",
                         sha256: str | None = None) -> Path:
    """
    외부 도구를 다운로드하고, 사이드카 .sha256 해시를 검증하여 이미 다운로드된 캐시가 있으면 재다운로드를 생략합니다.

    Args:
        url: 다운로드할 파일 URL
        destPath: 저장할 로컬 파일 경로
        minSize: 정상 파일로 인정할 최소 크기 (바이트 단위)
        label: 콘솔 로그 출력용 태그 이름
        sha256: 기대되는 SHA-256 해시값 (지정 시 엄격하게 검증)

    Returns:
        다운로드되었거나 캐시에서 재사용된 파일 경로
    """
    sidecarFile = destPath.with_name(destPath.name + ".sha256")
    expectedHash = (sha256 or "").strip().lower()

    def writeSidecarInternal(digest: str) -> None:
        sidecarFile.write_text(digest + "\n", encoding="utf-8")

    def cacheValidInternal() -> bool:
        if not destPath.is_file() or destPath.stat().st_size < minSize:
            return False
        digest = fileSha256(destPath)
        if expectedHash:
            if digest == expectedHash:
                return True
            print(f"[{label}] SHA256 mismatch for cache {destPath}, re-downloading")
            return False
        if sidecarFile.is_file():
            recorded = sidecarFile.read_text(encoding="utf-8").strip().split()[0].lower()
            if digest == recorded:
                return True
            print(f"[{label}] SHA256 sidecar mismatch for {destPath}, re-downloading")
            return False
        writeSidecarInternal(digest)
        return True

    if cacheValidInternal():
        print(f"[{label}] Reusing cached: {destPath}")
        return destPath

    downloadUrl(url, destPath, label=label)
    digest = fileSha256(destPath)
    if destPath.stat().st_size < minSize:
        destPath.unlink(missing_ok=True)
        raise RuntimeError(f"[{label}] Download too small: {destPath}")
    if expectedHash and digest != expectedHash:
        destPath.unlink(missing_ok=True)
        raise RuntimeError(f"[{label}] SHA256 mismatch for {destPath}")
    writeSidecarInternal(expectedHash or digest)
    return destPath


def wipeDirContents(path: Path) -> None:
    """
    지정된 디렉터리 내부의 모든 파일과 하위 폴더를 깨끗하게 삭제합니다.
    """
    if not path.is_dir():
        return
    for child in path.iterdir():
        if child.is_dir():
            shutil.rmtree(child, ignore_errors=True)
        else:
            child.unlink(missing_ok=True)
