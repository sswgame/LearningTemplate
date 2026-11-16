"""
Scripts/common/tool_download.py

네트워크 다운로드, SHA-256 해시 검증, Zip/Tar 안전 압축 해제(Zip Slip 방지) 및 캐시 유틸리티.
"""

from __future__ import annotations

import hashlib
from pathlib import Path
from typing import Any

from .toolchain_config import load_search_paths
from .project_constants import DIR_TOOLS_CACHE
from .paths import get_project_root

#: 해시를 계산할 때 한 번에 읽는 크기입니다. 수백 MB 압축 파일도 메모리에 통째로 올리지 않습니다.
_HASH_CHUNK_BYTES = 1 << 20


def tools_cache_dir() -> Path:
    """
    외부 도구 다운로드 아카이브를 보관할 캐시 디렉터리(Tools/_cache) 경로를 반환합니다.
    """
    cache_directory = get_project_root() / DIR_TOOLS_CACHE
    cache_directory.mkdir(parents=True, exist_ok=True)
    return cache_directory


def resolve_tools_subdir(subdir_key: str,
                       search: dict[str, Any] | None = None) -> Path:
    """
    search_paths.json에서 도구 설치 하위 경로(예: Tools/Ninja)를 읽어 프로젝트 루트 기준 절대 경로로 반환합니다.
    설정 파일에 해당 키가 없으면 KeyError를 발생시킵니다.
    """
    search_config = search if search is not None else load_search_paths()
    subdir = search_config.get(subdir_key)
    if not subdir:
        raise KeyError(f"[Config Error] Missing required key '{subdir_key}' in search_paths config")
    return (get_project_root() / str(subdir)).resolve()


def file_sha256(path: Path) -> str:
    """
    파일의 무결성 검증을 위해 SHA-256 해시값을 계산하여 소문자 16진수 문자열로 반환합니다.
    `hashlib.file_digest` 는 3.11 에 생겨 최소 버전(`MINIMUM_PYTHON_VERSION`)에서 쓸 수 없으므로, 청크로 읽어 갱신합니다.

    Args:
        path: 해시를 계산할 파일 경로

    Returns:
        64자리 16진수 SHA-256 해시 문자열
    """
    digest = hashlib.new("sha256")
    with path.open("rb") as handle:
        while chunk := handle.read(_HASH_CHUNK_BYTES):
            digest.update(chunk)
    return digest.hexdigest()


def download_url(url: str, dest_path: Path, *, label: str = "Download") -> None:
    """
    지정된 URL에서 파일을 다운로드하여 대상 경로에 저장하고 진행률(%)을 콘솔에 표시합니다.
    """
    import urllib.request  # 받을 때만 — 모듈 머리에 두면 상수 하나 읽는 스크립트도 http · email 까지 불러온다(45 ms)
    dest_path.parent.mkdir(parents=True, exist_ok=True)
    print(f"[{label}] Downloading {url}")
    last_percent = -1

    def _hook(block_number: int, block_size: int, total_size: int) -> None:
        nonlocal last_percent
        if total_size <= 0:
            return
        downloaded_bytes = block_number * block_size
        percent = min(100, int(downloaded_bytes * 100 / total_size))
        if percent >= last_percent + 5 or percent == 100:
            last_percent = percent
            print(
                f"[{label}]   {percent}% "
                f"({downloaded_bytes // (1024 * 1024)} / {total_size // (1024 * 1024)} MiB)"
            )

    urllib.request.urlretrieve(url, dest_path, reporthook=_hook)


def is_safe_archive_member(name: str, dest_root: Path) -> bool:
    """
    압축 해제 시 상위 디렉터리 탈출 공격(Zip Slip)을 방지하기 위해 파일 경로가 대상 폴더 내부인지 검사합니다.

    Args:
        name: 아카이브 내부 항목 이름
        dest_root: 압축 해제 대상 루트 디렉터리 경로

    Returns:
        안전한 경로이면 True, 상위 경로 탈출 시도시 False
    """
    normalized_name = name.replace("\\", "/")
    if normalized_name.startswith("/") or ".." in Path(normalized_name).parts:
        return False
    try:
        target = (dest_root / normalized_name).resolve()
        dest_root = dest_root.resolve()
        return dest_root == target or dest_root in target.parents
    except OSError:
        return False


def extract_zip_safe(archive_path: Path, dest_path: Path) -> None:
    """
    Zip 아카이브를 대상 디렉터리에 안전하게 압축 해제합니다 (상위 경로 탈출 보안 검사 포함).

    Args:
        archive_path: 해제할 Zip 파일 경로
        destPath: 저장할 대상 디렉터리 경로
    """
    import zipfile  # 풀 때만
    dest_path.mkdir(parents=True, exist_ok=True)
    dest_resolved = dest_path.resolve()
    with zipfile.ZipFile(archive_path, "r") as zip_handle:
        for info in zip_handle.infolist():
            if not is_safe_archive_member(info.filename, dest_resolved):
                continue
            zip_handle.extract(info, dest_resolved)


def ensure_cached_download(url: str,
                         dest_path: Path,
                         *,
                         min_size: int = 10_000,
                         label: str = "Download") -> Path:
    """
    외부 도구를 다운로드하고, 사이드카 .sha256 해시를 검증하여 이미 다운로드된 캐시가 있으면 재다운로드를 생략합니다.

    Args:
        url: 다운로드할 파일 URL
        destPath: 저장할 로컬 파일 경로
        minSize: 정상 파일로 인정할 최소 크기 (바이트 단위)
        label: 콘솔 로그 출력용 태그 이름

    Returns:
        다운로드되었거나 캐시에서 재사용된 파일 경로
    """
    sidecar_file = dest_path.with_name(dest_path.name + ".sha256")

    def _write_sidecar(digest: str) -> None:
        sidecar_file.write_text(digest + "\n", encoding="utf-8")

    def _cache_valid() -> bool:
        if not dest_path.is_file() or dest_path.stat().st_size < min_size:
            return False
        digest = file_sha256(dest_path)
        if sidecar_file.is_file():
            recorded = sidecar_file.read_text(encoding="utf-8").strip().split()[0].lower()
            if digest == recorded:
                return True
            print(f"[{label}] SHA256 sidecar mismatch for {dest_path}, re-downloading")
            return False
        _write_sidecar(digest)
        return True

    if _cache_valid():
        print(f"[{label}] Reusing cached: {dest_path}")
        return dest_path

    download_url(url, dest_path, label=label)
    digest = file_sha256(dest_path)
    if dest_path.stat().st_size < min_size:
        dest_path.unlink(missing_ok=True)
        raise RuntimeError(f"[{label}] Download too small: {dest_path}")
    _write_sidecar(digest)
    return dest_path
