#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CookResourcePacks.py

SW Engine용 리소스 패키징 도구 (.pack / SWPK 포맷)
Resource/ 폴더 내 에셋들을 4KB 섹터 정렬, CRC32 체크섬, 64비트 해시 인덱스를 포함한 정규 바이너리 아카이브로 빌드합니다.
"""

from __future__ import annotations

import argparse
import binascii
from pathlib import Path
import struct
import sys
import zlib

_kPackMagic = 0x4B505753  # 'SWPK'
_kPackFormatVersion = 1
_kPackSectorAlignment = 4096

_kFlagNone = 0x0000
_kFlagHasStringPool = 0x0001
_kFlagHasCrc32 = 0x0002
_kFlagEncrypted = 0x0004

_kCompressionNone = 0
_kCompressionRle = 1
_kCompressionZlib = 2
_kCompressionLz4 = 3


def fnv1a64Internal(path: str) -> int:
    """FNV-1a 64비트 해시 계산 (소문자 및 슬래시 정규화 적용)"""
    fnv_prime = 1099511628211
    fnv_offset = 14695981039346656037

    normalized = path.replace("\\", "/").lower()
    h = fnv_offset
    for ch in normalized.encode("utf-8"):
        h ^= ch
        h = (h * fnv_prime) & 0xFFFFFFFFFFFFFFFF
    return h


def alignOffsetInternal(offset: int, alignment: int = _kPackSectorAlignment) -> int:
    """오프셋을 섹터 정렬 경계로 올림"""
    mask = alignment - 1
    return (offset + mask) & ~mask


def cookPack(
    sourceDir: Path,
    outPackPath: Path,
    dlcAppId: int = 0,
    compression: int = _kCompressionZlib,
    stripDebugStrings: bool = True,
) -> bool:
    """단일 디렉터리 내 에셋들을 .pack 파일로 패킹합니다."""
    if not sourceDir.is_dir():
        print(f"[Error] Source directory does not exist: {sourceDir}", file=sys.stderr)
        return False

    outPackPath.parent.mkdir(parents=True, exist_ok=True)

    # 1. 파일 목록 수집 (숨김 파일, .meta 파일 등 제외 여부 결정)
    collectedFiles: list[tuple[str, Path]] = []
    for p in sorted(sourceDir.rglob("*")):
        if p.is_file():
            # 소문자 가상 상대 경로 생성
            rel = p.relative_to(sourceDir).as_posix().lower()
            collectedFiles.append((rel, p))

    if not collectedFiles:
        print(f"[Warning] No files found in: {sourceDir}", file=sys.stderr)

    fileCount = len(collectedFiles)
    flags = _kFlagHasCrc32
    if not stripDebugStrings:
        flags |= _kFlagHasStringPool

    # 2. 페이로드 블록 생성 및 4KB 정렬 배치
    payloadBlocks: list[tuple[int, int, int, int, bytes, str]] = []  # (hash, offset, compSize, uncompSize, crc32, relPath)
    dataStream = bytearray()
    curOffset = _kPackSectorAlignment  # 헤더(64B) 이후 첫 데이터 블록은 4096 경계에서 시작

    for relPath, absPath in collectedFiles:
        rawBytes = absPath.read_bytes()
        uncompSize = len(rawBytes)
        crc = binascii.crc32(rawBytes) & 0xFFFFFFFF
        pHash = fnv1a64Internal(relPath)

        if compression == _kCompressionZlib and uncompSize > 0:
            compBytes = zlib.compress(rawBytes, level=6)
            compSize = len(compBytes)
        else:
            compBytes = rawBytes
            compSize = uncompSize

        alignedOffset = alignOffsetInternal(curOffset, _kPackSectorAlignment)
        # 패딩 추가
        paddingLen = alignedOffset - curOffset
        if paddingLen > 0:
            dataStream.extend(b"\x00" * paddingLen)

        dataStream.extend(compBytes)
        curOffset = alignedOffset + compSize

        payloadBlocks.append((pHash, alignedOffset, compSize, uncompSize, crc, relPath))

    totalDataSize = len(dataStream)

    # 3. FAT 인덱스 테이블 및 스트링 풀 생성
    indexOffset = alignOffsetInternal(_kPackSectorAlignment + totalDataSize, 64)
    indexSize = fileCount * 32

    stringPoolOffset = indexOffset + indexSize
    stringPool = bytearray()
    fatEntries: list[bytes] = []

    for pHash, offset, compSize, uncompSize, crc, relPath in payloadBlocks:
        if not stripDebugStrings:
            poolOffset = len(stringPool)
            stringPool.extend(relPath.encode("utf-8") + b"\x00")
        else:
            poolOffset = 0

        # PackFileEntryOnDisk: uint64 pathHash, uint64 dataOffset, uint32 compSize, uint32 uncompSize, uint32 crc32, uint32 poolOffset
        fatEntryBytes = struct.pack("<QQIIII", pHash, offset, compSize, uncompSize, crc, poolOffset)
        fatEntries.append(fatEntryBytes)

    stringPoolSize = len(stringPool) if not stripDebugStrings else 0

    # 4. 64바이트 PackHeader 패킹
    headerBytes = struct.pack(
        "<IIIBBHHIQQQQQ2s",
        _kPackMagic,
        _kPackFormatVersion,
        dlcAppId,
        compression,
        0,  # encryptionType (None)
        _kPackSectorAlignment,
        flags,
        fileCount,
        indexOffset,
        indexSize,
        stringPoolOffset if not stripDebugStrings else 0,
        stringPoolSize,
        totalDataSize,
        b"\x00" * 2,
    )
    assert len(headerBytes) == 64, f"PackHeader size mismatch: {len(headerBytes)}"

    # 5. 최종 바이너리 파일 기록
    with open(outPackPath, "wb") as f:
        # 헤더 기록 (64B)
        f.write(headerBytes)
        # 첫 4096 섹터 경계까지 패딩
        headerPadding = _kPackSectorAlignment - len(headerBytes)
        f.write(b"\x00" * headerPadding)

        # 페이로드 기록
        f.write(dataStream)

        # FAT 인덱스 테이블 위치까지 패딩
        fatPadding = indexOffset - (_kPackSectorAlignment + totalDataSize)
        if fatPadding > 0:
            f.write(b"\x00" * fatPadding)

        # FAT 인덱스 테이블 기록
        for fatEntry in fatEntries:
            f.write(fatEntry)

        # 스트링 풀 기록 (포함 시)
        if not stripDebugStrings and stringPoolSize > 0:
            f.write(stringPool)

    print(
        f"[PackCooker] Cooked {outPackPath.name} ({fileCount} files, {outPackPath.stat().st_size:,} bytes, DLC: {dlcAppId})"
    )
    return True


def cookAll(projectRoot: Path, outputDir: Path, isShipping: bool = True) -> bool:
    """표준 Resource 하위 디렉터리들(engine, common, game)을 모두 패킹합니다."""
    resourceDir = projectRoot / "Resource"
    if not resourceDir.is_dir():
        print(f"[Error] Resource directory not found: {resourceDir}", file=sys.stderr)
        return False

    outputDir.mkdir(parents=True, exist_ok=True)
    success = True

    # 1. Engine 리소스 (engine.pack)
    engineDir = resourceDir / "engine"
    if engineDir.is_dir():
        success &= cookPack(
            engineDir, outputDir / "engine.pack", dlcAppId=0, stripDebugStrings=isShipping
        )

    # 2. Common 리소스 (common.pack)
    commonDir = resourceDir / "common"
    if commonDir.is_dir():
        success &= cookPack(
            commonDir, outputDir / "common.pack", dlcAppId=0, stripDebugStrings=isShipping
        )

    # 3. Game 팩들 (game_<name>.pack)
    gameDir = resourceDir / "game"
    if gameDir.is_dir():
        for child in sorted(gameDir.iterdir()):
            if child.is_dir():
                packName = f"game_{child.name}.pack"
                success &= cookPack(
                    child, outputDir / packName, dlcAppId=0, stripDebugStrings=isShipping
                )

    # 4. DLC 팩들 (dlc_<name>.pack)
    dlcDir = resourceDir / "dlc"
    if dlcDir.is_dir():
        for child in sorted(dlcDir.iterdir()):
            if child.is_dir():
                packName = f"dlc_{child.name}.pack"
                # DLC AppID는 1001부터 순차 부여 (또는 설정 파일 기반)
                success &= cookPack(
                    child, outputDir / packName, dlcAppId=1001, stripDebugStrings=isShipping
                )

    return success


def main() -> int:
    parser = argparse.ArgumentParser(description="SW Engine Resource Pack Cooker")
    parser.add_argument(
        "--all", action="store_true", help="Cook engine, common, and all game packs"
    )
    parser.add_argument(
        "--source", type=str, help="Source directory to pack into a single .pack file"
    )
    parser.add_argument("--output", type=str, default="", help="Output .pack file or output directory")
    parser.add_argument("--dlc-id", type=int, default=0, help="DLC AppID (0 for base game)")
    parser.add_argument(
        "--shipping",
        action="store_true",
        default=True,
        help="Strip debug string pool for shipping binary size and anti-datamining (default: True)",
    )
    parser.add_argument(
        "--include-debug-names",
        action="store_true",
        help="Include string pool containing original file paths for debugging/tools",
    )

    args = parser.parse_args()

    projectRoot = Path(__file__).resolve().parent.parent.parent
    stripNames = not args.include_debug_names

    if args.all:
        outDir = Path(args.output) if args.output else projectRoot / "Bin" / "Packs"
        success = cookAll(projectRoot, outDir, isShipping=stripNames)
        return 0 if success else 1

    if args.source and args.output:
        srcPath = Path(args.source)
        outPath = Path(args.output)
        success = cookPack(
            srcPath, outPath, dlcAppId=args.dlc_id, stripDebugStrings=stripNames
        )
        return 0 if success else 1

    # 인자가 없을 시 기본 --all 실행
    outDir = projectRoot / "Bin" / "Packs"
    success = cookAll(projectRoot, outDir, isShipping=stripNames)
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
