#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/generate/CookAssets.py

SW Engine 통합 에셋 쿠커:
  1. Scenes · Prefabs: 엔진(`App --cook-scenes`)이 쿠킹한다 — 리플렉션 · 형식이 엔진 안에 있다.
       Resource/**/*.scene.xml                  -> <cooked-dir>/**/<name>.scene.bin  (SCN1 바이너리)
       Resource/**/*.prefab.xml · *.prefab.json -> <cooked-dir>/**/<name>.prefab.bin (PFB2 바이너리)
     (산출물은 소스 옆이 아니라 스테이징 폴더에 쓰고, 팩에는 같은 상대 경로로 병합한다)
  2. Packs:   Resource/ 폴더 내 에셋을 4KB 섹터 정렬 .pack 아카이브로 패킹 (SWPK)

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
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from common import (
    CookContractSpec,
    PackFormatSpec,
    findAppExecutable,
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
    readJsonDictInternal,
    resolveDefaultOutputDir,
    runSceneCook,
    runShaderCook,
)

# ==============================================================================
# 1. 포맷 매직 및 상수 정의
# ==============================================================================

# ------------------------------------------------------------------------------
# .pack 바이너리 포맷 계약 — Config/Engine/PackFormat.json 이 단일 출처다.
# 레이아웃을 여기서 손으로 들고 있지 않는다 — 쿠커와 C++ 헤더 생성기가 각자 레이아웃을 들면 오프셋이
# 어긋나 리더가 fileCount 를 0 으로 읽는 "빈 팩"이 생긴다. 계약을 읽는 일은 `Scripts/common/PackFormat.py`
# 한 곳이고, 같은 객체를 헤더 생성기(GeneratePackFormat.py)도 쓴다.
# ------------------------------------------------------------------------------
_gPackFormat = PackFormatSpec.load()

# 쿠킹 표 — RHI 백엔드(별칭 · 셰이더 폴더)와 쿡 접미사. Config/Engine/CookContract.json 이 단일 출처이고 C++ 은 같은 파일에서
# 생성한 X-macro 를 읽는다. 쿠커가 표대로 고르는지는 lint/gate/CheckCookContract.py 가 본다.
_gCookContract = CookContractSpec.load()


# ==============================================================================
# 2. Scene · Prefab 쿠커(엔진)
# ==============================================================================

def cookScenes(resourceRoot: Path | None = None, cookedDir: Path | None = None, appExePath: Path | None = None) -> int:
    """씬과 프리팹을 `App.exe --cook-scenes` 로 쿠킹합니다 (씬은 엔티티 상태까지 바이너리로, 프리팹은 XML · JSON 모두).

    **프리팹도 여기다.** 프리팹 바이너리 형식을 쓰는 곳은 엔진(`PrefabAsset::saveToBinaryFile`) 하나다 — 파이썬이 형식을
    따로 들면 엔진과 어긋나고 한쪽 원본 형식(`.prefab.json` 등)을 빠뜨려 배포본에서 스폰이 실패한다. 산출물은 소스 옆이 아니라 스테이징 폴더에 쓴다 — 소스 옆에 두면 `.gitignore` 로 가려야 하고, 소스가 옮겨지거나
    지워진 뒤에도 낡은 .bin 이 남아 Dev 런타임이 그것으로 물러나 실패를 가린다.

    **왜 파이썬이 직접 안 쓰는가.** 파이썬이 쓸 수 있는 것은 엔티티마다 XML 문자열을 그대로 담은 컨테이너뿐이라,
    로드에서 비싼 엔티티 생성 단계(로드의 대부분)를 줄이지 못한다. 상태를 진짜 바이너리로 만들려면
    리플렉션이 필요하고 그것은 엔진 안에만 있으므로, 이 단계는 셰이더 쿠킹과 같은 방식으로
    엔진에 넘긴다. 포맷을 쓰는 곳도 그래서 하나다.
    """
    projectRoot = getProjectRoot()
    cookedDir = cookedDir or resolveDefaultOutputDir(projectRoot, "Cooked")

    if resourceRoot is not None:
        # 엔진 쪽은 마운트된 리소스 루트를 스스로 찾는다 — 부분 트리 쿠킹은 아직 받지 않는다.
        print(f"[CookScenes] resourceRoot={resourceRoot} 는 무시됩니다 (엔진이 리소스 루트를 정합니다).")

    appExe = findAppExecutable(projectRoot, appExePath)
    if appExe is None:
        print("[CookScenes Error] App 실행 파일을 찾지 못해 씬을 쿠킹하지 못했습니다.", file=sys.stderr)
        print("                   씬 쿠킹은 리플렉션이 필요해 엔진 안에서 돕니다 - 먼저 App 을 빌드하세요.", file=sys.stderr)
        if appExePath is not None:
            print(f"                   (--app {appExePath} 가 없습니다 - CookAssets 는 App 뒤에 돌아야 합니다.)", file=sys.stderr)
        return 1

    print(f"[CookScenes] Running headless scene cook: {appExe} --cook-scenes --cooked-dir={cookedDir}")
    completed = runSceneCook(appExe, cookedDir)
    if completed.returncode != 0:
        print(f"[CookScenes Error] 씬 쿠킹이 실패했습니다 (exit {completed.returncode}).", file=sys.stderr)
        return 1
    return 0


