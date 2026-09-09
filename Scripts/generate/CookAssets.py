#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/CookAssets.py

SW Engine 통합 에셋 쿠커:
  1. Prefabs: Resource/**/prefabs/*.prefab.xml -> *.prefab.bin (PFB2 바이너리)
  2. Scenes:  Resource/**/scenes/*.scene.xml   -> *.scene.bin  (SCN1 바이너리)
  3. Packs:   Resource/ 폴더 내 에셋을 4KB 섹터 정렬 .pack 아카이브로 패킹 (SWPK)

사용법:
  py -3 Scripts/generate/CookAssets.py [--all] [--output <dir>]
  py -3 Scripts/generate/CookAssets.py --prefabs-only
  py -3 Scripts/generate/CookAssets.py --scenes-only
  py -3 Scripts/generate/CookAssets.py --packs-only
"""

from __future__ import annotations

import argparse
import binascii
import fnmatch
import json
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (
    batchCookAssets,
    getProjectRoot,
    kFilePackConfig,
    kFileRuntimeEngineConfig,
    kKeyExcludeDirs,
    kKeyExcludePatterns,
    kKeyGlobalExcludeDirs,
    kKeyGlobalExcludePatterns,
    kKeyRecursive,
    kKeyRules,
    normalizePath,
    packLengthPrefixedString,
    readJsonDictInternal,
    resolveDefaultOutputDir,
    writeBinaryIfChanged,
)

# ==============================================================================
# 1. 포맷 매직 및 상수 정의
# ==============================================================================

_kPfb2Magic = 0x50464232  # 'PFB2'
_kPfb2Version = 0

_kScn1Magic = 0x53434E31  # 'SCN1'
_kScn1Version = 0

# ------------------------------------------------------------------------------
# .pack 바이너리 포맷 계약 — Config/Engine/PackFormat.json 이 단일 출처다.
# 같은 파일에서 C++ 헤더(sw/config/PackFormat.gen.h)도 생성되므로, 여기서 레이아웃을
# 손으로 들고 있지 않는다. 예전에는 양쪽이 각자 레이아웃을 갖고 있다가 헤더가 offset 8
# 부터 어긋나, 리더가 fileCount 를 0 으로 읽는 "빈 팩"이 만들어지고 있었다.
# ------------------------------------------------------------------------------
_kPackFormatConfigRelative = "Config/Engine/PackFormat.json"

_kStructTypeCodes = {"uint8": "B", "uint16": "H", "uint32": "I", "uint64": "Q"}
_kStructTypeSizes = {"uint8": 1, "uint16": 2, "uint32": 4, "uint64": 8}


def _loadPackFormatSpecInternal() -> dict:
    """팩 포맷 계약 파일을 읽어 struct 포맷 문자열까지 계산해 돌려줍니다."""
    specPath = getProjectRoot() / _kPackFormatConfigRelative
    if not specPath.is_file():
        raise FileNotFoundError(f"Pack format contract not found: {specPath}")
    spec = json.loads(specPath.read_text(encoding="utf-8"))

    def buildLayout(part: dict) -> tuple[str, int, list[str]]:
        formatText = "<"
        totalSize = 0
        fieldNames: list[str] = []
        for field in part["fields"]:
            typeName = field["type"]
            if typeName.endswith("]"):
                baseName, countText = typeName[:-1].split("[")
                count = int(countText)
                formatText += f"{count * _kStructTypeSizes[baseName]}s"
                totalSize += count * _kStructTypeSizes[baseName]
            else:
                formatText += _kStructTypeCodes[typeName]
                totalSize += _kStructTypeSizes[typeName]
            fieldNames.append(field["name"])
        if totalSize != int(part["size"]):
            raise ValueError(f"{part['name']}: 필드 합계 {totalSize}B != 선언 크기 {part['size']}B")
        return formatText, totalSize, fieldNames

    spec["_headerLayout"] = buildLayout(spec["header"])
    spec["_entryLayout"] = buildLayout(spec["entry"])
    return spec


_gPackFormatSpec = _loadPackFormatSpecInternal()

_kPackMagic = int.from_bytes(_gPackFormatSpec["magic"].encode("ascii"), "little")
_kPackFormatVersion = int(_gPackFormatSpec["formatVersion"])
_kPackSectorAlignment = int(_gPackFormatSpec["sectorAlignment"])

_kFlagNone = _gPackFormatSpec["flags"]["None"]
_kFlagHasStringPool = _gPackFormatSpec["flags"]["HasStringPool"]
_kFlagHasCrc32 = _gPackFormatSpec["flags"]["HasCrc32"]
_kFlagEncrypted = _gPackFormatSpec["flags"]["Encrypted"]

_kCompressionNone = _gPackFormatSpec["compression"]["codecs"]["None"]
_kCompressionZlib = _gPackFormatSpec["compression"]["codecs"]["Zlib"]
_kEncryptionNone = _gPackFormatSpec["encryption"]["None"]
_kDeflateStrategy = _gPackFormatSpec["compression"].get("deflateStrategy", "default")


# ==============================================================================
# 2. Prefab 쿠커
# ==============================================================================

def readXmlPrefabInternal(path: Path) -> tuple[str, str]:
    """XML 포맷의 프리팹 파일에서 이름(name)과 내부 요소(xmlBody)를 추출합니다."""
    tree = ET.parse(path)
    root = tree.getroot()
    name = ""
    if (nameNode := root.find("name")) is not None and nameNode.text:
        name = nameNode.text.strip()
    elif root.get("name"):
        name = root.get("name", "")

    body = ""
    for tag in ("GameObject", "GameObjectState", "ObjectState"):
        if (node := root.find(tag)) is not None:
            body = ET.tostring(node, encoding="unicode")
            break
    if not body:
        body = path.read_text(encoding="utf-8")
    if not name:
        name = path.stem.split(".")[0]
    return name, body


def writePfb2Internal(outPath: Path, name: str, body: str) -> bool:
    """프리팹을 PFB2 바이너리로 변환하여 변경 시에만 기록합니다."""
    blob = (
        struct.pack("<II", _kPfb2Magic, _kPfb2Version)
        + packLengthPrefixedString(name)
        + packLengthPrefixedString(body)
    )
    return writeBinaryIfChanged(outPath, blob)


def cookPrefabs(resourceRoot: Path | None = None) -> int:
    """게임 리소스 폴더 내의 .prefab.xml 파일들을 찾아 .prefab.bin으로 변환합니다."""
    projectRoot = getProjectRoot()
    root = resourceRoot or (projectRoot / "Resource" / "game")
    if not root.is_dir():
        print(f"[CookPrefabs] Directory not found: {root} (skipping)")
        return 0

    searchRoots = sorted(path for path in root.glob("*/prefabs") if path.is_dir())
    if not searchRoots:
        print("[CookPrefabs] No prefab directory found under Resource/game/*/prefabs (skipping)")
        return 0

    tasks: list[Path] = []
    for prefabDir in searchRoots:
        tasks.extend(sorted(prefabDir.glob("*.prefab.xml")))

    def cookOne(sourceFile: Path) -> bool:
        name, body = readXmlPrefabInternal(sourceFile)
        outputBinaryFile = sourceFile.with_suffix(".bin")
        wrote = writePfb2Internal(outputBinaryFile, name, body)
        if wrote:
            print(f"[CookPrefabs] {sourceFile.name} -> {outputBinaryFile.name} ('{name}')")
        return wrote

    batchCookAssets(tasks, cookOne, label="CookPrefabs")
    return 0


