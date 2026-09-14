#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/CookAssets.py

SW Engine 통합 에셋 쿠커:
  1. Prefabs: Resource/**/prefabs/*.prefab.xml -> <cooked-dir>/**/prefabs/*.prefab.bin (PFB2 바이너리)
  2. Scenes:  Resource/**/*.scene.xml          -> <cooked-dir>/**/<name>.scene.bin     (SCN1 바이너리)
     (산출물은 소스 옆이 아니라 스테이징 폴더에 쓰고, 팩에는 같은 상대 경로로 병합한다)
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
    PackFormatSpec,
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
# 레이아웃을 여기서 손으로 들고 있지 않는다. 예전에는 쿠커와 C++ 헤더 생성기가 각자 레이아웃을
# 갖고 있다가 헤더가 offset 8 부터 어긋나, 리더가 fileCount 를 0 으로 읽는 "빈 팩"이 만들어지고
# 있었다. 계약을 읽는 일은 이제 `Scripts/common/PackFormat.py` 한 곳이고, 같은 객체를 헤더
# 생성기(GeneratePackFormat.py)도 쓴다.
# ------------------------------------------------------------------------------
_gPackFormat = PackFormatSpec.load()


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


def cookedOutputPathInternal(sourceFile: Path, resourceDir: Path, cookedDir: Path, outputName: str) -> Path:
    """쿠킹 산출물의 스테이징 경로: `<cookedDir>/<Resource 기준 상대 폴더>/<outputName>`.

    산출물은 소스 옆이 아니라 `build/<preset>/Cooked/` 에 쓴다. 소스 옆에 두면 (1) `.gitignore` 로 가려야
    하고, (2) 소스가 옮겨지거나 지워진 뒤에도 낡은 .bin 이 남아 Dev 런타임이 그것으로 물러나 실패를 가린다
    (프리팹을 옮기는 실험에서 실제로 그랬다). 팩에 들어가는 상대 경로는 그대로다 — 팩은 "어디에 있었나" 가
    아니라 "팩 안의 상대 경로" 로 정해지므로 cookPack 이 스테이징 폴더를 같은 경로로 병합한다.
    """
    relDir = sourceFile.parent.relative_to(resourceDir)
    return cookedDir / relDir / outputName


def cookPrefabs(resourceRoot: Path | None = None, cookedDir: Path | None = None) -> int:
    """게임 리소스 폴더 내의 .prefab.xml 파일들을 찾아 .prefab.bin으로 변환합니다 (스테이징 폴더에)."""
    projectRoot = getProjectRoot()
    resourceDir = projectRoot / "Resource"
    cookedDir = cookedDir or resolveDefaultOutputDir(projectRoot, "Cooked")
    root = resourceRoot or (resourceDir / "game")
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
        outputBinaryFile = cookedOutputPathInternal(sourceFile, resourceDir, cookedDir, sourceFile.with_suffix(".bin").name)
        wrote = writePfb2Internal(outputBinaryFile, name, body)
        if wrote:
            print(f"[CookPrefabs] {sourceFile.name} -> {outputBinaryFile.name} ('{name}')")
        return wrote

    batchCookAssets(tasks, cookOne, label="CookPrefabs")
    return 0


# ==============================================================================
# 3. Scene 쿠커
# ==============================================================================

def readXmlSceneInternal(path: Path) -> tuple[str, list[tuple[str, str, str, str]]]:
    """XML 씬 파일에서 씬 이름과 엔티티 목록 (이름, 프리팹 경로, 프리팹 GUID, 본문 XML) 을 추출합니다.

    항목 순서는 C++ SceneDocument::saveBinary/loadBinary 와 같아야 한다 — 예전엔 여기가 prefabGuid 를 빼먹어
    배포본은 옮긴 프리팹을 GUID 로 찾을 길이 없었고, 네 문자열을 읽는 리더가 세 문자열 스트림을 어긋나게 읽었다.
    """
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

            prefabGuid = entityNode.get("prefabGuid", "")
            if (guidChild := entityNode.find("prefabGuid")) is not None and guidChild.text:
                prefabGuid = guidChild.text.strip()

            embeddedXml = ""
            if (stateNode := entityNode.find("GameObject")) is not None:
                embeddedXml = ET.tostring(stateNode, encoding="unicode")
            elif (stateNode := entityNode.find("GameObjectState")) is not None:
                embeddedXml = ET.tostring(stateNode, encoding="unicode")

            entitiesList.append((entityName, prefabPath, prefabGuid, embeddedXml))

    return sceneName, entitiesList