# ==============================================================================
# 3. Resource Pack (.pack) 쿠커
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
                "[Pack] LZ4 로 쿠킹하도록 설정돼 있는데 파이썬 lz4 모듈이 없습니다.  py -3 -m pip install lz4"
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
                "[Pack] Zstd 로 쿠킹하도록 설정돼 있는데 파이썬 zstandard 모듈이 없습니다.  py -3 -m pip install zstandard"
            ) from exc
        compressor = zstandard.ZstdCompressor(level=level if level > 0 else 3)
        return compressor.compress(rawBytes)

    if compression == _gPackFormat.codecNone:
        return rawBytes

    raise SystemExit(f"[Pack] 쿠커가 모르는 압축 코덱 값입니다: {compression}")


def resolveTargetRhi(config: dict, cliRhi: str = "", projectRoot: Path | None = None) -> str:
    """타깃 RHI 의 셰이더 폴더를 정합니다: CLI > PackConfig.json > EngineConfig.json > 표의 기본 백엔드. 이름은 표의 별칭으로 푼다."""
    r = ""
    if cliRhi:
        r = cliRhi.strip().lower()
    elif config.get("target_rhi"):
        r = str(config["target_rhi"]).strip().lower()
    elif projectRoot:
        engineCfgPath = projectRoot / kFileRuntimeEngineConfig
        if engineCfgPath.is_file():
            engineCfg = readJsonDictInternal(engineCfgPath, kFileRuntimeEngineConfig)
            r = engineCfg.get("_window", {}).get("_defaultRHI", "").strip().lower()

    backend = _gCookContract.findBackend(r) or _gCookContract.defaultBackend
    return backend.shaderFolder


def cookShadersInternal(projectRoot: Path, appExePath: Path | None = None) -> bool:
    """App --cook-shaders 를 헤드리스 모드로 실행하여 바이너리를 일괄 빌드합니다."""
    appExe = findAppExecutable(projectRoot, appExePath)
    if appExe is None:
        print("[CookAssets Warning] App executable not found to run --cook-shaders", file=sys.stderr)
        return False

    print(f"[CookAssets] Running headless shader cook: {appExe} --cook-shaders")
    return runShaderCook(appExe).returncode == 0


_kCookStampFileName = "cook.stamp"
_kCookStampHeader = "SWCOOK 3"
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
    Shipping 빌드가 매번 스탬프를 다시 써서 작업 트리가 더러워집니다. `ShaderCooker::writeCookStamp`
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


_kIncludeRootDomains = ("engine", "common")


