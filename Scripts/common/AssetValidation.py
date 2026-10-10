#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
에셋 검증 규칙 — `Config/Editor/AssetValidationRules.json` 의 규칙 표를 `Resource/` 에 돌려 (파일, 규칙, 심각도, 메시지) 목록을 만든다.

**규칙은 데이터, 검사는 이름으로 고르는 연산자다.** 표의 줄 하나가 규칙 하나이고 `check` 이름이 아래 `_kCheckRegistry` 의 연산자를
고른다. 모르는 `check` · 모르는 키는 규칙 표를 읽는 단계의 오류다(조용히 안 도는 규칙이 없게). 언리얼 Data Validation 의
`UEditorValidatorBase` 하나 = 여기 연산자 하나, 유니티 Asset Validation 의 규칙 자산 = 여기 표 한 줄에 해당한다.

**왜 파이썬인가(App 이 아니라).** 이 검사들은 모두 텍스트 · 파일 머리 수준이다(XML 속성 · DDS 머리 · `.mesh` 머리 · `.meta`).
그래서 빌드 없이 커밋 훅 · CI(nogpu) 에서 돌고, 활성 게임 하나의 타입만 등록되는 App 과 달리 **일곱 게임 팩을 모두** 본다.
실제 로더로 끝까지 읽어 보는 검사(모르는 키 · 타입 · 열거값)는 이미 `ResourceDataSchemaTest` 가 하므로 여기서 다시 하지 않는다.
에디터는 저장 · 임포트 직후 같은 스크립트를 그 파일 하나에 부른다(`EditorAssetValidation`).

검사 목록(`check` 이름):
  naming               파일 이름 정규식 · 폴더별 허용 접미사
  texture              DDS 머리 — 최대 크기 · 2 의 거듭제곱 · 밉 유무 · 허용 형식
  mesh_budget          `.mesh` 머리 — 삼각형 수 · 경계 반지름 예산
  xml_wellformed       XML 이 파싱되는가
  missing_reference    XML 안의 리소스 경로(`engine/…` · `game/<팩>/…`)가 실제로 있는가
  orphan               아무 데서도(리소스 · 소스 · 설정) 이름이 불리지 않는 파일
  material             셰이더 경로 · 블렌드 모드 · 정적 스위치 중복 · 키워드 프로퍼티와 스위치 대응 · 멀티 컴파일 선택값
  material_keywords    정적 스위치 키워드가 어느 셰이더 소스에라도 나오는가(안 나오면 죽은 퍼뮤테이션)
  component_type       씬 · 프리팹 컴포넌트가 소스의 `REFLECT` 타입인가(일곱 게임 팩 전부)
  entity_id            씬 엔티티 id 가 0 이 아니고 겹치지 않으며 `_attachOwnerID` 가 있는 엔티티를 가리키는가
  prefab_guid          프리팹 인스턴스의 `prefabGuid` 가 그 프리팹 `.meta` 의 guid 인가 · `.meta` 의 sourcePath · guid 중복
                       (`stale_path_only` 면 guid 로는 열리지만 경로가 낡은 인스턴스만 알린다)
  catalog_reference    카탈로그(팩 데이터) 안의 id 참조가 같은(또는 지정한) 파일의 id 를 가리키는가 — 팩마다 규칙을 둔다