def writeScn1Internal(outputPath: Path, sceneName: str, entitiesList: list[tuple[str, str, str, str]]) -> bool:
    """씬 데이터를 SCN1 바이너리로 변환하여 변경 시에만 기록합니다 (엔티티마다 이름·프리팹·프리팹 GUID·본문 순)."""
    chunks = [
        struct.pack("<II", _kScn1Magic, _kScn1Version),
        packLengthPrefixedString(sceneName),
        struct.pack("<I", len(entitiesList)),
    ]
    for entityName, prefabPath, prefabGuid, embeddedXml in entitiesList:
        chunks.extend([
            packLengthPrefixedString(entityName),
            packLengthPrefixedString(prefabPath),
            packLengthPrefixedString(prefabGuid),
            packLengthPrefixedString(embeddedXml),
        ])
    return writeBinaryIfChanged(outputPath, b"".join(chunks))


def cookScenes(resourceRoot: Path | None = None, cookedDir: Path | None = None) -> int:
    """Resource 하위의 모든 .scene.xml 파일을 .bin 으로 변환합니다 (스테이징 폴더에)."""
    projectRoot = getProjectRoot()
    resourceDir = projectRoot / "Resource"
    cookedDir = cookedDir or resolveDefaultOutputDir(projectRoot, "Cooked")
    root = resourceRoot or resourceDir
    if not root.is_dir():
        print(f"[CookScenes] Resource dir not found: {root}")
        return 0

    sceneFiles = sorted(root.rglob("*.scene.xml"))
    if not sceneFiles:
        print(f"[CookScenes] No .scene.xml found under {root}")
        return 0

    def cookOne(xmlPath: Path) -> bool:
        # 런타임(SceneDocument::load)은 `<name>.scene.xml` 의 짝을 `<name>.scene.bin` 으로 찾는다 — 예전엔 `.scene` 까지
        # 벗겨 `<name>.bin` 을 만들어 배포본이 씬을 한 번도 열지 못했다(시작 씬이 없어 드러나지 않았다).
        outputBinaryFile = cookedOutputPathInternal(xmlPath, resourceDir, cookedDir, xmlPath.with_suffix(".bin").name)
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
    if _gPackFormat.deflateStrategy == "fixed":
        # 리더의 인플레이터가 동적 허프만을 거부하므로 Z_FIXED 로 고정한다.
        compressor = zlib.compressobj(9, zlib.DEFLATED, 15, 9, zlib.Z_FIXED)
        return compressor.compress(rawBytes) + compressor.flush()
    return zlib.compress(rawBytes, level=9)


def resolveCompressionCodecInternal(packConfig: dict | None) -> tuple[int, int]:
    """PackConfig 의 `compression` 을 (코덱 값, 레벨) 로 풉니다. 없으면 Zlib 기본입니다."""
    section = (packConfig or {}).get("compression") or {}
    name = str(section.get("codec", "Zlib"))
    level = int(section.get("level", 0))

    if name not in _gPackFormat.mapCodec:
        known = ", ".join(sorted(_gPackFormat.mapCodec))
        raise SystemExit(
            f"[Pack] PackConfig.compression.codec='{name}' 은 팩 포맷에 없는 코덱입니다. 가능한 값: {known}"
        )
    return int(_gPackFormat.mapCodec[name]), level