def collectStampedSourceHashesInternal(resourceDir: Path, shadersDir: Path) -> dict[str, str]:
    """스탬프가 담아야 할 { 키: hex } 입니다 — 자기 도메인의 .hlsl/.hlsli 와, 다른 include 루트 도메인의 .hlsli.

    셰이더는 자기 폴더 밖 include 루트(engine · common)의 헤더도 include 한다. 다른 도메인 헤더의 키는
    `<도메인>:<상대 경로>` 다. `ShaderCookStamp.cpp` 의 `collectStampedSources` 와 같은 규칙이다.
    """
    result = collectShaderSourceHashesInternal(shadersDir)
    for domain in _kIncludeRootDomains:
        rootDir = resourceDir / domain / "shaders"
        if not rootDir.is_dir() or rootDir.resolve() == shadersDir.resolve():
            continue
        for rel, digest in collectShaderSourceHashesInternal(rootDir).items():
            if rel.endswith(".hlsli"):
                result[f"{domain}:{rel}"] = digest
    return result


def verifyShaderCookInternal(projectRoot: Path, targetRhi: str) -> list[str]:
    """쿠킹해 둔 셰이더가 **지금 소스에서 나온 것인지** 확인하고 문제 목록을 돌려줍니다.

    파일 시간이 아니라 cook.stamp 의 내용 해시로 본다 — git clone 은 모든 파일의 mtime 을
    체크아웃 시각으로 덮어써서 시간 비교가 무의미하다. Shipping 은 런타임 컴파일이 없어
    쿠킹된 것이 낡으면 화면이 통째로 비므로, 여기서 막지 못하면 그대로 배포된다.
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

        if not collectShaderSourceHashesInternal(shadersDir):
            continue
        expected = collectStampedSourceHashesInternal(resourceDir, shadersDir)

        label = f"{domainDir.name}/shaders"
        binDir = shadersDir / "bin" / targetRhi
        if not binDir.is_dir():
            problems.append(f"{label}: '{targetRhi}' 바이너리 폴더가 없습니다 (한 번도 쿠킹하지 않았습니다)")
            continue

        stampPath = binDir / _kCookStampFileName
        if not stampPath.is_file():
            problems.append(f"{label}: {_kCookStampFileName} 이 없습니다 (쿠커가 남기는 파일입니다)")
            continue

        lines = stampPath.read_text(encoding="utf-8").splitlines()
        if not lines or lines[0].strip() != _kCookStampHeader:
            problems.append(f"{label}: {_kCookStampFileName} 형식을 알 수 없습니다")
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
                problems.append(f"{label}/{rel}: 쿠킹한 적 없는 셰이더입니다")
            elif stamped[rel] != digest:
                problems.append(f"{label}/{rel}: 소스가 바뀌었는데 다시 쿠킹하지 않았습니다")
        for rel in sorted(set(stamped) - set(expected)):
            problems.append(f"{label}/{rel}: 사라진 셰이더가 스탬프에 남아 있습니다")

        if not any(child.suffix.lower() in (".dxil", ".dxbc", ".spv") for child in binDir.iterdir()):
            problems.append(f"{label}: '{targetRhi}' 바이너리가 하나도 없습니다")
        manifestPath = binDir / "reflection.manifest"
        if not manifestPath.is_file():
            problems.append(f"{label}: '{targetRhi}' 리플렉션 매니페스트가 없습니다")
        else:
            # 바이너리는 있는데 매니페스트에 그 키가 없는 조합을 잡는다. 스탬프는 **소스**가 바뀌었는지만 보므로
            # 쿠커의 레시피 목록이 늘어난 경우(새 퍼뮤테이션 축)는 여기가 아니면 못 잡는다 — 실제로 Unlit x
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
                problems.append(f"{label}: '{targetRhi}' 매니페스트에 없는 바이너리 {len(uncovered)}개 ({sample}): 다시 쿠킹하거나 낡은 파일을 지우십시오")

    return problems


def isCookedArtifact(relPath: str) -> bool:
    """쿠커가 만드는 산출물 이름인가 — 표의 쿠킹본 접미사(`.scene.bin` · `.prefab.bin`). 소스 트리에서 보이면 잔재다."""
    normRel = normalizePath(relPath).lower()
    return any(normRel.endswith(cooked) for cooked in _gCookContract.listCookedSuffix)


def shouldIncludeFile(relPath: str, config: dict, targetRhi: str = "dx12") -> bool:
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
        for rhiFolder in _gCookContract.listShaderFolder:
            if rhiFolder in dirParts and rhiFolder != targetRhi:
                return False

    return True


_kAssetRegistryFileName = "assetregistry.txt"


def hasStagedAssetRegistryInternal(stagedDir: Path | None) -> bool:
    """엔진 쿠킹 단계(App --cook-scenes)가 이 도메인의 GUID 레지스트리를 스테이징했는가.

    레지스트리는 엔진이 쓴다(`AssetDatabase::writeRegistryFiles`) — 경로는 `.meta` 가 놓인 자리이고, Dev 런타임과 같은 규칙이다.
    주의: `.meta` 안에 적힌 경로로 만들면 탐색기 · git 으로 옮긴 에셋이 배포본에서만 옛 경로를 가리킨다.
    """
    return stagedDir is not None and (stagedDir / _kAssetRegistryFileName).is_file()


class PackWriter:
    """
    팩 하나를 쌓아 올리는 동안의 상태 — 담긴 항목 · 데이터 커서 · TOC · 스트링 풀.

    네 상태를 한 함수가 루프 여러 개로 나눠 굴리면 "루프들이 같은 순서로 돈다" 는 전제가 코드에 안 보이게 숨는다.

    여기서는 순서가 메서드 이름이다: `add*` 로 담고 `writeTo` 로 쿠킹한다. 스트링 풀도 TOC 와 같은
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
        """담긴 항목으로 팩을 쿠킹하고 만들어진 파일 크기를 돌려줍니다."""
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
        if packConfig and not shouldIncludeFile(rel, packConfig, targetRhi=targetRhi):
            continue
        if isCookedArtifact(rel):
            # 산출물은 스테이징에만 있다. 소스 트리에 남은 것은 낡은 쿠킹 잔재이고, Dev 런타임이 소스가
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
        staged = cookedDir / src.relative_to(resourceDir)
        if any(src.rglob("*.meta")) and not hasStagedAssetRegistryInternal(staged):
            # 레지스트리가 없으면 배포본의 GUID 참조가 전부 경로 폴백으로 간다(이름을 바꾼 에셋은 배포본에서만 못 찾는다).
            print(f"[CookAssets Warning] {src.name}: 스테이징에 {_kAssetRegistryFileName} 이 없습니다 - 엔진 쿠킹 단계(--all 또는 --prefabs-only)를 먼저 돌리십시오.",
                  file=sys.stderr)
        success = cookPack(src, out, dlcAppId=dlcId, compression=packCompression, compressionLevel=packCompressionLevel,
                           stripDebugStrings=isShipping, packConfig=packConfig, targetRhi=targetRhi,
                           stagedDir=staged)
        if not success:
            allSuccess = False

    return allSuccess