# ==============================================================================
# 3. Scene 쿠커
# ==============================================================================

def readXmlSceneInternal(path: Path) -> tuple[str, list[tuple[str, str, str]]]:
    """XML 씬 파일에서 씬 이름과 엔티티 목록을 추출합니다."""
    tree = ET.parse(path)
    root = tree.getroot()
    sceneName = root.get("name", "")
    if (nameNode := root.find("name")) is not None and nameNode.text:
        sceneName = nameNode.text.strip()
    if not sceneName:
        sceneName = path.stem.split(".")[0]

    entitiesList: list[tuple[str, str, str]] = []
    if (entitiesNode := root.find("entities")) is not None:
        for entityNode in entitiesNode.findall("entity"):
            entityName = entityNode.get("name", "")
            if (entNameChild := entityNode.find("name")) is not None and entNameChild.text:
                entityName = entNameChild.text.strip()
            if not entityName:
                entityName = "Entity"

            prefabPath = entityNode.get("prefab", "")
            if (prefabChild := entityNode.find("prefab")) is not None and prefabChild.text:
                prefabPath = prefabChild.text.strip()

            embeddedXml = ""
            if (stateNode := entityNode.find("GameObject")) is not None:
                embeddedXml = ET.tostring(stateNode, encoding="unicode")
            elif (stateNode := entityNode.find("GameObjectState")) is not None:
                embeddedXml = ET.tostring(stateNode, encoding="unicode")

            entitiesList.append((entityName, prefabPath, embeddedXml))

    return sceneName, entitiesList