def compressPayloadInternal(rawBytes: bytes, compression: int, level: int) -> bytes:
    """팩 코덱으로 한 항목을 압축합니다.

    **모듈이 없으면 조용히 물러나지 않고 그 자리에서 멈춥니다.** 설정이 LZ4 인데 zlib 으로 구우면
    설정과 산출물이 달라지고, 그 사실은 한참 뒤 배포본에서야 드러납니다.
    """
    if compression == _gPackFormat.codecZlib:
        return _deflateForReaderInternal(rawBytes)

    if compression == _gPackFormat.mapCodec.get("LZ4"):
        try:
            import lz4.block  # type: ignore
        except ImportError as exc:
            raise SystemExit(
                "[Pack] LZ4 로 굽도록 설정돼 있는데 파이썬 lz4 모듈이 없습니다.  py -3 -m pip install lz4"
            ) from exc
        # 리더는 raw LZ4 블록을 기대한다(엔트리 헤더에 원본 크기가 이미 있다).
        mode = "high_compression" if level > 0 else "default"
        if mode == "high_compression":
            return lz4.block.compress(rawBytes, mode=mode, compression=level, store_size=False)
        return lz4.block.compress(rawBytes, mode=mode, store_size=False)

    if compression == _gPackFormat.mapCodec.get("Zstd"):
        try:
            import zstandard  # type: ignore
        except ImportError as exc:
            raise SystemExit(
                "[Pack] Zstd 로 굽도록 설정돼 있는데 파이썬 zstandard 모듈이 없습니다.  py -3 -m pip install zstandard"
            ) from exc
        compressor = zstandard.ZstdCompressor(level=level if level > 0 else 3)
        return compressor.compress(rawBytes)

    if compression == _gPackFormat.codecNone:
        return rawBytes

    raise SystemExit(f"[Pack] 쿠커가 모르는 압축 코덱 값입니다: {compression}")


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
_kBakeStampHeader = "SWBAKE 3"
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

    CR 을 뺀 바이트로 해싱합니다(스탬프 버전 3). 저장소에 `.gitattributes` 가 없어 체크아웃마다 줄
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
        manifestPath = binDir / "reflection.manifest"
        if not manifestPath.is_file():
            problems.append(f"{label}: '{targetRhi}' 리플렉션 매니페스트가 없습니다")
        else:
            # 바이너리는 있는데 매니페스트에 그 키가 없는 조합을 잡는다. 스탬프는 **소스**가 바뀌었는지만 보므로
            # 베이커의 레시피 목록이 늘어난 경우(새 퍼뮤테이션 축)는 여기가 아니면 못 잡는다 — 실제로 Unlit x
            # 머티리얼 바이너리가 커밋돼 있는데 매니페스트에는 없어서, 배포 빌드가 맞는 바이트코드를 리플렉션
            # 없이 바인딩하고 DX12 가 DEVICE_HUNG 으로 죽었다. 매니페스트 키는 바이너리 파일 이름 그대로라
            # 바이트 검색으로 충분하다. 다시 구우면 매니페스트는 현재 레시피로 새로 쓰이므로, 그 뒤에도 남는
            # 바이너리는 레시피에서 빠진 낡은 파일이다.
            manifestBytes = manifestPath.read_bytes()
            uncovered = sorted(
                child.name
                for child in binDir.iterdir()
                if child.suffix.lower() in (".dxil", ".dxbc", ".spv") and child.name.encode("utf-8") not in manifestBytes
            )
            if uncovered:
                sample = ", ".join(uncovered[:3]) + (" ..." if len(uncovered) > 3 else "")
                # 메시지에 cp949 밖 문자(—)를 쓰지 않는다 — Windows 콘솔에서 print 자체가 죽는다.
                problems.append(f"{label}: '{targetRhi}' 매니페스트에 없는 바이너리 {len(uncovered)}개 ({sample}): 다시 굽거나 낡은 파일을 지우십시오")

    return problems


def isCookedArtifactInternal(relPath: str) -> bool:
    """쿠커가 만드는 산출물 이름인가 — .prefab.bin, maps/·scenes/ 아래 .bin (씬). 소스 트리에서 보이면 잔재다."""
    normRel = normalizePath(relPath).lower()
    if normRel.endswith(".prefab.bin"):
        return True
    if normRel.endswith(".bin") and ("/maps/" in f"/{normRel}" or "/scenes/" in f"/{normRel}"):
        return True
    return False


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


_kAssetRegistryFileName = "assetregistry.txt"