# ==============================================================================
# 5. 메인 통합 실행 진입점
# ==============================================================================

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="SW Engine 통합 에셋 쿠커 (Prefabs, Scenes, Packs)")
    parser.add_argument("--all", action="store_true", help="프리팹, 씬, 리소스 팩 전체를 순서대로 쿠킹 (기본 동작)")
    parser.add_argument("--prefabs-only", action="store_true", help="프리팹 · 씬 바이너리만 쿠킹(같은 엔진 실행이 둘 다 쿠킹한다)")
    parser.add_argument("--scenes-only", action="store_true", help="씬 바이너리(.scene.bin)만 쿠킹")
    parser.add_argument("--packs-only", action="store_true", help="리소스 팩(.pack)만 쿠킹")
    parser.add_argument("--output", type=str, default="", help="팩 출력 디렉터리")
    parser.add_argument("--cooked-dir", type=str, default="", help="프리팹·씬 쿠킹 산출물 스테이징 디렉터리 (기본: build/*/Bin/Cooked)")
    parser.add_argument("--config", type=str, default="", help="PackConfig.json 경로")
    parser.add_argument("--include-debug-names", action="store_true", help="팩 내부에 파일 경로 디버그 문자열 포함")
    parser.add_argument("--target-rhi", type=str, default="", help=f"타깃 RHI 백엔드 ({', '.join(backend.name for backend in _gCookContract.listBackend)} 또는 그 별칭)")
    parser.add_argument("--cook-shaders", action="store_true", help="패킹 전 App.exe --cook-shaders 를 실행하여 셰이더 일괄 사전 빌드")
    parser.add_argument("--verify-shaders", action="store_true", help="쿠킹된 셰이더가 현재 소스에서 나온 것인지 확인하고, 아니면 쿠킹을 중단")
    parser.add_argument("--app", type=str, default="", help="씬 쿠킹·셰이더 쿠킹에 쓸 App 실행 파일 (CMake 가 $<TARGET_FILE:App> 을 넘긴다; 없으면 빌드 폴더를 뒤진다)")

    args = parser.parse_args(argv)
    projectRoot = getProjectRoot()
    appExePath = Path(args.app) if args.app else None

    if args.cook_shaders:
        cookShadersInternal(projectRoot, appExePath)

    doPrefabs = args.prefabs_only or (not args.scenes_only and not args.packs_only)
    doScenes = args.scenes_only or (not args.prefabs_only and not args.packs_only)
    doPacks = args.packs_only or (not args.prefabs_only and not args.scenes_only)

    cookedDir = Path(args.cooked_dir) if args.cooked_dir else resolveDefaultOutputDir(projectRoot, "Cooked")

    exitCode = 0
    # 씬과 프리팹은 같은 엔진 실행이 쿠킹한다.
    if doPrefabs or doScenes:
        exitCode = cookScenes(cookedDir=cookedDir, appExePath=appExePath) or exitCode
    if doPacks:
        stripNames = not args.include_debug_names
        configPath = Path(args.config) if args.config else (projectRoot / kFilePackConfig)
        packConfig = readJsonDictInternal(configPath, kFilePackConfig)
        if not packConfig:
            print(f"[CookAssets Error] missing or invalid config: {configPath}", file=sys.stderr)
            return 1
        targetRhi = resolveTargetRhi(packConfig, cliRhi=args.target_rhi, projectRoot=projectRoot)
        print(f"[CookAssets] Target RHI for shader packaging: {targetRhi}")

        if args.verify_shaders:
            problems = verifyShaderCookInternal(projectRoot, targetRhi)
            # 낡았을 뿐이라면 쿠커가 있는 자리에서는 스스로 고친다 — 로컬 Shipping 빌드가
            # 셰이더 한 줄 고칠 때마다 손으로 --cook-shaders 를 부르라고 요구할 이유는 없다.
            if problems and not args.cook_shaders and cookShadersInternal(projectRoot, appExePath):
                problems = verifyShaderCookInternal(projectRoot, targetRhi)
            if problems:
                print("[CookAssets Error] 쿠킹해 둔 셰이더가 현재 소스와 맞지 않습니다.", file=sys.stderr)
                print("                   배포 빌드는 런타임 컴파일이 없어 이대로 패킹하면 화면이 비게 됩니다.", file=sys.stderr)
                for problem in problems[:20]:
                    print(f"                   - {problem}", file=sys.stderr)
                if len(problems) > 20:
                    print(f"                   ... 외 {len(problems) - 20}건", file=sys.stderr)
                print("                   해결: build/Ninja-Debug/Bin/App.exe --cook-shaders", file=sys.stderr)
                return 1
        outDir = Path(args.output) if args.output else resolveDefaultOutputDir(projectRoot, "Packs")
        success = cookAllPacks(projectRoot, outDir, isShipping=stripNames, packConfig=packConfig, targetRhi=targetRhi, cookedDir=cookedDir)
        if not success:
            exitCode = 1

    return exitCode


if __name__ == "__main__":
    sys.exit(main())