"""

from __future__ import annotations

import fnmatch
import json
import re
import struct
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterator

#: 심각도 — 앞일수록 무겁다. 게이트는 `error` 만 막는다.
kSeverityOrder: tuple[str, ...] = ("error", "warning", "info")

#: 리소스 경로로 보는 값 — 도메인으로 시작하고 확장자가 있다(`engine/shaders/forwardlit.hlsl`).
_kResourcePathRe = re.compile(r"^(engine|common|editor|game/[a-z0-9_]+)/[A-Za-z0-9_./\-]+\.[A-Za-z0-9]+$")

#: 소스 · 설정 · 리소스 텍스트에서 리소스 이름을 줍는 패턴(고아 판정용) — 경로든 파일 이름만이든 확장자로 끝나는 토큰.
#: 덩어리(`[A-Za-z0-9_./\-]+`)의 **첫 글자에서만** 시작한다(앞 뒤보기) — 없으면 덩어리 안 모든 자리에서 끝까지 먹고 되돌아온다.
#: 덩어리 안에서는 첫 글자에서 시작한 맞춤이 가장 왼쪽이고 끝이 같으므로 찾는 토큰은 같다.
_kReferenceTokenRe = re.compile(
    r"(?<![A-Za-z0-9_./\-])[A-Za-z0-9_./\-]+\.(?:mesh|material|prefab\.xml|scene\.xml|dds|ogg|wav|xml|json|hlsl|hlsli)\b")
#: 위 토큰이 반드시 품는 꼬리 — 이것이 없는 글(대부분의 .h · .cpp)에는 위 정규식을 부르지 않는다.
_kReferenceExtensionRe = re.compile(r"\.(?:mesh|material|prefab\.xml|scene\.xml|dds|ogg|wav|xml|json|hlsl|hlsli)\b")
#: 셰이더 글의 낱말.
_kWordRe = re.compile(r"\w+")

#: `REFLECT(` 뒤의 첫 class/struct 이름.
#: 인자 안의 문자열(`Tooltip = "… (…) …"`)에 든 괄호는 건너뛴다.
_kReflectTypeRe = re.compile(r'REFLECT\s*\((?:[^()"]|"[^"]*")*\)\s*(?:class|struct)\s+(?:SW_[A-Z_]+\s+)?([A-Za-z_][A-Za-z0-9_]*)')

#: DXGI 형식 번호 → 이름(검증에 쓰는 것만). 레거시 DDS 는 FourCC · 마스크에서 이름을 만든다.
_kDxgiFormatName: dict[int, str] = {
    2: "R32G32B32A32_FLOAT", 10: "R16G16B16A16_FLOAT", 24: "R10G10B10A2_UNORM", 28: "R8G8B8A8_UNORM", 29: "R8G8B8A8_UNORM_SRGB",
    61: "R8_UNORM", 71: "BC1_UNORM", 72: "BC1_UNORM_SRGB", 74: "BC2_UNORM", 75: "BC2_UNORM_SRGB", 77: "BC3_UNORM", 78: "BC3_UNORM_SRGB",
    80: "BC4_UNORM", 83: "BC5_UNORM", 87: "B8G8R8A8_UNORM", 91: "B8G8R8A8_UNORM_SRGB", 95: "BC6H_UF16", 98: "BC7_UNORM", 99: "BC7_UNORM_SRGB",
}
_kLegacyFourCcName: dict[bytes, str] = {
    b"DXT1": "BC1_UNORM", b"DXT3": "BC2_UNORM", b"DXT5": "BC3_UNORM", b"ATI2": "BC5_UNORM", b"BC5U": "BC5_UNORM",
    struct.pack("<I", 113): "R16G16B16A16_FLOAT", struct.pack("<I", 116): "R32G32B32A32_FLOAT",
}


class RuleConfigError(ValueError):
    """규칙 표를 읽을 수 없다(모르는 검사 · 모르는 키 · 틀린 값)."""


@dataclass(frozen=True)
class Finding:
    """검사 결과 한 줄 — 파일(리소스 루트 기준) · 규칙 이름 · 심각도 · 메시지."""

    path: str
    rule: str
    severity: str
    message: str

    def format(self) -> str:
        return f"{self.severity.upper():7} {self.path}: [{self.rule}] {self.message}"

    def toJSON(self) -> dict:
        return {"path": self.path, "rule": self.rule, "severity": self.severity, "message": self.message}


@dataclass
class Rule:
    """규칙 표의 한 줄."""

    name: str
    check: str
    severity: str
    listIncludePattern: list[str]
    listExcludePattern: list[str]
    params: dict
    excludeReason: str = ""  # 제외가 있으면 그 까닭 — 이유 없는 제외는 규칙 표를 읽지 못한다

    def matches(self, relPath: str) -> bool:
        if not any(fnmatch.fnmatchcase(relPath, pattern) for pattern in self.listIncludePattern):
            return False
        return not any(fnmatch.fnmatchcase(relPath, pattern) for pattern in self.listExcludePattern)


@dataclass
class ValidationContext:
    """검사들이 함께 쓰는 트리 색인 — 처음 필요할 때 한 번만 만든다."""

    resourceRoot: Path
    repositoryRoot: Path
    listAllPath: list[str]
    _uniquePath: set[str] | None = None
    _uniqueReflectedType: set[str] | None = None
    _uniqueReferencedName: set[str] | None = None
    _mapXmlCache: dict[str, ElementTree.Element | None] = field(default_factory=dict)
    _mapGuidByPath: dict[str, str] | None = None
    _shaderText: str | None = None
    _uniqueShaderWord: set[str] | None = None

    def hasResource(self, relPath: str) -> bool:
        # 리소스 id 는 찾을 때 소문자로 맞춘다(`ResourceUtil::normalizePath`) — 비교도 소문자로 한다.
        if self._uniquePath is None:
            self._uniquePath = {path.lower() for path in self.listAllPath}
        return relPath.lower() in self._uniquePath

    def readXml(self, relPath: str) -> ElementTree.Element | None:
        """XML 을 한 번만 파싱합니다. 파싱하지 못하면 None 입니다(그 보고는 `xml_wellformed` 의 몫이다)."""
        if relPath not in self._mapXmlCache:
            try:
                self._mapXmlCache[relPath] = ElementTree.parse(self.resourceRoot / relPath).getroot()
            except (ElementTree.ParseError, OSError):
                self._mapXmlCache[relPath] = None
        return self._mapXmlCache[relPath]

    def getReflectedTypes(self) -> set[str]:
        """`Source/**/*.h` 의 `REFLECT` 타입 이름 — 일곱 게임 팩의 타입까지 빌드 없이 안다."""
        if self._uniqueReflectedType is None:
            self._uniqueReflectedType = set()
            for headerPath in (self.repositoryRoot / "Source").rglob("*.h"):
                text = headerPath.read_text(encoding="utf-8", errors="replace")
                if "REFLECT" in text:
                    self._uniqueReflectedType.update(_kReflectTypeRe.findall(text))
        return self._uniqueReflectedType

    def getReferencedNames(self) -> set[str]:
        """리소스 · 소스 · 설정 · 테스트 텍스트에 나오는 리소스 토큰들(전체 경로와 파일 이름 둘 다 넣는다)."""
        if self._uniqueReferencedName is None:
            unique: set[str] = set()
            listRoot = [self.resourceRoot, self.repositoryRoot / "Source", self.repositoryRoot / "Config", self.repositoryRoot / "Test"]
            listSuffix = (".xml", ".material", ".json", ".h", ".cpp", ".xxx", ".hlsl", ".hlsli", ".txt", ".md")
            for root in listRoot:
                if not root.is_dir():
                    continue
                for path in root.rglob("*"):
                    if path.suffix.lower() not in listSuffix or not path.is_file():
                        continue
                    text = path.read_text(encoding="utf-8", errors="replace")
                    if _kReferenceExtensionRe.search(text) is None:
                        continue
                    for token in _kReferenceTokenRe.findall(text):
                        token = token.lower().lstrip("./")
                        unique.add(token)
                        unique.add(token.rsplit("/", 1)[-1])
            self._uniqueReferencedName = unique
        return self._uniqueReferencedName

    def getGuidByPath(self) -> dict[str, str]:
        """`.meta` 들의 guid(리소스 경로 → guid)."""
        if self._mapGuidByPath is None:
            self._mapGuidByPath = {}
            for relPath in self.listAllPath:
                if relPath.endswith(".meta"):
                    meta = readMetaFile(self.resourceRoot / relPath)
                    if "guid" in meta:
                        self._mapGuidByPath[relPath[:-len(".meta")]] = meta["guid"]
        return self._mapGuidByPath

    def getShaderText(self) -> str:
        """모든 셰이더 소스(.hlsl · .hlsli)를 이어 붙인 것 — 키워드가 어딘가의 `#if` 에 쓰이는지 본다."""
        if self._shaderText is None:
            listText = []
            for relPath in self.listAllPath:
                if relPath.endswith((".hlsl", ".hlsli")):
                    listText.append((self.resourceRoot / relPath).read_text(encoding="utf-8", errors="replace"))
            self._shaderText = "\n".join(listText)
        return self._shaderText

    def getShaderWords(self) -> set[str]:
        """셰이더 소스에 나오는 낱말(`\\w+`) — 키워드가 쓰이는지 정규식 검색 대신 집합으로 묻는다."""
        if self._uniqueShaderWord is None:
            self._uniqueShaderWord = set(_kWordRe.findall(self.getShaderText()))
        return self._uniqueShaderWord


def readMetaFile(path: Path) -> dict[str, str]:
    """`key=value` 줄로 된 `.meta` 를 읽습니다(# 주석 · 빈 줄은 넘깁니다)."""
    result: dict[str, str] = {}
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return result
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        result[key.strip()] = value.strip()
    return result


# ------------------------------------------------------------------------------
# 파일 머리 읽기
# ------------------------------------------------------------------------------
@dataclass(frozen=True)
class DdsInfo:
    width: int
    height: int
    mipCount: int
    formatName: str


def readDdsInfo(data: bytes) -> DdsInfo:
    """DDS 머리(+ DX10 머리)에서 크기 · 밉 수 · 형식 이름을 읽습니다. 잘렸거나 매직이 다르면 ValueError 입니다."""
    if len(data) < 128 or data[:4] != b"DDS ":
        raise ValueError("not a DDS file (magic or header size)")
    height, width = struct.unpack_from("<II", data, 12)
    mipCount = max(1, struct.unpack_from("<I", data, 28)[0])
    pixelFlags = struct.unpack_from("<I", data, 80)[0]
    fourCc = data[84:88]
    if fourCc == b"DX10":
        if len(data) < 148:
            raise ValueError("DX10 header is truncated")
        dxgiFormat = struct.unpack_from("<I", data, 128)[0]
        formatName = _kDxgiFormatName.get(dxgiFormat, f"DXGI_{dxgiFormat}")
    elif pixelFlags & 0x4:  # DDPF_FOURCC
        formatName = _kLegacyFourCcName.get(fourCc, f"FOURCC_{fourCc!r}")
    else:
        bitCount = struct.unpack_from("<I", data, 88)[0]
        formatName = f"UNCOMPRESSED_{bitCount}BPP"
    return DdsInfo(width, height, mipCount, formatName)


@dataclass(frozen=True)
class MeshInfo:
    vertexCount: int
    triangleCount: int
    boundingRadius: float


# `MeshAssetFormat.h` 와 같은 값 — 판이 바뀌면 함께 바꾼다.
_kMeshVersion = 2
_kMeshHeaderSize = 24
_kMeshSkinVertexSize = 24
_kMeshChunkTags = (b"MRPH",)


def readMeshInfo(data: bytes) -> MeshInfo:
    """`.mesh` 머리(`MeshAssetFormat` — SWMS · 버전 · 정점 수 · 정점 크기 · 경계 반지름 · 스킨 본 수)를 읽습니다. 스킨이 있으면 정점마다 스킨 칸이 뒤에 붙습니다."""
    if len(data) < _kMeshHeaderSize or data[:4] != b"SWMS":
        raise ValueError("not a .mesh file (magic or header size)")
    version, vertexCount, vertexSize, boundingRadius, skinBoneCount = struct.unpack_from("<IIIfI", data, 4)
    if version != _kMeshVersion:
        raise ValueError(f"mesh version {version} is not the current {_kMeshVersion} - re-import the model")
    if vertexCount == 0 or vertexCount % 3 != 0:
        raise ValueError(f"vertex count {vertexCount} is not a non-zero multiple of 3")
    skinSize = vertexCount * _kMeshSkinVertexSize if skinBoneCount > 0 else 0
    baseSize = _kMeshHeaderSize + vertexCount * vertexSize + skinSize
    if len(data) < baseSize:
        raise ValueError(f"file size {len(data)} does not match the header ({vertexCount} x {vertexSize} bytes + {skinSize} skin bytes + {_kMeshHeaderSize})")
    # 정점 뒤의 선택 덩어리(태그 4 바이트 + 길이 uint32 + 본문 — 지금은 모프 `MRPH`)가 파일 끝에 꼭 맞아야 한다.
    cursor = baseSize
    while cursor < len(data):
        if len(data) - cursor < 8:
            raise ValueError(f"truncated chunk header at byte {cursor}")
        tag = data[cursor:cursor + 4]
        (chunkLength,) = struct.unpack_from("<I", data, cursor + 4)
        if tag not in _kMeshChunkTags or cursor + 8 + chunkLength > len(data):
            raise ValueError(f"unknown or truncated chunk {tag!r} at byte {cursor}")
        cursor += 8 + chunkLength
    return MeshInfo(vertexCount, vertexCount // 3, boundingRadius)


# ------------------------------------------------------------------------------
# 검사 연산자 — 이름 하나 = 함수 하나. 시그니처: (rule, relPath, context) -> 메시지 목록
# ------------------------------------------------------------------------------
CheckFunction = Callable[[Rule, str, ValidationContext], list[str]]


def checkNamingInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    listMessage: list[str] = []
    fileName = relPath.rsplit("/", 1)[-1]
    pattern = rule.params.get("name_pattern")
    if pattern and not re.fullmatch(pattern, fileName):
        listMessage.append(f"file name '{fileName}' does not match {pattern}")
    listSuffix = rule.params.get("allowed_suffixes")
    if listSuffix and not any(fileName.endswith(suffix) for suffix in listSuffix):
        listMessage.append(f"'{fileName}' does not belong here (allowed: {', '.join(listSuffix)})")
    return listMessage


def isPowerOfTwoInternal(value: int) -> bool:
    return value > 0 and (value & (value - 1)) == 0


def checkTextureInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    try:
        info = readDdsInfo((context.resourceRoot / relPath).read_bytes()[:148])
    except (ValueError, OSError) as error:
        return [str(error)]
    listMessage: list[str] = []
    maxSize = rule.params.get("max_size")
    if maxSize and max(info.width, info.height) > maxSize:
        listMessage.append(f"{info.width}x{info.height} exceeds the {maxSize} limit")
    if rule.params.get("power_of_two") and not (isPowerOfTwoInternal(info.width) and isPowerOfTwoInternal(info.height)):
        listMessage.append(f"{info.width}x{info.height} is not a power of two")
    requireMips = rule.params.get("require_mips")
    fullMipCount = max(info.width, info.height).bit_length()
    if requireMips is True and info.mipCount < fullMipCount:
        listMessage.append(f"has {info.mipCount} mip level(s), expected the full chain ({fullMipCount})")
    if requireMips is False and info.mipCount > 1:
        listMessage.append(f"has {info.mipCount} mip levels, this folder expects none")
    listFormat = rule.params.get("allowed_formats")
    if listFormat and info.formatName not in listFormat:
        listMessage.append(f"format {info.formatName} is not allowed here ({', '.join(listFormat)})")
    return listMessage


def checkMeshBudgetInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    try:
        info = readMeshInfo((context.resourceRoot / relPath).read_bytes())
    except (ValueError, OSError) as error:
        return [str(error)]
    listMessage: list[str] = []
    maxTriangle = rule.params.get("max_triangles")
    if maxTriangle and info.triangleCount > maxTriangle:
        listMessage.append(f"{info.triangleCount} triangles exceed the budget of {maxTriangle}")
    maxRadius = rule.params.get("max_bounding_radius")
    if maxRadius and info.boundingRadius > maxRadius:
        listMessage.append(f"bounding radius {info.boundingRadius:.2f} exceeds {maxRadius}")
    return listMessage


def checkXmlWellFormedInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    try:
        ElementTree.parse(context.resourceRoot / relPath)
    except ElementTree.ParseError as error:
        return [f"XML does not parse: {error}"]
    return []


def iterateXmlValuesInternal(root: ElementTree.Element) -> Iterator[tuple[ElementTree.Element, str | None, str]]:
    """(요소, 속성 이름 또는 None, 값) — 속성 값과 요소 본문 텍스트를 모두 냅니다."""
    for element in root.iter():
        for name, value in element.attrib.items():
            yield element, name, value
        if element.text and element.text.strip():
            yield element, None, element.text.strip()


def checkMissingReferenceInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    root = context.readXml(relPath)
    if root is None:
        return []
    listMessage: list[str] = []
    for element, name, value in iterateXmlValuesInternal(root):
        if not _kResourcePathRe.match(value):
            continue
        if element.tag == "entity" and name == "prefab" and element.get("prefabGuid"):
            continue  # 프리팹 인스턴스는 guid 가 정본이다 — 경로가 낡았는지는 `prefab_guid` 가 본다
        if not context.hasResource(value.lower()):
            where = f"{element.tag}@{name}" if name else f"<{element.tag}>"
            listMessage.append(f"{where} points at '{value}', which does not exist")
        elif value != value.lower():
            listMessage.append(f"'{value}' is not lowercase (resource ids are lowercase)")
    return listMessage


def checkOrphanInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    unique = context.getReferencedNames()
    fileName = relPath.rsplit("/", 1)[-1]
    if relPath in unique or fileName in unique:
        return []
    return ["nothing in Resource/, Source/, Config/ or Test/ names this file"]


def checkMaterialInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    root = context.readXml(relPath)
    if root is None:
        return []
    listMessage: list[str] = []
    shaderPath = root.get("shaderPath", "")
    if not shaderPath:
        listMessage.append("has no shaderPath")
    allowedBlend = rule.params.get("allowed_blend_modes")
    if allowedBlend and root.get("blendMode") not in allowedBlend:
        listMessage.append(f"blendMode '{root.get('blendMode')}' is not one of {', '.join(allowedBlend)}")

    permutations = root.find("_permutations")
    listSwitchKeyword: list[str] = []
    if permutations is not None:
        uniqueSwitchName: set[str] = set()
        switches = permutations.find("_staticSwitches")
        for item in (switches.findall("item") if switches is not None else []):
            name, keyword = item.get("name", ""), item.get("keyword", "")
            if name in uniqueSwitchName:
                listMessage.append(f"static switch '{name}' is declared twice")
            uniqueSwitchName.add(name)
            if not keyword:
                listMessage.append(f"static switch '{name}' has no keyword")
                continue
            listSwitchKeyword.append(keyword)
        compiles = permutations.find("_multiCompiles")
        for item in (compiles.findall("item") if compiles is not None else []):
            listOption = [option.text.strip() for option in item.iter("item") if option is not item and option.text]
            selected = item.get("selected")
            if not listOption:
                listMessage.append(f"multi compile '{item.get('name')}' has no options")
            elif selected and selected not in listOption:
                listMessage.append(f"multi compile '{item.get('name')}' selects '{selected}', which is not one of its options")
    elif rule.params.get("require_permutations", False):
        listMessage.append("has no _permutations block (every backend falls back differently without it)")

    properties = root.find("_properties")
    for item in (properties.findall("item") if properties is not None else []):
        if item.get("type") == "Keyword":
            keyword = item.get("shaderKeyword", "")
            if keyword and keyword not in listSwitchKeyword:
                listMessage.append(f"keyword property '{item.get('name')}' drives '{keyword}', which no static switch declares")
    return listMessage


def isShaderWordUsedInternal(keyword: str, context: ValidationContext) -> bool:
    """키워드가 셰이더 글에 낱말로 나오는가. 낱말 글자만이면 집합으로, 아니면(`\\b` 의 뜻이 달라진다) 정규식으로."""
    if _kWordRe.fullmatch(keyword):
        return keyword in context.getShaderWords()
    return re.search(rf"\b{re.escape(keyword)}\b", context.getShaderText()) is not None


def checkMaterialKeywordsInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    """정적 스위치 키워드가 어느 셰이더 소스에도 나오지 않으면 그 스위치는 같은 바이트코드를 두 벌 만들 뿐이다."""
    root = context.readXml(relPath)
    if root is None:
        return []
    listMessage: list[str] = []
    switches = root.find("_permutations/_staticSwitches")
    for item in (switches.findall("item") if switches is not None else []):
        keyword = item.get("keyword")
        if keyword and not isShaderWordUsedInternal(keyword, context):
            listMessage.append(f"static switch '{item.get('name')}' keyword '{keyword}' is not used by any shader (dead permutation)")
    return listMessage


def iterateComponentElementsInternal(root: ElementTree.Element) -> Iterator[ElementTree.Element]:
    for listComponent in root.iter("_listComponent"):
        yield from list(listComponent)


def checkComponentTypeInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    root = context.readXml(relPath)
    if root is None:
        return []
    uniqueType = context.getReflectedTypes()
    listMessage: list[str] = []
    for element in iterateComponentElementsInternal(root):
        if element.tag not in uniqueType:
            listMessage.append(f"component type '{element.tag}' is not a REFLECT type anywhere in Source/")
    return listMessage


def checkEntityIdInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    root = context.readXml(relPath)
    if root is None:
        return []
    listMessage: list[str] = []
    uniqueId: set[str] = set()
    for entity in root.iter("entity"):
        entityID = entity.get("id", "")
        if not entityID.isdigit() or int(entityID) == 0:
            listMessage.append(f"entity '{entity.get('name')}' has id '{entityID}' (needs a non-zero integer)")
        elif entityID in uniqueId:
            listMessage.append(f"entity id {entityID} is used twice")
        uniqueId.add(entityID)
    for entity in root.iter("entity"):
        for element in entity.iter():
            ownerID = element.get("_attachOwnerID")
            if ownerID and ownerID != "0" and ownerID not in uniqueId:
                listMessage.append(f"entity {entity.get('id')} ({element.tag}) attaches to entity {ownerID}, which is not in the scene")
    return listMessage


def checkPrefabGuidInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    listMessage: list[str] = []
    mapGuid = context.getGuidByPath()
    bStalePathOnly = rule.params.get("stale_path_only", False)
    if relPath.endswith(".meta"):
        if bStalePathOnly:
            return listMessage
        meta = readMetaFile(context.resourceRoot / relPath)
        sourcePath = meta.get("sourcePath")
        if sourcePath and sourcePath != relPath[:-len(".meta")]:
            listMessage.append(f"sourcePath '{sourcePath}' does not match the file next to it")
        guid = meta.get("guid", "")
        if not re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", guid):
            listMessage.append(f"guid '{guid}' is not a lowercase UUID")
        listOwner = sorted(path for path, otherGuid in mapGuid.items() if otherGuid == guid)
        if len(listOwner) > 1:
            listMessage.append(f"guid {guid} is shared by {', '.join(listOwner)}")
        if not context.hasResource(relPath[:-len(".meta")]):
            listMessage.append("the asset this .meta describes does not exist")
        return listMessage
    root = context.readXml(relPath)
    if root is None:
        return listMessage
    for entity in root.iter("entity"):
        prefab, guid = entity.get("prefab"), entity.get("prefabGuid")
        if not prefab:
            continue
        owner = next((path for path, otherGuid in mapGuid.items() if otherGuid == guid), None) if guid else None
        if bStalePathOnly:
            # guid 가 정본이라 낡은 경로로도 열린다 — 다시 저장하면 경로가 고쳐진다는 알림이다.
            if owner is not None and not context.hasResource(prefab):
                listMessage.append(f"entity {entity.get('id')} names '{prefab}' but its guid resolves to '{owner}' (resave to update the path)")
            continue
        expected = mapGuid.get(prefab.lower())
        if guid and expected is None and owner is None:
            listMessage.append(f"entity {entity.get('id')} uses prefab '{prefab}' and guid {guid}, and neither is known")
        elif guid and expected is not None and expected != guid:
            owner = next((path for path, otherGuid in mapGuid.items() if otherGuid == guid), None)
            hint = f" (that guid belongs to {owner})" if owner else ""
            listMessage.append(f"entity {entity.get('id')} prefabGuid {guid} does not match {prefab} ({expected}){hint}")
    return listMessage


def splitIdListInternal(value: str) -> list[str]:
    return [token for token in re.split(r"[\s,;]+", value.strip()) if token]


def checkCatalogReferenceInternal(rule: Rule, relPath: str, context: ValidationContext) -> list[str]:
    root = context.readXml(relPath)
    if root is None:
        return []
    targetPath = rule.params.get("target_file", relPath)
    targetRoot = context.readXml(targetPath)
    if targetRoot is None:
        return [f"target catalog '{targetPath}' does not parse or does not exist"]
    idAttribute = rule.params.get("id_attribute", "id")
    listTargetElement = rule.params.get("target_elements") or [element.tag for element in targetRoot]
    uniqueId = {element.get(idAttribute) for element in targetRoot.iter() if element.tag in listTargetElement and element.get(idAttribute)}
    listMessage: list[str] = []
    listSourceElement = rule.params.get("elements")
    for element in root.iter():
        if listSourceElement and element.tag not in listSourceElement:
            continue
        for attribute in rule.params.get("reference_attributes", []):
            value = element.get(attribute)
            if value is None:
                continue
            for token in splitIdListInternal(value):
                if token not in uniqueId:
                    owner = element.get(idAttribute, element.tag)
                    listMessage.append(f"{element.tag} '{owner}' {attribute}='{token}' is not an id in {targetPath}")
    return listMessage


_kCheckRegistry: dict[str, tuple[CheckFunction, frozenset[str]]] = {
    "naming": (checkNamingInternal, frozenset({"name_pattern", "allowed_suffixes"})),
    "texture": (checkTextureInternal, frozenset({"max_size", "power_of_two", "require_mips", "allowed_formats"})),
    "mesh_budget": (checkMeshBudgetInternal, frozenset({"max_triangles", "max_bounding_radius"})),
    "xml_wellformed": (checkXmlWellFormedInternal, frozenset()),
    "missing_reference": (checkMissingReferenceInternal, frozenset()),
    "orphan": (checkOrphanInternal, frozenset()),
    "material": (checkMaterialInternal, frozenset({"allowed_blend_modes", "require_permutations"})),
    "material_keywords": (checkMaterialKeywordsInternal, frozenset()),
    "component_type": (checkComponentTypeInternal, frozenset()),
    "entity_id": (checkEntityIdInternal, frozenset()),
    "prefab_guid": (checkPrefabGuidInternal, frozenset({"stale_path_only"})),
    "catalog_reference": (checkCatalogReferenceInternal,
                          frozenset({"target_file", "id_attribute", "target_elements", "elements", "reference_attributes"})),
}

#: 규칙 줄마다 공통 키.
_kCommonRuleKey = frozenset({"name", "check", "severity", "include_patterns", "exclude_patterns", "exclude_reason", "description"})


def getCheckNames() -> list[str]:
    return sorted(_kCheckRegistry)


def parseRules(data: dict) -> list[Rule]:
    """규칙 표(JSON 객체)를 읽습니다. 모르는 검사 · 키 · 심각도 · 겹친 이름은 RuleConfigError 입니다."""
    if not isinstance(data, dict) or not isinstance(data.get("rules"), list):
        raise RuleConfigError("the rule file needs a top-level 'rules' array")
    listRule: list[Rule] = []
    uniqueName: set[str] = set()
    for index, entry in enumerate(data["rules"]):
        if not isinstance(entry, dict):
            raise RuleConfigError(f"rule #{index} is not an object")
        name, check = entry.get("name"), entry.get("check")
        if not name or name in uniqueName:
            raise RuleConfigError(f"rule #{index} has a missing or repeated name '{name}'")
        uniqueName.add(name)
        if check not in _kCheckRegistry:
            raise RuleConfigError(f"rule '{name}' uses unknown check '{check}' (known: {', '.join(getCheckNames())})")
        severity = entry.get("severity", "error")
        if severity not in kSeverityOrder:
            raise RuleConfigError(f"rule '{name}' has unknown severity '{severity}' ({', '.join(kSeverityOrder)})")
        allowedKey = _kCheckRegistry[check][1]
        params = {key: value for key, value in entry.items() if key not in _kCommonRuleKey}
        unknownKey = sorted(set(params) - allowedKey)
        if unknownKey:
            raise RuleConfigError(f"rule '{name}' ({check}) has unknown key(s) {', '.join(unknownKey)}")
        listInclude = entry.get("include_patterns", ["*"])
        if not isinstance(listInclude, list) or not listInclude:
            raise RuleConfigError(f"rule '{name}' needs a non-empty include_patterns list")
        listExclude = entry.get("exclude_patterns", [])
        excludeReason = str(entry.get("exclude_reason", "")).strip()
        if listExclude and not excludeReason:
            raise RuleConfigError(f"rule '{name}' excludes {', '.join(listExclude)} without an exclude_reason - every exception carries its reason")
        listRule.append(Rule(name, check, severity, listInclude, listExclude, params, excludeReason))
    return listRule


def loadRules(path: Path) -> list[Rule]:
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuleConfigError(f"cannot read {path}: {error}") from error
    return parseRules(data)


def listResourceFiles(resourceRoot: Path) -> list[str]:
    """리소스 루트 아래 모든 파일(리소스 경로, 소문자 비교 전 원래 철자)."""
    return sorted(path.relative_to(resourceRoot).as_posix() for path in resourceRoot.rglob("*") if path.is_file())


def validate(listRule: list[Rule], resourceRoot: Path, repositoryRoot: Path, listTarget: list[str] | None = None,
             minimumSeverity: str = "info") -> list[Finding]:
    """규칙을 돌립니다. `listTarget` 이 있으면 그 리소스 경로만 봅니다(색인은 트리 전체로 만든다 — 참조는 다른 파일을 본다)."""
    listAllPath = listResourceFiles(resourceRoot)
    context = ValidationContext(resourceRoot=resourceRoot, repositoryRoot=repositoryRoot, listAllPath=listAllPath)
    severityLimit = kSeverityOrder.index(minimumSeverity)
    listFinding: list[Finding] = []
    for relPath in (listTarget if listTarget is not None else listAllPath):
        if not (resourceRoot / relPath).is_file():
            continue
        for rule in listRule:
            if kSeverityOrder.index(rule.severity) > severityLimit or not rule.matches(relPath):
                continue
            function = _kCheckRegistry[rule.check][0]
            for message in function(rule, relPath, context):
                listFinding.append(Finding(relPath, rule.name, rule.severity, message))
    # 트리 전체를 볼 때만 — 규칙이 포함하는 파일을 하나도 빼지 않는 제외는 낡은 예외다.
    if listTarget is None:
        for rule in listRule:
            for pattern in rule.listExcludePattern:
                bCovers = any(fnmatch.fnmatchcase(relPath, pattern) and
                              any(fnmatch.fnmatchcase(relPath, include) for include in rule.listIncludePattern) for relPath in listAllPath)
                if bCovers is False:
                    listFinding.append(Finding("<rules>", rule.name, "error",
                                               f"exclude pattern '{pattern}' matches no file the rule includes - remove the stale exception"))
    listFinding.sort(key=lambda finding: (kSeverityOrder.index(finding.severity), finding.path, finding.rule))
    return listFinding