def buildAssetRegistryInternal(domainDir: Path) -> bytes:
    """도메인 아래 모든 .meta 를 `<guid> <sourcePath>` 한 줄씩으로 모읍니다 (AssetDatabase::loadRegistryText 가 읽는 형식).

    배포본은 .meta 를 싣지 않으므로(PackConfig `*.meta` 제외) 이 파일이 배포본 GUID 의 유일한 출처다.
    없으면 씬·프리팹의 GUID 참조가 전부 경로 폴백으로 가고, 이름을 바꾼 에셋은 배포본에서만 못 찾는다.
    """
    lines: list[str] = []
    for metaPath in sorted(domainDir.rglob("*.meta")):
        guid = ""
        sourcePath = ""
        for rawLine in metaPath.read_text(encoding="utf-8", errors="replace").splitlines():
            key, sep, value = rawLine.strip().partition("=")
            if not sep:
                continue
            if key == "guid":
                guid = value.strip()
            elif key == "sourcePath":
                sourcePath = normalizePath(value.strip())
        if guid and sourcePath:
            lines.append(f"{guid} {sourcePath}")
    if not lines:
        return b""
    return ("# <guid> <sourcePath> - CookAssets.py buildAssetRegistryInternal\n" + "\n".join(lines) + "\n").encode("utf-8")