def writeScn1Internal(outputPath: Path, sceneName: str, entitiesList: list[tuple[str, str, str]]) -> bool:
    """씬 데이터를 SCN1 바이너리로 변환하여 변경 시에만 기록합니다."""
    chunks = [
        struct.pack("<II", _kScn1Magic, _kScn1Version),
        packLengthPrefixedString(sceneName),
        struct.pack("<I", len(entitiesList)),
    ]
    for entityName, prefabPath, embeddedXml in entitiesList:
        chunks.extend([
            packLengthPrefixedString(entityName),
            packLengthPrefixedString(prefabPath),
            packLengthPrefixedString(embeddedXml),
        ])
    return writeBinaryIfChanged(outputPath, b"".join(chunks))


def cookScenes(resourceRoot: Path | None = None) -> int:
    """Resource 하위의 모든 .scene.xml 파일을 .scene.bin으로 변환합니다."""
    root = resourceRoot or (getProjectRoot() / "Resource")
    if not root.is_dir():
        print(f"[CookScenes] Resource dir not found: {root}")
        return 0

    sceneFiles = sorted(root.rglob("*.scene.xml"))
    if not sceneFiles:
        print(f"[CookScenes] No .scene.xml found under {root}")
        return 0

    def cookOne(xmlPath: Path) -> bool:
        outputBinaryFile = xmlPath.with_suffix("").with_suffix(".bin")
        sceneName, entitiesList = readXmlSceneInternal(xmlPath)
        wrote = writeScn1Internal(outputBinaryFile, sceneName, entitiesList)
        if wrote:
            print(f"[CookScenes] Cooked {xmlPath.relative_to(root)} -> {outputBinaryFile.name} ({len(entitiesList)} entities)")
        return wrote

    batchCookAssets(sceneFiles, cookOne, label="CookScenes")
    return 0


# ==============================================================================
# 4. Resource Pack (.pack) 쿠커
# ==============================================================================

def _deflateForReaderInternal(rawBytes: bytes) -> bytes:
    """리더가 해석할 수 있는 전략으로 zlib 압축합니다(계약 파일 compression.deflateStrategy)."""
    if _kDeflateStrategy == "fixed":
        # 리더의 인플레이터가 동적 허프만을 거부하므로 Z_FIXED 로 고정한다.
        compressor = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_FIXED)
        return compressor.compress(rawBytes) + compressor.flush()
    return zlib.compress(rawBytes, level=9)


