"""
Scripts/common/AssetPipeline.py

SW Engine 에셋 쿠킹 및 바이너리 직렬화 공통 파이프라인 모듈:
  - writeBinaryIfChanged: 바이트 내용이 변경되었을 때만 원자적으로 파일 쓰기 (불필요한 타임스탬프 갱신 방지)
  - packLengthPrefixedBytes / packLengthPrefixedString: 32비트 길이 접두어 바이너리 패킹
  - batchCookAssets: 멀티스레드 에셋 병렬 쿠킹 및 통계 로깅
  - resolveDefaultOutputDir: 가장 최근에 빌드된 산출물 디렉터리(build/*/Bin) 동적 탐색
"""

from __future__ import annotations

import concurrent.futures
import os
import struct
import sys
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Any


def writeBinaryIfChanged(path: Path, data: bytes) -> bool:
    """
    바이트 데이터가 기존 파일의 내용과 다를 때만 파일에 기록합니다.

    내용이 동일하면 디스크 쓰기를 생략하여 파일 수정 시간(mtime)을 보존함으로써
    불필요한 다운스트림 빌드 트리거를 방지합니다.

    Args:
        path: 기록할 대상 파일 경로.
        data: 기록할 원시 바이트 데이터.

    Returns:
        파일이 새로 작성되거나 내용이 변경되었으면 True, 기존 내용과 완전히 동일하여 건너뛰었으면 False.
    """
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def packLengthPrefixedBytes(data: bytes) -> bytes:
    """32비트 리틀 엔디안 부호 없는 정수 길이 접두어와 바이트 배열을 패킹합니다."""
    return struct.pack("<I", len(data)) + data


def packLengthPrefixedString(text: str) -> bytes:
    """UTF-8 인코딩된 문자열을 32비트 길이 접두어와 함께 패킹합니다."""
    return packLengthPrefixedBytes(text.encode("utf-8"))


def batchCookAssets(
    tasks: Sequence[Any],
    cookFunc: Callable[[Any], bool | int],
    label: str = "CookAssets",
    maxWorkers: int | None = None,
) -> tuple[int, int]:
    """
    에셋 쿠킹 작업 리스트를 멀티스레드로 병렬 실행하고 완료 통계를 집계하여 출력합니다.

    Args:
        tasks: 쿠킹 함수에 전달할 작업 인자들의 시퀀스.
        cookFunc: 단일 작업 항목을 처리하는 콜백 함수. (파일 갱신 시 True/1, 건너뜀 시 False/0 반환)
        label: 콘솔 출력에 사용할 작업 레이블 (예: "CookPrefabs", "CookScenes").
        maxWorkers: 스레드 풀 최대 워커 수 (기본값: CPU 코어 기반 자동 계산).

    Returns:
        (전체 검사된 파일 수, 실제 갱신된 파일 수) 튜플.
    """
    totalCount = len(tasks)
    if totalCount == 0:
        return 0, 0

    workers = maxWorkers or min(32, (os.cpu_count() or 4) * 2)
    updatedCount = 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(cookFunc, task) for task in tasks]
        for future in concurrent.futures.as_completed(futures):
            try:
                result = future.result()
                if result:
                    updatedCount += int(result)
            except Exception as exception:
                print(f"[{label} Error] {exception}", file=sys.stderr)

    print(f"[{label}] Done - {totalCount} item(s) checked, {updatedCount} updated.")
    return totalCount, updatedCount


def resolveDefaultOutputDir(projectRoot: Path, subDir: str = "Packs") -> Path:
    """
    프로젝트의 build/ 디렉터리 하위에서 가장 최근에 구성/빌드된 산출물 경로(build/*/Bin/<subDir>)를 동적으로 탐색합니다.

    Args:
        projectRoot: 프로젝트 루트 경로.
        subDir: Bin 하위의 출력 서브디렉터리 명 (예: "Packs").

    Returns:
        탐색된 대상 출력 디렉터리 Path.
    """
    buildDir = projectRoot / "build"
    if buildDir.is_dir():
        candidates: list[tuple[float, Path]] = []
        for child in buildDir.iterdir():
            if not child.is_dir():
                continue
            binDir = child / "Bin"
            if binDir.is_dir():
                mtime = 0.0
                for marker in ("build.ninja", "CMakeCache.txt", "compile_commands.json"):
                    markerPath = child / marker
                    if markerPath.is_file():
                        mtime = max(mtime, markerPath.stat().st_mtime)
                if mtime == 0.0:
                    mtime = binDir.stat().st_mtime
                target = (binDir / subDir) if subDir else binDir
                candidates.append((mtime, target))

        if candidates:
            candidates.sort(key=lambda item: item[0], reverse=True)
            return candidates[0][1]

    print(f"[Error] Could not find a valid build output directory (build/*/Bin/{subDir}). Please specify output explicitly.", file=sys.stderr)
    sys.exit(1)