class PackWriter:
    """
    팩 하나를 쌓아 올리는 동안의 상태 — 담긴 항목 · 데이터 커서 · TOC · 스트링 풀.

    예전에는 `cookPack` 이 150줄 한 함수 안에서 그 넷을 동시에 굴렸다. 오프셋 커서(`dataOffset`)가
    한 루프 안에서 정렬·패딩·증가를 다 겪고, 같은 루프가 TOC 레코드와 페이로드 블롭을 각각 모으고,
    스트링 풀은 **그보다 앞선 별도의 루프**에서 쌓인 뒤 인덱스로 다시 맞춰졌다 — 두 루프가 같은
    순서로 돈다는 전제가 코드 어디에도 적혀 있지 않았다.

    여기서는 순서가 메서드 이름이다: `add*` 로 담고 `writeTo` 로 굽는다. 스트링 풀도 TOC 와 같은
    루프에서 같은 순서로 쌓이므로 인덱스를 맞출 일이 없다.
    """

    def __init__(
        self,
        spec: PackFormatSpec,
        *,
        compression: int,
        compressionLevel: int = 0,
        dlcAppId: int = 0,
        bStripDebugStrings: bool = True,
    ) -> None:
        self._spec = spec
        self._compression = compression
        self._compressionLevel = compressionLevel
        self._dlcAppId = dlcAppId
        self._bStripDebugStrings = bStripDebugStrings
        self._listEntry: list[tuple[int, str, Path | bytes]] = []

    def addFile(self, relPath: str, sourcePath: Path) -> None:
        """디스크의 파일 하나를 담습니다 (읽기는 `writeTo` 에서 한 번만 한다)."""
        self._listEntry.append((self._spec.hashPath(relPath), relPath, sourcePath))

    def addBytes(self, relPath: str, data: bytes) -> None:
        """디스크에 없는 항목을 담습니다 (에셋 레지스트리처럼 쿠커가 만든 바이트)."""
        self._listEntry.append((self._spec.hashPath(relPath), relPath, data))

    @property
    def entryCount(self) -> int:
        return len(self._listEntry)

    def writeTo(self, outPackPath: Path) -> int:
        """담긴 항목으로 팩을 굽고 만들어진 파일 크기를 돌려줍니다."""
        spec = self._spec

        # 리더는 경로 해시로 TOC 를 이진 탐색한다 — 반드시 해시 오름차순이다.
        self._listEntry.sort(key=lambda entry: entry[0])

        tocStartOffset = spec.alignOffset(spec.header.size)
        tocTotalSize = len(self._listEntry) * spec.entry.size
        dataStartOffset = spec.alignOffset(tocStartOffset + tocTotalSize)

        stringPool = bytearray()
        listTocRecord: list[bytes] = []
        listDataBlob: list[bytes] = []
        dataOffset = dataStartOffset

        for pathHash, relPath, source in self._listEntry:
            stringOffset = 0
            if not self._bStripDebugStrings:
                stringOffset = len(stringPool)
                stringPool.extend(relPath.encode("utf-8") + b"\x00")

            rawBytes = source if isinstance(source, bytes) else source.read_bytes()

            # 압축 코덱은 팩 단위다(계약 파일 compression.scope="pack"). 리더가 헤더의 코덱
            # 하나로 모든 항목을 해제하므로, 압축 이득이 없는 파일도 같은 코덱으로 넣어야 한다.
            payload = compressPayloadInternal(rawBytes, self._compression, self._compressionLevel)

            alignedOffset = spec.alignOffset(dataOffset)
            if alignedOffset > dataOffset:
                listDataBlob.append(b"\x00" * (alignedOffset - dataOffset))
            dataOffset = alignedOffset

            listTocRecord.append(
                spec.entry.pack(
                    _pathHash=pathHash,
                    _dataOffset=dataOffset,
                    _compressedSize=len(payload),
                    _uncompressedSize=len(rawBytes),
                    _crc32=binascii.crc32(rawBytes) & 0xFFFFFFFF,
                    _stringPoolOffset=stringOffset,
                )
            )
            listDataBlob.append(payload)
            dataOffset += len(payload)

        flags = spec.mapFlag["HasCrc32"]
        stringPoolOffset = 0
        stringPoolSize = 0
        if not self._bStripDebugStrings:
            flags |= spec.mapFlag["HasStringPool"]
            if stringPool:
                stringPoolOffset = spec.alignOffset(dataOffset)
                stringPoolSize = len(stringPool)

        header = spec.header.pack(
            _magic=spec.magic,
            _formatVersion=spec.formatVersion,
            _dlcAppId=self._dlcAppId,
            _compressionType=self._compression,
            _encryptionType=spec.encryptionNone,
            _sectorAlignment=spec.sectorAlignment,
            _flags=flags,
            _fileCount=len(self._listEntry),
            _indexOffset=tocStartOffset,
            _indexSize=tocTotalSize,
            _stringPoolOffset=stringPoolOffset,
            _stringPoolSize=stringPoolSize,
            _totalDataSize=dataOffset - dataStartOffset,
        )

        outPackPath.parent.mkdir(parents=True, exist_ok=True)
        with open(outPackPath, "wb") as packFile:
            packFile.write(header)
            packFile.write(b"\x00" * (tocStartOffset - len(header)))
            for record in listTocRecord:
                packFile.write(record)
            packFile.write(b"\x00" * (dataStartOffset - (tocStartOffset + tocTotalSize)))
            for blob in listDataBlob:
                packFile.write(blob)
            if stringPoolSize:
                packFile.write(b"\x00" * (stringPoolOffset - dataOffset))
                packFile.write(stringPool)

        return outPackPath.stat().st_size