def fnv1a64Internal(path: str) -> int:
    """FNV-1a 64비트 해시를 계산합니다."""
    fnv_prime = int(_gPackFormatSpec["pathHash"]["prime"])
    fnv_offset = int(_gPackFormatSpec["pathHash"]["offsetBasis"])
    normalized = normalizePath(path).lower()
    h = fnv_offset
    for ch in normalized.encode("utf-8"):
        h ^= ch
        h = (h * fnv_prime) & 0xFFFFFFFFFFFFFFFF
    return h


def alignOffsetInternal(offset: int, alignment: int = _kPackSectorAlignment) -> int:
    """오프셋을 섹터 정렬 경계로 올림합니다."""
    mask = alignment - 1
    return (offset + mask) & ~mask


def resolveTargetRhiInternal(config: dict, cliRhi: str = "", projectRoot: Path | None = None) -> str:
    """타깃 RHI를 결정합니다: CLI > PackConfig.json > EngineConfig.json (기본값: dx12)."""
    r = ""
    if cliRhi:
        r = cliRhi.strip().lower()
    elif config.get("target_rhi"):
        r = str(config["target_rhi"]).strip().lower()
    elif projectRoot:
        engineCfgPath = projectRoot / kFileRuntimeEngineConfig
        if engineCfgPath.is_file():
            engineCfg = readJsonDictInternal(engineCfgPath, kFileRuntimeEngineConfig)
            r = engineCfg.get("_window", {}).get("_defaultRHI", "DirectX12").strip().lower()

    if r in ("directx12", "dx12", "d3d12"):
        return "dx12"
    if r in ("vulkan", "vk", "spirv"):
        return "vulkan"
    if r in ("opengl", "gl"):
        return "opengl"
    if r in ("directx11", "dx11", "d3d11"):
        return "dx11"
    return "dx12"


def bakeShadersInternal(projectRoot: Path) -> bool:
    """App.exe --bake-shaders 를 헤드리스 모드로 실행하여 바이너리를 일괄 빌드합니다."""
    import subprocess
    candidates = [
        projectRoot / "build/Ninja-Debug/Bin/App.exe",
        projectRoot / "build/Ninja-Release/Bin/App.exe",
        # Shipping App 도 베이커를 링크한다(로그만 안 남는다). 두 번째 Shipping 빌드부터는
        # 이 경로가 살아 있어서 Dev 빌드 없이도 스스로 다시 굽는다.
        projectRoot / "build/Ninja-Shipping/Bin/App.exe",
        projectRoot / "Bin/App.exe",
    ]
    appExe = None
    for c in candidates:
        if c.is_file():
            appExe = c
            break
    if not appExe:
        print("[CookAssets Warning] App.exe not found to run --bake-shaders", file=sys.stderr)
        return False
    print(f"[CookAssets] Running headless shader bake: {appExe} --bake-shaders")
    res = subprocess.run([str(appExe), "--bake-shaders"])
    return res.returncode == 0


_kBakeStampFileName = "bake.stamp"
_kBakeStampHeader = "SWBAKE 2"
_kFnv1a64Offset = 14695981039346656037
_kFnv1a64Prime = 1099511628211


def computeFnv1a64Internal(data: bytes) -> int:
    """StringUtil::computeHash64(bIgnoreCase=false) 와 같은 FNV-1a 64비트 해시입니다."""
    h = _kFnv1a64Offset
    for b in data:
        h = ((h ^ b) * _kFnv1a64Prime) & 0xFFFFFFFFFFFFFFFF
    return h


