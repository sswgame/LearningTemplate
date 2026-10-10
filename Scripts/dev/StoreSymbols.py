#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file StoreSymbols.py
@brief 빌드의 심볼을 심볼 저장소 배치로 복사합니다 — 크래시 묶음의 `buildID` 로 그 빌드의 심볼을 찾는 자리입니다.

    py -3 -m Scripts symbols --preset Ninja-Shipping --store D:/SymbolStore
    py -3 -m Scripts symbols --preset Ninja-Shipping --store D:/SymbolStore --dry-run

Windows: `Symbols/*.pdb`(없으면 Dev 의 `Bin/Symbols/*.pdb`)를 `<저장소>/<pdb 이름>/<GUID 32 자리><age 16진>/<pdb 이름>`(symstore · 심볼 서버 배치)로.
PDB 의 GUID · age 는 PDB 자신의 정보 스트림(스트림 1)에서 읽는다 — 실행 파일의 RSDS 와 같은 값이다(`ModuleBuildID` 가 만드는 열쇠).
리눅스: `Symbols/*.debug` 를 `<저장소>/.build-id/<앞 2 자리>/<나머지>.debug`(gdb · Sentry 배치)로 — build-id 는 `.note.gnu.build-id` 에서 읽는다.
배포물(`Bin`)에는 심볼을 싣지 않는다.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from common import BuildTree, addBuildTreeArguments  # noqa: E402

_kMsfMagic = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\x00\x00\x00"
_kPdbInfoStream = 1
_kElfMagic = b"\x7fELF"
_kNoteGnuBuildId = 3


def readPdbSignatureInternal(path: Path) -> tuple[str, int] | None:
    """MSF 7.0 PDB 의 정보 스트림에서 (GUID 대문자 16진 32 자리, age) 를 읽는다. PDB 가 아니면 None."""
    data = path.read_bytes()
    if not data.startswith(_kMsfMagic):
        return None
    blockSize, _freeBlockMap, _blockCount, directorySize, _reserved, blockMapAddress = struct.unpack_from("<6I", data, len(_kMsfMagic))
    directoryBlockCount = (directorySize + blockSize - 1) // blockSize
    listDirectoryBlock = struct.unpack_from(f"<{directoryBlockCount}I", data, blockMapAddress * blockSize)
    directory = b"".join(data[block * blockSize:(block + 1) * blockSize] for block in listDirectoryBlock)[:directorySize]
    streamCount = struct.unpack_from("<I", directory, 0)[0]
    if streamCount <= _kPdbInfoStream:
        return None
    listStreamSize = struct.unpack_from(f"<{streamCount}I", directory, 4)
    offset = 4 + 4 * streamCount
    for streamIndex in range(_kPdbInfoStream):
        size = listStreamSize[streamIndex]
        offset += 4 * ((size + blockSize - 1) // blockSize if size != 0xFFFFFFFF else 0)
    firstBlock = struct.unpack_from("<I", directory, offset)[0]
    info = data[firstBlock * blockSize:firstBlock * blockSize + 28]
    _version, _signature, age = struct.unpack_from("<3I", info, 0)
    data1, data2, data3 = struct.unpack_from("<IHH", info, 12)
    data4 = info[20:28]
    guidText = f"{data1:08X}{data2:04X}{data3:04X}{data4.hex().upper()}"
    return guidText, age


def readBuildIdInternal(path: Path) -> str | None:
    """64 비트 리틀 엔디언 ELF 의 `.note.gnu.build-id`(노트 타입 3) 설명 바이트를 소문자 16진으로. 없으면 None."""
    data = path.read_bytes()
    if not data.startswith(_kElfMagic) or data[4] != 2 or data[5] != 1:
        return None
    sectionOffset = struct.unpack_from("<Q", data, 0x28)[0]
    sectionEntrySize, sectionCount, nameSectionIndex = struct.unpack_from("<HHH", data, 0x3A)
    listSection = [struct.unpack_from("<IIQQQQIIQQ", data, sectionOffset + index * sectionEntrySize) for index in range(sectionCount)]
    nameTableOffset = listSection[nameSectionIndex][4]
    for section in listSection:
        nameOffset = nameTableOffset + section[0]
        name = data[nameOffset:data.index(b"\x00", nameOffset)]
        if name != b".note.gnu.build-id":
            continue
        position = section[4]
        end = position + section[5]
        while position + 12 <= end:
            nameSize, descriptionSize, noteType = struct.unpack_from("<3I", data, position)
            descriptionOffset = position + 12 + ((nameSize + 3) & ~3)
            if noteType == _kNoteGnuBuildId:
                return data[descriptionOffset:descriptionOffset + descriptionSize].hex()
            position = descriptionOffset + ((descriptionSize + 3) & ~3)
    return None


def storeFileInternal(source: Path, destination: Path, bDryRun: bool) -> bool:
    """같은 크기의 파일이 이미 있으면 건너뛴다. 복사했으면 True."""
    if destination.is_file() and destination.stat().st_size == source.stat().st_size:
        return False
    print(f"  {source.name} -> {destination}")
    if not bDryRun:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    return True


def collectSymbolFilesInternal(buildDir: Path) -> list[Path]:
    symbolsDir = buildDir / "Symbols"
    listFile = sorted(symbolsDir.glob("*.pdb")) + sorted(symbolsDir.glob("*.debug")) if symbolsDir.is_dir() else []
    if not listFile:
        # Dev 빌드는 PDB 를 `Bin/Symbols` 에 낸다(cmake/Engine/BuildLayout.cmake). 그보다 옛 빌드 폴더는 `Bin` 옆이다.
        devSymbolsDir = buildDir / "Bin" / "Symbols"
        listFile = sorted(devSymbolsDir.glob("*.pdb")) if devSymbolsDir.is_dir() else sorted((buildDir / "Bin").glob("*.pdb"))
    return listFile


def main(listArgument: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="빌드의 심볼(PDB · .debug)을 심볼 저장소 배치로 복사한다")
    addBuildTreeArguments(parser, defaultPreset="Ninja-Shipping")
    parser.add_argument("--store", type=Path, required=True, help="심볼 저장소 폴더")
    parser.add_argument("--dry-run", action="store_true", help="복사하지 않고 배치만 찍는다")
    args = parser.parse_args(listArgument)

    buildDir = BuildTree.fromArguments(args).path
    listFile = collectSymbolFilesInternal(buildDir)
    if not listFile:
        print(f"no symbol file under {buildDir}/Symbols or {buildDir}/Bin", file=sys.stderr)
        return 1
    copiedCount = 0
    failedCount = 0
    for path in listFile:
        if path.suffix == ".pdb":
            signature = readPdbSignatureInternal(path)
            if signature is None:
                print(f"  [skip] {path.name}: not an MSF 7.0 PDB", file=sys.stderr)
                failedCount += 1
                continue
            guidText, age = signature
            destination = args.store / path.name / f"{guidText}{age:X}" / path.name
        else:
            buildID = readBuildIdInternal(path)
            if buildID is None or len(buildID) < 3:
                print(f"  [skip] {path.name}: no .note.gnu.build-id", file=sys.stderr)
                failedCount += 1
                continue
            destination = args.store / ".build-id" / buildID[:2] / f"{buildID[2:]}.debug"
        if storeFileInternal(path, destination, args.dry_run):
            copiedCount += 1
    print(f"{copiedCount} stored, {len(listFile) - copiedCount - failedCount} already present, {failedCount} skipped")
    return 1 if failedCount else 0


if __name__ == "__main__":
    sys.exit(main())