def cookPack(
    sourceDir: Path,
    outPackPath: Path,
    dlcAppId: int = 0,
    compression: int | None = None,
    compressionLevel: int = 0,
    stripDebugStrings: bool = True,
    packConfig: dict | None = None,
    targetRhi: str = "dx12",
    extraEntries: list[tuple[str, bytes]] | None = None,
    stagedDir: Path | None = None,
) -> bool:
    """
    단일 디렉터리 내 에셋들을 .pack 파일로 패킹합니다 — **무엇을 담을지** 고르는 것이 이 함수의 일이고,
    어떤 바이트가 되는지는 `PackWriter` 가 안다.

    extraEntries 는 디스크에 없는 (상대경로, 바이트) 항목, stagedDir 은 쿠커가 산출물을 쓴 스테이징 폴더다 —
    그 안의 파일은 sourceDir 기준과 **같은 상대 경로** 로 들어간다.
    """
    if not sourceDir.is_dir():
        print(f"[CookAssets Error] Source directory does not exist: {sourceDir}", file=sys.stderr)
        return False

    writer = PackWriter(
        _gPackFormat,
        compression=_gPackFormat.codecZlib if compression is None else compression,
        compressionLevel=compressionLevel,
        dlcAppId=dlcAppId,
        bStripDebugStrings=stripDebugStrings,
    )

    staleCooked: list[str] = []
    for filePath in sorted(sourceDir.rglob("*")):
        if not filePath.is_file():
            continue
        rel = filePath.relative_to(sourceDir).as_posix()
        if packConfig and not shouldIncludeFileInternal(rel, packConfig, targetRhi=targetRhi):
            continue
        if isCookedArtifactInternal(rel):
            # 산출물은 이제 스테이징에만 있다. 소스 트리에 남은 것은 옛 쿠킹의 잔재이고, Dev 런타임이 소스가
            # 없을 때 그것으로 물러나 실패를 가리므로 팩에 넣지 않고 이름을 찍어 지우게 한다.
            staleCooked.append(rel)
            continue
        writer.addFile(rel, filePath)

    if staleCooked:
        sample = ", ".join(staleCooked[:3]) + (" ..." if len(staleCooked) > 3 else "")
        print(f"[CookAssets Warning] {sourceDir.name}: 소스 트리에 낡은 쿠킹 산출물 {len(staleCooked)}개 ({sample}) - 지우십시오. 팩에는 넣지 않습니다.", file=sys.stderr)

    if stagedDir is not None and stagedDir.is_dir():
        for filePath in sorted(stagedDir.rglob("*")):
            if filePath.is_file():
                writer.addFile(filePath.relative_to(stagedDir).as_posix(), filePath)

    for rel, data in extraEntries or []:
        writer.addBytes(rel, data)

    fileCount = writer.entryCount
    packSize = writer.writeTo(outPackPath)
    print(f"[PackCooker] Cooked {outPackPath.name} ({fileCount} files, {packSize:,} bytes, DLC: {dlcAppId})")
    return True


def cookAllPacks(
    projectRoot: Path,
    outputDir: Path,
    isShipping: bool = True,
    packConfig: dict | None = None,
    targetRhi: str = "dx12",
    cookedDir: Path | None = None,
) -> bool:
    """engine, common, 그리고 game 에셋 디렉터리들을 일괄 패킹합니다. cookedDir 은 프리팹·씬 산출물의 스테이징 루트다."""
    resourceDir = projectRoot / "Resource"
    cookedDir = cookedDir or resolveDefaultOutputDir(projectRoot, "Cooked")
    outputDir.mkdir(parents=True, exist_ok=True)
    allSuccess = True

    # 압축 코덱은 PackConfig 가 정한다(설치가 필요한 코덱은 모듈이 없으면 여기서 멈춘다).
    packCompression, packCompressionLevel = resolveCompressionCodecInternal(packConfig)
    codecName = _gPackFormat.codecNameOf(packCompression)
    print(f"[PackCooker] 압축 코덱: {codecName} (level {packCompressionLevel})")

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
        registry = buildAssetRegistryInternal(src)
        extra = [(_kAssetRegistryFileName, registry)] if registry else None
        staged = cookedDir / src.relative_to(resourceDir)
        success = cookPack(src, out, dlcAppId=dlcId, compression=packCompression, compressionLevel=packCompressionLevel,
                           stripDebugStrings=isShipping, packConfig=packConfig, targetRhi=targetRhi,
                           extraEntries=extra, stagedDir=staged)
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
    parser.add_argument("--cooked-dir", type=str, default="", help="프리팹·씬 쿠킹 산출물 스테이징 디렉터리 (기본: build/*/Bin/Cooked)")
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

    cookedDir = Path(args.cooked_dir) if args.cooked_dir else resolveDefaultOutputDir(projectRoot, "Cooked")

    exitCode = 0
    if doPrefabs:
        exitCode = cookPrefabs(cookedDir=cookedDir) or exitCode
    if doScenes:
        exitCode = cookScenes(cookedDir=cookedDir) or exitCode
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
        success = cookAllPacks(projectRoot, outDir, isShipping=stripNames, packConfig=packConfig, targetRhi=targetRhi, cookedDir=cookedDir)
        if not success:
            exitCode = 1

    return exitCode


if __name__ == "__main__":
    sys.exit(main())