def collectShaderSourceHashesInternal(shadersDir: Path) -> dict[str, str]:
    """shaders/ 아래 .hlsl/.hlsli 의 내용 해시를 { 상대경로: hex } 로 모읍니다 (bin/ 제외).

    CR 을 뺀 바이트로 해싱합니다(스탬프 버전 2). 저장소에 `.gitattributes` 가 없어 체크아웃마다 줄
    끝이 달라질 수 있고, 바이트를 그대로 해싱하면 같은 소스가 PC 마다 다른 값을 냅니다 — 그러면
    Shipping 빌드가 매번 스탬프를 다시 써서 작업 트리가 더러워집니다. `ShaderBaker::writeBakeStamp`
    가 같은 정규화를 합니다.
    """
    result: dict[str, str] = {}
    for path in sorted(shadersDir.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() not in (".hlsl", ".hlsli"):
            continue
        rel = normalizePath(str(path.relative_to(shadersDir))).lower()
        if rel.startswith("bin/") or "/bin/" in rel:
            continue
        result[rel] = "%016x" % computeFnv1a64Internal(path.read_bytes().replace(b"\r", b""))
    return result


def verifyShaderBakeInternal(projectRoot: Path, targetRhi: str) -> list[str]:
    """구워둔 셰이더가 **지금 소스에서 나온 것인지** 확인하고 문제 목록을 돌려줍니다.

    파일 시간이 아니라 bake.stamp 의 내용 해시로 본다 — git clone 은 모든 파일의 mtime 을
    체크아웃 시각으로 덮어써서 시간 비교가 무의미하다. Shipping 은 런타임 컴파일이 없어
    구운 것이 낡으면 화면이 통째로 비므로, 여기서 막지 못하면 그대로 배포된다.
    """
    problems: list[str] = []
    resourceDir = projectRoot / "Resource"

    domains: list[Path] = []
    for name in ("engine", "common"):
        d = resourceDir / name
        if d.is_dir():
            domains.append(d)
    gameDir = resourceDir / "game"
    if gameDir.is_dir():
        domains.extend(sorted(sub for sub in gameDir.iterdir() if sub.is_dir()))

    for domainDir in domains:
        shadersDir = domainDir / "shaders"
        if not shadersDir.is_dir():
            continue

        expected = collectShaderSourceHashesInternal(shadersDir)
        if not expected:
            continue

        label = f"{domainDir.name}/shaders"
        binDir = shadersDir / "bin" / targetRhi
        if not binDir.is_dir():
            problems.append(f"{label}: '{targetRhi}' 바이너리 폴더가 없습니다 (한 번도 베이킹하지 않았습니다)")
            continue

        stampPath = binDir / _kBakeStampFileName
        if not stampPath.is_file():
            problems.append(f"{label}: {_kBakeStampFileName} 이 없습니다 (베이커가 남기는 파일입니다)")
            continue

        lines = stampPath.read_text(encoding="utf-8").splitlines()
        if not lines or lines[0].strip() != _kBakeStampHeader:
            problems.append(f"{label}: {_kBakeStampFileName} 형식을 알 수 없습니다")
            continue

        stamped: dict[str, str] = {}
        for line in lines[1:]:
            line = line.strip()
            if not line:
                continue
            parts = line.split(" ", 1)
            if len(parts) != 2:
                continue
            stamped[parts[1].strip()] = parts[0].strip()

        for rel, digest in sorted(expected.items()):
            if rel not in stamped:
                problems.append(f"{label}/{rel}: 베이킹한 적 없는 셰이더입니다")
            elif stamped[rel] != digest:
                problems.append(f"{label}/{rel}: 소스가 바뀌었는데 다시 굽지 않았습니다")
        for rel in sorted(set(stamped) - set(expected)):
            problems.append(f"{label}/{rel}: 사라진 셰이더가 스탬프에 남아 있습니다")

        if not any(child.suffix.lower() in (".dxil", ".dxbc", ".spv") for child in binDir.iterdir()):
            problems.append(f"{label}: '{targetRhi}' 바이너리가 하나도 없습니다")
        if not (binDir / "reflection.manifest").is_file():
            problems.append(f"{label}: '{targetRhi}' 리플렉션 매니페스트가 없습니다")

    return problems


def shouldIncludeFileInternal(relPath: str, config: dict, targetRhi: str = "dx12") -> bool:
    """PackConfig에 정의된 전역 및 개별 규칙에 따라 파일 패킹 포함 여부를 판단합니다."""
    normRel = normalizePath(relPath).lower()
    parts = normRel.split("/")
    fileName = parts[-1]
    dirParts = parts[:-1]

    for exDir in config.get(kKeyGlobalExcludeDirs, []):
        if exDir.lower() in dirParts:
            return False

    for exPattern in config.get(kKeyGlobalExcludePatterns, []):
        pLower = exPattern.lower()
        if fnmatch.fnmatch(fileName, pLower) or fnmatch.fnmatch(normRel, pLower):
            return False

    for rule in config.get(kKeyRules, []):
        for exDir in rule.get(kKeyExcludeDirs, []):
            exLower = exDir.lower()
            isRecursive = rule.get(kKeyRecursive, True)
            if isRecursive:
                if exLower in dirParts:
                    return False
            else:
                if dirParts and dirParts[0] == exLower:
                    return False

        for exPattern in rule.get(kKeyExcludePatterns, []):
            pLower = exPattern.lower()
            if fnmatch.fnmatch(fileName, pLower) or fnmatch.fnmatch(normRel, pLower):
                return False

    # 셰이더 전용 스마트 필터링
    shaderCookCfg = config.get("shader_cook", {})
    if shaderCookCfg.get("exclude_raw_hlsl", True):
        if fileName.endswith(".hlsl") or fileName.endswith(".hlsli"):
            return False

    if "shaders/bin" in normRel:
        for rhiFolder in ("dx12", "vulkan", "dx11"):
            if rhiFolder in dirParts and rhiFolder != targetRhi:
                return False

    return True


def cookPack(
    sourceDir: Path,
    outPackPath: Path,
    dlcAppId: int = 0,
    compression: int = _kCompressionZlib,
    stripDebugStrings: bool = True,
    packConfig: dict | None = None,
    targetRhi: str = "dx12",
) -> bool:
    """단일 디렉터리 내 에셋들을 .pack 파일로 패킹합니다."""
    if not sourceDir.is_dir():
        print(f"[CookAssets Error] Source directory does not exist: {sourceDir}", file=sys.stderr)
        return False

    fileEntries: list[tuple[str, Path, int]] = []
    for p in sorted(sourceDir.rglob("*")):
        if p.is_file():
            rel = p.relative_to(sourceDir).as_posix()
            if packConfig and not shouldIncludeFileInternal(rel, packConfig, targetRhi=targetRhi):
                continue
            pathHash = fnv1a64Internal(rel)
            fileEntries.append((rel, p, pathHash))

    fileEntries.sort(key=lambda item: item[2])
    fileCount = len(fileEntries)

    flags = _kFlagHasCrc32
    if not stripDebugStrings:
        flags |= _kFlagHasStringPool

    stringPoolBytes = bytearray()
    stringPoolOffsets: list[int] = []
    if not stripDebugStrings:
        for rel, _, _ in fileEntries:
            stringPoolOffsets.append(len(stringPoolBytes))
            stringPoolBytes.extend(rel.encode("utf-8") + b"\x00")

    headerSize = int(_gPackFormatSpec["header"]["size"])
    tocEntrySize = int(_gPackFormatSpec["entry"]["size"])
    tocTotalSize = fileCount * tocEntrySize
    tocStartOffset = alignOffsetInternal(headerSize)
    dataStartOffset = alignOffsetInternal(tocStartOffset + tocTotalSize)

    dataOffset = dataStartOffset
    tocRecords: list[bytes] = []
    dataBlobs: list[bytes] = []

    entryFormat, entrySize, _ = _gPackFormatSpec["_entryLayout"]

    for index, (rel, filePath, pathHash) in enumerate(fileEntries):
        rawBytes = filePath.read_bytes()
        rawSize = len(rawBytes)
        crc = binascii.crc32(rawBytes) & 0xFFFFFFFF

        # 압축 코덱은 팩 단위다(계약 파일 compression.scope="pack"). 리더가 헤더의 코덱
        # 하나로 모든 항목을 해제하므로, 압축 이득이 없는 파일도 같은 코덱으로 넣어야 한다.
        if compression == _kCompressionZlib:
            payload = _deflateForReaderInternal(rawBytes)
        else:
            payload = rawBytes

        compressedSize = len(payload)
        dataOffsetAligned = alignOffsetInternal(dataOffset)
        padding = dataOffsetAligned - dataOffset
        if padding > 0:
            dataBlobs.append(b"\x00" * padding)
        dataOffset = dataOffsetAligned

        strOffset = stringPoolOffsets[index] if not stripDebugStrings else 0
        tocEntry = struct.pack(
            entryFormat,
            pathHash,
            dataOffset,
            compressedSize,
            rawSize,
            crc,
            strOffset,
        )
        assert len(tocEntry) == entrySize
        tocRecords.append(tocEntry)
        dataBlobs.append(payload)
        dataOffset += compressedSize

    stringPoolOffset = 0
    stringPoolSize = 0
    if not stripDebugStrings and stringPoolBytes:
        stringPoolOffset = alignOffsetInternal(dataOffset)
        stringPoolSize = len(stringPoolBytes)

    headerFormat, headerSizeFromSpec, _ = _gPackFormatSpec["_headerLayout"]
    header = struct.pack(
        headerFormat,
        _kPackMagic,
        _kPackFormatVersion,
        dlcAppId,
        compression,
        _kEncryptionNone,
        _kPackSectorAlignment,
        flags,
        fileCount,
        tocStartOffset,
        tocTotalSize,
        stringPoolOffset,
        stringPoolSize,
        dataOffset - dataStartOffset,
        b"\x00" * 2,
    )
    assert len(header) == headerSizeFromSpec

    outPackPath.parent.mkdir(parents=True, exist_ok=True)
    with open(outPackPath, "wb") as f:
        f.write(header)
        padding = tocStartOffset - len(header)
        if padding > 0:
            f.write(b"\x00" * padding)
        for entry in tocRecords:
            f.write(entry)
        padding = dataStartOffset - (tocStartOffset + tocTotalSize)
        if padding > 0:
            f.write(b"\x00" * padding)
        for blob in dataBlobs:
            f.write(blob)
        if not stripDebugStrings and stringPoolBytes:
            padding = stringPoolOffset - dataOffset
            if padding > 0:
                f.write(b"\x00" * padding)
            f.write(stringPoolBytes)

    packSize = outPackPath.stat().st_size
    print(f"[PackCooker] Cooked {outPackPath.name} ({fileCount} files, {packSize:,} bytes, DLC: {dlcAppId})")
    return True


def cookAllPacks(
    projectRoot: Path,
    outputDir: Path,
    isShipping: bool = True,
    packConfig: dict | None = None,
    targetRhi: str = "dx12",
) -> bool:
    """engine, common, 그리고 game 에셋 디렉터리들을 일괄 패킹합니다."""
    resourceDir = projectRoot / "Resource"
    outputDir.mkdir(parents=True, exist_ok=True)
    allSuccess = True

    targets: list[tuple[Path, Path, int]] = []
    engineDir = resourceDir / "engine"
    if engineDir.is_dir():
        targets.append((engineDir, outputDir / "engine.pack", 0))

    commonDir = resourceDir / "common"
    if commonDir.is_dir():
        targets.append((commonDir, outputDir / "common.pack", 0))

    gameDir = resourceDir / "game"
    if gameDir.is_dir():
        for sub in sorted(gameDir.iterdir()):
            if sub.is_dir():
                targets.append((sub, outputDir / f"game_{sub.name}.pack", 0))

    for src, out, dlcId in targets:
        success = cookPack(src, out, dlcAppId=dlcId, stripDebugStrings=isShipping, packConfig=packConfig, targetRhi=targetRhi)
        if not success:
            allSuccess = False

    return allSuccess


# ==============================================================================
# 5. 메인 통합 실행 진입점
# ==============================================================================

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="SW Engine 통합 에셋 쿠커 (Prefabs, Scenes, Packs)")
    parser.add_argument("--all", action="store_true", help="프리팹, 씬, 리소스 팩 전체를 순서대로 쿠킹 (기본 동작)")
    parser.add_argument("--prefabs-only", action="store_true", help="프리팹 바이너리(.prefab.bin)만 쿠킹")
    parser.add_argument("--scenes-only", action="store_true", help="씬 바이너리(.scene.bin)만 쿠킹")
    parser.add_argument("--packs-only", action="store_true", help="리소스 팩(.pack)만 쿠킹")
    parser.add_argument("--output", type=str, default="", help="팩 출력 디렉터리")
    parser.add_argument("--config", type=str, default="", help="PackConfig.json 경로")
    parser.add_argument("--include-debug-names", action="store_true", help="팩 내부에 파일 경로 디버그 문자열 포함")
    parser.add_argument("--target-rhi", type=str, default="", help="타깃 RHI 백엔드 (DirectX12, Vulkan, DirectX11)")
    parser.add_argument("--bake-shaders", action="store_true", help="패킹 전 App.exe --bake-shaders 를 실행하여 셰이더 일괄 사전 빌드")
    parser.add_argument("--verify-shaders", action="store_true", help="구운 셰이더가 현재 소스에서 나온 것인지 확인하고, 아니면 쿠킹을 중단")

    args = parser.parse_args(argv)
    projectRoot = getProjectRoot()

    if args.bake_shaders:
        bakeShadersInternal(projectRoot)

    doPrefabs = args.prefabs_only or (not args.scenes_only and not args.packs_only)
    doScenes = args.scenes_only or (not args.prefabs_only and not args.packs_only)
    doPacks = args.packs_only or (not args.prefabs_only and not args.scenes_only)

    exitCode = 0
    if doPrefabs:
        exitCode = cookPrefabs() or exitCode
    if doScenes:
        exitCode = cookScenes() or exitCode
    if doPacks:
        stripNames = not args.include_debug_names
        configPath = Path(args.config) if args.config else (projectRoot / kFilePackConfig)
        packConfig = readJsonDictInternal(configPath, kFilePackConfig)
        if not packConfig:
            print(f"[CookAssets Error] missing or invalid config: {configPath}", file=sys.stderr)
            return 1
        targetRhi = resolveTargetRhiInternal(packConfig, cliRhi=args.target_rhi, projectRoot=projectRoot)
        print(f"[CookAssets] Target RHI for shader packaging: {targetRhi}")

        if args.verify_shaders:
            problems = verifyShaderBakeInternal(projectRoot, targetRhi)
            # 낡았을 뿐이라면 베이커가 있는 자리에서는 스스로 고친다 — 로컬 Shipping 빌드가
            # 셰이더 한 줄 고칠 때마다 손으로 --bake-shaders 를 부르라고 요구할 이유는 없다.
            if problems and not args.bake_shaders and bakeShadersInternal(projectRoot):
                problems = verifyShaderBakeInternal(projectRoot, targetRhi)
            if problems:
                print("[CookAssets Error] 구워둔 셰이더가 현재 소스와 맞지 않습니다.", file=sys.stderr)
                print("                   배포 빌드는 런타임 컴파일이 없어 이대로 패킹하면 화면이 비게 됩니다.", file=sys.stderr)
                for problem in problems[:20]:
                    print(f"                   - {problem}", file=sys.stderr)
                if len(problems) > 20:
                    print(f"                   ... 외 {len(problems) - 20}건", file=sys.stderr)
                print("                   해결: build/Ninja-Debug/Bin/App.exe --bake-shaders", file=sys.stderr)
                return 1
        outDir = Path(args.output) if args.output else resolveDefaultOutputDir(projectRoot, "Packs")
        success = cookAllPacks(projectRoot, outDir, isShipping=stripNames, packConfig=packConfig, targetRhi=targetRhi)
        if not success:
            exitCode = 1

    return exitCode


if __name__ == "__main__":
    sys.exit(main())
