"""
Scripts/common/ConfigReference.py

설정 참조 문서(`docs/Config/`)를 코드에서 만듭니다. **원본은 코드이고 문서는 출력입니다.**

- 설정 파일의 칸 표 — 설정 구조체(`REFLECT` · `PROPERTY`)의 이름 · 타입 · 기본값(멤버 초기값) · 범위(`Min` · `Max`) · 단위(`Units`) ·
  설명(문서 주석, 없으면 `Tooltip`)과 enum 값. 손으로 읽는 설정은 그 읽기 코드가 검사에 쓰는 `ConfigKeyDoc` 표.
- 전역 변수 표 — `SW_GLOBAL_VARIABLE` · `SW_TEST_GLOBAL_VARIABLE` · `SW_TEST_GLOBAL_VARIABLE_SHIPPED` 정의(타입 · 이름 · 기본값 · 설명).
- 명령줄 표 — `Source/Core/Predefined/ArgumentList.xxx` 의 줄과 그 위 주석, RHI 백엔드 줄은 `Config/Engine/CookContract.json`.
- CMake 캐시 옵션 표 — `option()` · `set( … CACHE … )` 과 `CMakePresets.json` 에서 그 값을 정하는 프리셋.
- 사용자 설정 표 — `*.settings.xml` 스키마의 `<Setting>`.
- 파일 목록과 수명 — `ConfigCatalog.py`.

C++ 를 파싱하지 않고 **이 저장소의 선언 모양**을 읽습니다(`PROPERTY( … )` 다음 줄의 멤버 선언 하나, `///<` · `/** */` 문서 주석).
모양이 어긋나 칸을 놓치면 `ConfigReferenceTest.FieldsMatchReflection`(EngineTest)이 리플렉션 등록과 대조해 잡습니다 — 이 모듈은
인쇄기이고 판정은 리플렉션이 합니다. 빌드 없이 돌아야 커밋 훅 · CI 린트 단계에서 낡은 문서를 잡을 수 있어서 이렇게 나눴습니다.
"""

from __future__ import annotations

import fnmatch
import json
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator
from xml.etree import ElementTree

from .CodeText import blankCommentsAndLiterals, blankMatch
from .CookContract import kCookContractConfigRelative
from .ConfigCatalog import (ConfigFileEntry, kKeyStyleJsonMember, kKeyStyleKeyTable, kKeyStyleXmlElementBare, kKeyStyleXmlMember,
                            kListConfigFile, kListLayerOrder, kListResourceSettingPattern, kSetConfigFolderNonConfig)

#: 생성 문서 폴더(저장소 상대).
kConfigReferenceDir = "docs/Config"
#: 기계가 읽는 메타데이터(테스트 · 에디터 설정 브라우저).
kConfigReferenceJson = "ConfigReference.json"
#: 손으로 쓰는 색인 한 장 — 생성 페이지가 거기로 고리를 건다.
kHandWrittenIndex = "docs/07_Configuration.md"

_kGeneratedBanner = "<!-- 생성 문서입니다. 손으로 고치지 말고 원본 코드를 고친 뒤 다시 만듭니다: py -3 Scripts/generate/GenerateConfigReference.py -->"

#: 스캔에서 빼는 폴더 조각.
_kListSkipPart = ("/ThirdParty/", "/build/", "/Tools/vcpkg/", "/.git/")

# ------------------------------------------------------------------------------
# 1) C++ 글 다루기 — 주석 · 문자열을 같은 길이의 공백으로 지워 괄호 깊이를 센다
# ------------------------------------------------------------------------------
_kCommentRe = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)


def blankCodeInternal(text: str, bKeepLiterals: bool = False) -> str:
    """
    주석(과 `bKeepLiterals` 가 아니면 문자열)을 줄바꿈만 남긴 공백으로 바꿉니다. 길이와 위치는 그대로입니다.
    `bKeepLiterals` 는 문자열 안의 `//` 도 주석으로 읽는다(`CodeText.blankComments` 와 다르다) — 생성 문서가 그 결과에 맞춰져 있다.
    """
    if bKeepLiterals:
        return _kCommentRe.sub(blankMatch, text)
    return blankCommentsAndLiterals(text)


def findClosingInternal(blanked: str, openIndex: int, openChar: str, closeChar: str) -> int:
    """`openIndex` 의 여는 괄호와 짝이 되는 닫는 괄호 위치입니다(없으면 -1)."""
    depth = 0
    for index in range(openIndex, len(blanked)):
        character = blanked[index]
        if character == openChar:
            depth += 1
        elif character == closeChar:
            depth -= 1
            if depth == 0:
                return index
    return -1


def splitTopLevelInternal(text: str, separator: str = ",") -> list[str]:
    """괄호 · 중괄호 · 꺾쇠 · 문자열 밖의 구분자로 나눕니다."""
    listPart: list[str] = []
    depth = 0
    current: list[str] = []
    bInString = False
    previous = ""
    for character in text:
        if bInString:
            current.append(character)
            if character == '"' and previous != "\\":
                bInString = False
        elif character == '"':
            bInString = True
            current.append(character)
        elif character in "({[<":
            depth += 1
            current.append(character)
        elif character in ")}]>":
            depth -= 1
            current.append(character)
        elif character == separator and depth == 0:
            listPart.append("".join(current).strip())
            current = []
        else:
            current.append(character)
        previous = character
    tail = "".join(current).strip()
    if tail:
        listPart.append(tail)
    return listPart


def cleanDocCommentInternal(raw: str) -> str:
    """문서 주석 덩어리를 한 줄 글로 만듭니다(`@brief` · `@details` · `@note` 표식과 별표를 뗀다)."""
    text = raw.strip()
    text = re.sub(r"^/\*\*<?|\*/$", "", text)
    lines = []
    for line in text.splitlines():
        line = line.strip()
        line = re.sub(r"^///<?\s?", "", line)
        line = re.sub(r"^\*\s?", "", line)
        lines.append(line)
    text = " ".join(part for part in lines if part)
    text = re.sub(r"@code\b.*?@endcode\b", "", text)
    text = re.sub(r"@(struct|class|enum)\s+\w+", "", text)
    text = re.sub(r"@(brief|details|note|param|return)\b", "", text)
    return re.sub(r"\s+", " ", text).strip()


def escapeCellInternal(text: str) -> str:
    """표 칸에 넣을 글 — `|` 와 줄바꿈이 표를 깨지 않게 바꿉니다."""
    return text.replace("|", "\\|").replace("\r", " ").replace("\n", " ").strip()


def findLeadingDocInternal(segment: str) -> str:
    """선언 바로 앞의 문서 주석(`/** … */` 또는 이어진 `///` 줄)입니다. 사이에 공백만 있어야 합니다."""
    stripped = segment.rstrip()
    if stripped.endswith("*/"):
        start = stripped.rfind("/**")
        if start >= 0:
            return cleanDocCommentInternal(stripped[start:])
        return ""
    listLine = stripped.splitlines()
    listDoc: list[str] = []
    for line in reversed(listLine):
        if line.strip().startswith("///"):
            listDoc.append(line)
            continue
        break
    return cleanDocCommentInternal("\n".join(reversed(listDoc))) if listDoc else ""


# ------------------------------------------------------------------------------
# 2) 리플렉션 구조체 · enum 읽기
# ------------------------------------------------------------------------------
@dataclass
class FieldDoc:
    """설정 칸 하나입니다."""

    name: str
    typeName: str
    defaultText: str = ""
    minText: str | None = None   # PROPERTY 메타의 Min — 적지 않았으면 None
    maxText: str | None = None   # Max — 위와 같다
    units: str | None = None     # Units — 위와 같다
    description: str = ""


@dataclass
class TypeDoc:
    """리플렉션 구조체 · 손으로 읽는 키 표 하나입니다."""

    name: str
    source: str
    description: str = ""
    baseName: str = ""
    listField: list[FieldDoc] = field(default_factory=list)


@dataclass
class EnumDoc:
    """enum 하나 — 값 이름과 설명입니다."""

    name: str
    source: str
    description: str = ""
    listValue: list[tuple[str, str]] = field(default_factory=list)


_kReflectStructRe = re.compile(r"\bREFLECT\s*\((?P<meta>[^)]*)\)\s*(?:struct|class)\s+(?:SW_\w+\s+)?(?P<name>\w+)\s*(?P<base>:[^{;]*)?\{")
_kEnumRe = re.compile(r"\benum\s+class\s+(?P<name>\w+)\s*(?::\s*\w+)?\s*\{")
_kPropertyRe = re.compile(r"\bPROPERTY\s*\(")
_kDeclRe = re.compile(r"^(?P<type>.+?)\s+(?P<name>_\w+)\s*(?::\s*\d+\s*)?(?P<init>\{.*\}|=.*)?$", re.DOTALL)


@dataclass
class SourceIndex:
    """`Source/` 의 리플렉션 구조체 · enum 자리입니다(이름 → (파일, 글))."""

    mapStruct: dict[str, tuple[str, str, re.Match]] = field(default_factory=dict)
    mapEnum: dict[str, tuple[str, str, re.Match]] = field(default_factory=dict)


class SourceTreeReader:
    """
    문서 한 벌을 만드는 동안 폴더마다 한 번 걷고 파일마다 한 번 읽는다 — 구조체 색인 · 전역 변수 · CMake 옵션이 같은 `Source/` 를 나눠 읽는다.

    순서는 `sorted(Path)` 와 같다(Windows 는 대소문자를 가리지 않고 `\\` 로 견준다) — 같은 이름이 둘이면 먼저 본 것을 쓰므로 순서가 결과다.
    """

    def __init__(self, repositoryRoot: Path) -> None:
        self.root = repositoryRoot
        self._mapListing: dict[str, list[str]] = {}
        self._mapText: dict[str, str] = {}

    def listFiles(self, rootName: str) -> list[str]:
        """`rootName`(파일 또는 폴더) 아래 파일의 저장소 상대 경로입니다. 남의 코드 · 빌드 산출물은 뺀다."""
        listing = self._mapListing.get(rootName)
        if listing is not None:
            return listing
        import os

        listing = []
        rootPath = self.root / rootName
        if rootPath.is_file():
            listing.append(rootName)
        elif rootPath.is_dir():
            for dirPath, listDirName, listFileName in os.walk(rootPath):
                relativeDir = os.path.relpath(dirPath, self.root).replace(os.sep, "/")
                listDirName[:] = [name for name in listDirName
                                  if not any(part in f"/{relativeDir}/{name}/" for part in _kListSkipPart)]
                listing.extend(f"{relativeDir}/{name}" for name in listFileName)
        listing = [relative for relative in listing if not any(part in f"/{relative}" for part in _kListSkipPart)]
        self._mapListing[rootName] = listing
        return listing

    def iterFiles(self, listRoot: tuple[str, ...], listSuffix: tuple[str, ...]) -> Iterator[tuple[str, str]]:
        """저장소 상대 경로(정렬)와 글을 냅니다. 이름 끝 비교는 파일 시스템처럼(Windows 는 대소문자를 가리지 않는다)."""
        import os

        bWindows = os.name == "nt"

        def normalizeCaseInternal(text: str) -> str:
            return text.replace("/", "\\").lower() if bWindows else text

        listNormSuffix = tuple(normalizeCaseInternal(suffix) for suffix in listSuffix)
        uniqueRelative = {relative for rootName in listRoot for relative in self.listFiles(rootName)
                          if relative == rootName or normalizeCaseInternal(relative.rsplit("/", 1)[-1]).endswith(listNormSuffix)}
        for relative in sorted(uniqueRelative, key=normalizeCaseInternal):
            yield relative, self.readText(relative)

    def readText(self, relative: str) -> str:
        text = self._mapText.get(relative)
        if text is None:
            text = (self.root / relative).read_text(encoding="utf-8", errors="replace")
            self._mapText[relative] = text
        return text


def buildSourceIndex(reader: SourceTreeReader) -> SourceIndex:
    """`Source/` 의 헤더에서 `REFLECT` 구조체와 `enum class` 자리를 모읍니다. 같은 이름이 둘이면 먼저 본 것(경로 순)을 씁니다."""
    index = SourceIndex()
    for relative, text in reader.iterFiles(("Source",), (".h",)):
        if "REFLECT" not in text and "enum class" not in text:
            continue
        blanked = blankCodeInternal(text)
        for match in _kReflectStructRe.finditer(blanked):
            index.mapStruct.setdefault(match.group("name"), (relative, text, match))
        for match in _kEnumRe.finditer(blanked):
            index.mapEnum.setdefault(match.group("name"), (relative, text, match))
    return index


def parseMetaInternal(metaText: str) -> dict[str, str]:
    """`PROPERTY( Min = 0.0, Tooltip = "…", Meta = "Units=s" )` 를 키 → 값으로 풉니다. 값 없는 표식은 빈 글입니다."""
    mapMeta: dict[str, str] = {}
    for part in splitTopLevelInternal(metaText):
        if "=" not in part:
            if part:
                mapMeta[part] = ""
            continue
        key, value = part.split("=", 1)
        value = value.strip()
        if value.startswith('"') and value.endswith('"'):
            value = value[1:-1]
        mapMeta[key.strip()] = value
    if "Meta" in mapMeta:
        for item in mapMeta["Meta"].split(";"):
            if "=" in item:
                key, value = item.split("=", 1)
                mapMeta.setdefault(key.strip(), value.strip())
    return mapMeta


def formatDefaultInternal(initText: str, mapMacro: dict[str, str]) -> str:
    """멤버 초기값을 문서 칸 글로 바꿉니다(`1.0f / 60.0f` → `1.0 / 60.0`, `RHIBackend::X` → `X`, `""` → 빈 글)."""
    text = (initText or "").strip()
    if text.startswith("="):
        text = text[1:].strip()
    elif text.startswith("{") and text.endswith("}"):
        text = text[1:-1].strip()
    text = re.sub(r"\s+", " ", text)
    if text in ("", '""'):
        return ""
    text = re.sub(r"\b(\d+\.\d*|\.\d+|\d+)f\b", r"\1", text)
    text = text.replace("SW_TRUE", "true").replace("SW_FALSE", "false")
    for macro, value in mapMacro.items():
        text = text.replace(macro, value)
    text = re.sub(r"\b(?:\w+::)+(\w+)", r"\1", text)
    listItem = splitTopLevelInternal(text)
    listItem = [item[1:-1] if len(item) >= 2 and item[0] == '"' and item[-1] == '"' else item for item in listItem]
    if len(listItem) > 4:
        return ", ".join(listItem[:3]) + f", … ({len(listItem)} 개)"
    return ", ".join(listItem)


def resolveRangeConstantInternal(rangeText: str | None, fileText: str, typeName: str) -> str | None:
    """`Min` · `Max` 가 C++ 상수 이름(`kMaxWorkerCount2D`, `-Limit::kMax`)이면 같은 파일의 `constexpr` 값으로 바꿉니다.

    에디터가 읽는 메타데이터 JSON 의 min · max 는 숫자 글이어야 하므로 이름이 남으면 안 됩니다(생성기 `AstVisitor::resolveRangeConstants` 와 같은 규칙:
    `_` 로 시작하는 이름은 필드). 같은 파일에서 찾지 못하면 멈춥니다 — 이름이 그대로 JSON 에 들어가면 에디터가 숫자로 읽지 못합니다.
    """
    if not rangeText:
        return rangeText
    match = re.fullmatch(r"(-?)\s*((?:\w+::)*)([A-Za-z]\w*)", rangeText.strip())
    if match is None:
        return rangeText
    valueMatch = re.search(r"constexpr\s+[\w:]+\s+" + re.escape(match.group(3)) + r"\s*=\s*(-?\d+(?:\.\d+)?)f?\s*;", fileText)
    if valueMatch is None:
        raise ValueError(f"{typeName}: Min/Max 상수 '{rangeText}' 를 같은 파일의 constexpr 숫자 상수에서 찾지 못했습니다(문서 생성기는 다른 헤더의 상수를 풀지 못합니다 — 상수를 설정 구조체와 같은 파일에 두세요)")
    value = valueMatch.group(1)
    if match.group(1):
        value = value[1:] if value.startswith("-") else "-" + value
    return value


def parseStructInternal(name: str, index: SourceIndex, mapMacro: dict[str, str]) -> TypeDoc | None:
    """리플렉션 구조체 하나를 읽습니다. 반사 부모의 칸을 앞에 둡니다(`getPropertiesWithBase` 순서)."""
    found = index.mapStruct.get(name)
    if found is None:
        return None
    relative, text, match = found
    blanked = blankCodeInternal(text)
    bodyOpen = match.end() - 1
    bodyClose = findClosingInternal(blanked, bodyOpen, "{", "}")
    if bodyClose < 0:
        return None
    typeDoc = TypeDoc(name=name, source=relative, description=findLeadingDocInternal(text[:match.start()]))
    baseText = (match.group("base") or "").strip(": \t\n")
    if baseText:
        typeDoc.baseName = re.sub(r"^(public|protected|private)\s+", "", baseText).split("::")[-1].strip()
        baseDoc = parseStructInternal(typeDoc.baseName, index, mapMacro)
        if baseDoc is not None:
            typeDoc.listField.extend(baseDoc.listField)

    for propMatch in _kPropertyRe.finditer(blanked, bodyOpen + 1, bodyClose):
        metaOpen = propMatch.end() - 1
        metaClose = findClosingInternal(blanked, metaOpen, "(", ")")
        if metaClose < 0:
            continue
        declEnd = blanked.find(";", metaClose, bodyClose)
        if declEnd < 0:
            continue
        segmentStart = max(blanked.rfind(";", bodyOpen, propMatch.start()), blanked.rfind("{", bodyOpen, propMatch.start()),
                           blanked.rfind("}", bodyOpen, propMatch.start())) + 1
        # 앞 선언 줄의 `///<` 는 그 선언의 것이다 — 줄 끝까지 건너뛴다.
        previousLineEnd = text.find("\n", segmentStart, propMatch.start())
        if previousLineEnd >= 0:
            segmentStart = previousLineEnd + 1
        leadingDoc = findLeadingDocInternal(text[segmentStart:propMatch.start()])
        lineEnd = text.find("\n", declEnd)
        trailing = text[declEnd + 1:lineEnd if lineEnd >= 0 else len(text)]
        trailingMatch = re.search(r"///<\s*(.*)$", trailing)
        declText = blankCodeInternal(text[metaClose + 1:declEnd], bKeepLiterals=True).strip()
        declMatch = _kDeclRe.match(re.sub(r"\s+", " ", declText))
        if declMatch is None:
            continue
        mapMeta = parseMetaInternal(text[metaOpen + 1:metaClose])
        description = trailingMatch.group(1).strip() if trailingMatch else ""
        if leadingDoc:
            description = f"{leadingDoc} {description}".strip() if description and description not in leadingDoc else leadingDoc
        if not description and "Tooltip" in mapMeta:
            description = mapMeta["Tooltip"]
        typeDoc.listField.append(FieldDoc(
            name=declMatch.group("name"), typeName=re.sub(r"\s+", " ", declMatch.group("type")).strip(),
            defaultText=formatDefaultInternal(declMatch.group("init") or "", mapMacro),
            minText=resolveRangeConstantInternal(mapMeta.get("Min"), text, name),
            maxText=resolveRangeConstantInternal(mapMeta.get("Max"), text, name), units=mapMeta.get("Units"),
            description=description))
    return typeDoc


def parseEnumInternal(name: str, index: SourceIndex) -> EnumDoc | None:
    """`enum class` 하나의 값과 `///<` 설명을 읽습니다."""
    found = index.mapEnum.get(name)
    if found is None:
        return None
    relative, text, match = found
    blanked = blankCodeInternal(text)
    bodyOpen = match.end() - 1
    bodyClose = findClosingInternal(blanked, bodyOpen, "{", "}")
    enumDoc = EnumDoc(name=name, source=relative, description=findLeadingDocInternal(text[:match.start()]))
    cursor = bodyOpen + 1
    for part in blanked[bodyOpen + 1:bodyClose].split(","):
        partStart = cursor
        cursor += len(part) + 1
        valueName = part.split("=")[0].strip()
        if re.fullmatch(r"\w+", valueName or "") is None:
            continue
        # 값의 `///<` 는 그 값의 쉼표 뒤, 같은 줄에 있다(마지막 값은 쉼표가 없을 수 있다 — 그때는 값 줄 끝까지).
        nameEnd = partStart + len(part.rstrip())
        lineEnd = text.find("\n", nameEnd)
        tail = text[nameEnd:lineEnd if 0 <= lineEnd < bodyClose else bodyClose]
        docMatch = re.search(r"///<\s*(.*)$", tail)
        enumDoc.listValue.append((valueName, docMatch.group(1).strip() if docMatch else ""))
    return enumDoc


# ------------------------------------------------------------------------------
# 3) 손으로 읽는 설정의 키 표 — `ConfigKeyDoc kArrXxxKeyDoc[] = { { "key", "type", "default", "설명" }, … };`
# ------------------------------------------------------------------------------
_kKeyTableRe = r"\b{name}\s*\[\s*\]\s*=\s*\{{"


def parseKeyTableInternal(repositoryRoot: Path, sourcePath: str, tableName: str) -> TypeDoc | None:
    """`ConfigKeyDoc` 표 하나를 읽습니다. 없으면 None 입니다."""
    path = repositoryRoot / sourcePath
    if path.is_file() is False:
        return None
    text = path.read_text(encoding="utf-8", errors="replace")
    blanked = blankCodeInternal(text)
    match = re.search(_kKeyTableRe.format(name=re.escape(tableName)), blanked)
    if match is None:
        return None
    bodyOpen = match.end() - 1
    bodyClose = findClosingInternal(blanked, bodyOpen, "{", "}")
    lineStart = text.rfind("\n", 0, match.start()) + 1
    typeDoc = TypeDoc(name=tableName, source=sourcePath, description=findLeadingDocInternal(text[:lineStart].rsplit(";", 1)[-1]))
    for row in re.finditer(r"\{([^{}]*)\}", text[bodyOpen + 1:bodyClose]):
        listCell = [cell.strip() for cell in splitTopLevelInternal(row.group(1))]
        listText = [json.loads(cell) if cell.startswith('"') else cell for cell in listCell]
        if len(listText) != 4:
            continue
        typeDoc.listField.append(FieldDoc(name=listText[0], typeName=listText[1], defaultText=listText[2], description=listText[3]))
    return typeDoc


# ------------------------------------------------------------------------------
# 4) 전역 변수 · 명령줄 · CMake 옵션 · 사용자 설정
# ------------------------------------------------------------------------------
_kGlobalVariableRe = re.compile(r"\b(SW_GLOBAL_VARIABLE|SW_TEST_GLOBAL_VARIABLE_SHIPPED|SW_TEST_GLOBAL_VARIABLE)\s*\(")
_kMapGlobalVariableKind = {
    "SW_GLOBAL_VARIABLE": "일반",
    "SW_TEST_GLOBAL_VARIABLE": "테스트",
    "SW_TEST_GLOBAL_VARIABLE_SHIPPED": "테스트 · 배포본에도",
}


@dataclass
class GlobalVariableDoc:
    """전역 변수 하나입니다."""

    name: str
    typeName: str
    defaultText: str
    kind: str
    description: str
    source: str


def collectGlobalVariables(reader: SourceTreeReader) -> list[GlobalVariableDoc]:
    """`Source/` 의 전역 변수 정의를 모읍니다(이름 순). 매크로 정의 · 주석 안의 예시는 세지 않습니다."""
    listVariable: list[GlobalVariableDoc] = []
    for relative, text in reader.iterFiles(("Source",), (".h", ".cpp", ".inl")):
        if "GLOBAL_VARIABLE" not in text:
            continue
        blanked = blankCodeInternal(text, bKeepLiterals=True)
        blanked = re.sub(r"^[ \t]*#[ \t]*define(?:[^\n]*\\\n)*[^\n]*", lambda m: re.sub(r"[^\n]", " ", m.group(0)), blanked, flags=re.M)
        blankedCode = blankCodeInternal(blanked)
        for match in _kGlobalVariableRe.finditer(blanked):
            close = findClosingInternal(blankedCode, match.end() - 1, "(", ")")
            if close < 0:
                continue
            listArgument = splitTopLevelInternal(blanked[match.end():close])
            if len(listArgument) != 4 or re.fullmatch(r"gv_\w+", listArgument[1]) is None:
                continue
            description = listArgument[3]
            if description.startswith('"'):
                description = json.loads(description)
            listVariable.append(GlobalVariableDoc(
                name=listArgument[1], typeName=listArgument[0].replace("sw::", ""),
                defaultText=formatDefaultInternal(listArgument[2], {}), kind=_kMapGlobalVariableKind[match.group(1)],
                description=description, source=relative))
    listVariable.sort(key=lambda variable: variable.name.lower())
    return listVariable


@dataclass
class ArgumentDoc:
    """명령줄 인자 한 줄입니다."""

    enumName: str
    listSpelling: list[str]
    valueKind: str
    description: str


def collectArguments(repositoryRoot: Path) -> list[ArgumentDoc]:
    """`ArgumentList.xxx` 의 줄을 순서대로 모읍니다.

    줄 위의 `//` 주석 덩어리가 설명이고, 빈 줄이 나올 때까지 뒤따르는 줄 모두의 설명입니다(`-import-textures` · `-check-textures` 처럼 짝인 줄).
    파일 머리의 `/** … */` 는 형식 설명이라 세지 않습니다.
    """
    path = repositoryRoot / "Source/Core/Predefined/ArgumentList.xxx"
    if path.is_file() is False:
        return []
    listArgument: list[ArgumentDoc] = []
    listComment: list[str] = []
    bAfterRow = False
    text = re.sub(r"/\*.*?\*/", "", path.read_text(encoding="utf-8"), flags=re.DOTALL)
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("//"):
            if bAfterRow:
                listComment = []
                bAfterRow = False
            listComment.append(stripped[2:].strip())
            continue
        if stripped.startswith("#include") and "CookContract" in stripped:
            listArgument.extend(collectBackendArgumentsInternal(repositoryRoot, " ".join(listComment)))
            bAfterRow = True
            continue
        match = re.match(r"SW_REGISTER_ARGUMENT\s*\((.*)\)\s*$", stripped)
        if match is None:
            if not stripped:
                listComment = []
                bAfterRow = False
            continue
        bAfterRow = True
        listPart = splitTopLevelInternal(match.group(1))
        defaultText = listPart[1]
        if defaultText in ("true", "false"):
            valueKind = "플래그(`-이름`)"
        elif defaultText.startswith('"'):
            valueKind = "글 값(`-이름=값`)"
        else:
            valueKind = "숫자 값(`-이름=값`)"
        listArgument.append(ArgumentDoc(enumName=listPart[0], listSpelling=[json.loads(spelling) for spelling in listPart[3:]],
                                        valueKind=valueKind, description=" ".join(listComment)))
    return listArgument


def collectBackendArgumentsInternal(repositoryRoot: Path, description: str) -> list[ArgumentDoc]:
    """RHI 백엔드 플래그 줄 — 쿠킹 표의 `rhi_backends` 줄마다 하나입니다."""
    # 쿠킹 표는 저장소에 늘 있다 — 없으면 멈춘다(빈 목록이면 백엔드 인자가 문서에서 조용히 빠진다).
    contract = json.loads((repositoryRoot / kCookContractConfigRelative).read_text(encoding="utf-8"))
    return [ArgumentDoc(enumName=row["command_line_argument"], listSpelling=[row["command_line_name"]], valueKind="플래그(`-이름`)",
                        description=f"{row['name']} 백엔드로 띄운다. {description}".strip())
            for row in contract["rhi_backends"]]


@dataclass
class BuildOptionDoc:
    """CMake 캐시 옵션 하나입니다."""

    name: str
    kind: str
    defaultText: str
    description: str
    listChoice: list[str]
    source: str
    listPreset: list[str] = field(default_factory=list)


_kOptionRe = re.compile(r"^\s*option\s*\(\s*(SW_\w+)\s+\"([^\"]*)\"\s*([^)]*)\)", re.M)
_kCacheRe = re.compile(r"^\s*set\s*\(\s*(SW_\w+)\s+(\"[^\"]*\"|\S+)\s+CACHE\s+(\w+)\s+\"([^\"]*)\"", re.M)
_kStringsRe = re.compile(r"set_property\s*\(\s*CACHE\s+(SW_\w+)\s+PROPERTY\s+STRINGS\s+([^)]*)\)")
_kLocalSetRe = re.compile(r"^\s*set\s*\(\s*(\w+)\s+([^)\"]*)\)", re.M)
_kVariableRefRe = re.compile(r"\$\{(\w+)\}")


def expandCMakeWordsInternal(listWord: list[str], mapVariable: dict[str, list[str]]) -> list[str]:
    """`${이름}` 낱말을 알려진 값으로 풀어 펼칩니다. 모르는 변수(configure 때 계산하는 값)는 뺍니다."""
    listOut: list[str] = []
    for word in listWord:
        match = _kVariableRefRe.fullmatch(word)
        if match is None:
            listOut.append(word)
        elif match.group(1) in mapVariable:
            listOut.extend(mapVariable[match.group(1)])
    return listOut


def collectBuildOptions(reader: SourceTreeReader) -> list[BuildOptionDoc]:
    """`SW_*` 캐시 옵션을 모읍니다(이름 순). 같은 옵션을 갈래마다 다른 기본값으로 둔 곳은 기본값을 이어 적습니다."""
    mapOption: dict[str, BuildOptionDoc] = {}
    mapChoice: dict[str, list[str]] = {}
    # configure 가 쿠킹 표에서 읽는 값(`Source/Engine/CMakeLists.txt` 의 Shipping RHI 기본) — 글로는 풀 수 없어 표에서 같은 값을 읽는다.
    repositoryRoot = reader.root
    mapKnownVariable = {"swContractDefaultRhi": [findMacroValuesInternal(repositoryRoot)["SW_RHI_BACKEND_DEFAULT"]]}
    for relative, text in reader.iterFiles(("CMakeLists.txt", "cmake", "Source", "Test", "Tools/ReflectionParser"), (".cmake", "CMakeLists.txt")):
        mapVariable = dict(mapKnownVariable)
        for match in _kLocalSetRe.finditer(text):
            if "CACHE" not in match.group(2).split():
                mapVariable[match.group(1)] = match.group(2).split()
        for match in _kOptionRe.finditer(text):
            mapOption.setdefault(match.group(1), BuildOptionDoc(match.group(1), "BOOL", match.group(3).strip(), match.group(2), [], relative))
        for match in _kCacheRe.finditer(text):
            if match.group(3) == "INTERNAL":
                continue
            defaultText = " ".join(expandCMakeWordsInternal([match.group(2).strip('"')], mapVariable)) or match.group(2).strip('"')
            existing = mapOption.get(match.group(1))
            if existing is not None and defaultText not in existing.defaultText.split(" · "):
                existing.defaultText += f" · {defaultText}"
                continue
            mapOption.setdefault(match.group(1), BuildOptionDoc(match.group(1), match.group(3), defaultText, match.group(4), [], relative))
        for match in _kStringsRe.finditer(text):
            mapChoice[match.group(1)] = expandCMakeWordsInternal(match.group(2).split(), mapVariable)
    listPreset = json.loads((repositoryRoot / "CMakePresets.json").read_text(encoding="utf-8"))["configurePresets"]
    for option in mapOption.values():
        option.listChoice = mapChoice.get(option.name, [])
        option.listPreset = sorted(preset["name"] for preset in listPreset if option.name in preset.get("cacheVariables", {}))
    return sorted(mapOption.values(), key=lambda option: option.name)


def collectUserSettings(repositoryRoot: Path) -> list[tuple[str, list[dict[str, str]]]]:
    """`*.settings.xml` 스키마마다 `<Setting>` 의 속성 목록입니다(파일 경로 순)."""
    listSchema: list[tuple[str, list[dict[str, str]]]] = []
    resourceRoot = repositoryRoot / "Resource"
    if resourceRoot.is_dir() is False:
        return listSchema
    for path in sorted(resourceRoot.rglob("*.settings.xml")):
        relative = path.relative_to(repositoryRoot).as_posix()
        root = ElementTree.parse(path).getroot()
        listSchema.append((relative, [dict(element.attrib) for element in root.iter("Setting")]))
    return listSchema


# ------------------------------------------------------------------------------
# 5) 카탈로그 대조 — 파일이 모두 어느 줄에 맞나, 줄이 가리키는 것이 있나
# ------------------------------------------------------------------------------
def findCatalogEntry(relativePath: str) -> ConfigFileEntry | None:
    """저장소 상대 경로에 맞는 첫 줄입니다. 대소문자를 가린다(`fnmatchcase`)."""
    for entry in kListConfigFile:
        if entry.bOutsideRepo is False and fnmatch.fnmatchcase(relativePath, entry.pathPattern):
            return entry
    return None


def listCandidateFilesInternal(repositoryRoot: Path) -> list[str]:
    """`Config/` · `Resource/` · `Source/` 의 파일(저장소 상대)입니다.

    git 저장소면 추적하거나 추적 대상이 될 파일만 본다(`git ls-files --cached --others --exclude-standard`) — git 이 무시하는 로컬 파일
    (`imgui.ini` · `toolchain_config.json`)은 머신마다 달라 결과를 흔든다. git 이 아니면(린트 자가 테스트의 임시 트리) 폴더를 훑는다.
    """
    if (repositoryRoot / ".git").exists():
        result = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", "Config", "Resource", "Source"],
                                cwd=repositoryRoot, capture_output=True, text=True, encoding="utf-8", check=False)
        if result.returncode == 0:
            return [line.strip() for line in result.stdout.splitlines() if line.strip()]
    listPath: list[str] = []
    for folder in ("Config", "Resource", "Source"):
        root = repositoryRoot / folder
        if root.is_dir():
            listPath.extend(path.relative_to(repositoryRoot).as_posix() for path in root.rglob("*") if path.is_file())
    return listPath


def collectConfigFiles(repositoryRoot: Path) -> list[str]:
    """대조할 파일 — `Config/` 아래 전부와 `Resource/` 의 설정 XML, `Source/` 의 모듈 매니페스트입니다(저장소 상대, 정렬)."""
    listPath: list[str] = []
    for relative in listCandidateFilesInternal(repositoryRoot):
        if relative in kSetConfigFolderNonConfig:
            continue
        if relative.startswith("Config/"):
            listPath.append(relative)
        elif relative.startswith("Resource/") and any(fnmatch.fnmatchcase(relative, pattern) for pattern in kListResourceSettingPattern):
            listPath.append(relative)
        elif relative.startswith("Source/") and relative.endswith(".module.json"):
            listPath.append(relative)
    return sorted(listPath)


def findCatalogViolations(repositoryRoot: Path, index: SourceIndex) -> list[str]:
    """카탈로그와 저장소가 어긋난 곳입니다."""
    violations: list[str] = []
    listFile = collectConfigFiles(repositoryRoot)
    for relative in listFile:
        if findCatalogEntry(relative) is None:
            violations.append(f"[Config Catalog] 표에 없는 설정 파일입니다: {relative} — Scripts/common/ConfigCatalog.py 에 줄을 더한다")
    for entry in kListConfigFile:
        if entry.bOutsideRepo:
            continue
        if entry.bOptional is False and not any(fnmatch.fnmatchcase(relative, entry.pathPattern) for relative in listFile):
            violations.append(f"[Config Catalog] 표의 줄에 맞는 파일이 없습니다: {entry.pathPattern}")
        if entry.typeName and entry.typeName not in index.mapStruct:
            violations.append(f"[Config Catalog] {entry.pathPattern}: 리플렉션 구조체 {entry.typeName} 를 {entry.header or 'Source/'} 에서 찾지 못했습니다")
        if entry.ownerDoc and (repositoryRoot / entry.ownerDoc).exists() is False:
            violations.append(f"[Config Catalog] {entry.pathPattern}: 정본 문서 {entry.ownerDoc} 가 없습니다")
        for tableName in entry.listKeyTable:
            if parseKeyTableInternal(repositoryRoot, entry.keyTableSource, tableName) is None:
                violations.append(f"[Config Catalog] {entry.pathPattern}: 키 표 {tableName} 를 {entry.keyTableSource} 에서 찾지 못했습니다")
    return violations


# ------------------------------------------------------------------------------
# 6) 페이지 만들기
# ------------------------------------------------------------------------------
def describeKeyStyleInternal(entry: ConfigFileEntry) -> str:
    """파일의 키가 코드의 무엇과 같은지 한 줄입니다."""
    if entry.keyStyle == kKeyStyleJsonMember:
        return "JSON 키는 아래 필드 이름 그대로입니다(앞의 `_` 포함). 적지 않은 필드는 기본값입니다. 모르는 키나 읽지 못하는 값은 로드 오류입니다."
    if entry.keyStyle == kKeyStyleXmlMember:
        return "XML 속성(값 하나)과 자식 요소(목록, 구조체)의 이름은 아래 필드 이름 그대로입니다. 모르는 이름은 로드 오류입니다."
    if entry.keyStyle == kKeyStyleXmlElementBare:
        return "XML 자식 요소 이름은 아래 필드 이름에서 앞의 `_` 를 뺀 것입니다(`_startMap` → `<startMap>`). 모르는 요소는 로드 오류입니다."
    if entry.keyStyle == kKeyStyleKeyTable:
        return "JSON 키는 아래 테이블의 키 그대로입니다. 모르는 키는 로드 오류이고, 읽기 코드가 이 테이블(`ConfigKeyDoc`)로 검사합니다."
    return ""


def makeFieldTableInternal(typeDoc: TypeDoc) -> list[str]:
    """칸 표입니다."""
    lines = ["| 필드 | 타입 | 기본값 | 범위 | 단위 | 설명 |", "|---|---|---|---|---|---|"]
    for fieldDoc in typeDoc.listField:
        rangeText = ""
        if fieldDoc.minText or fieldDoc.maxText:
            rangeText = f"{fieldDoc.minText or '-'} ~ {fieldDoc.maxText or '-'}"
        defaultText = f"`{escapeCellInternal(fieldDoc.defaultText)}`" if fieldDoc.defaultText else "—"
        lines.append(f"| `{fieldDoc.name}` | `{escapeCellInternal(fieldDoc.typeName)}` | {defaultText} | {rangeText} | {fieldDoc.units or ''} | "
                     f"{escapeCellInternal(fieldDoc.description)} |")
    return lines


def formatBaseValueInternal(value: object) -> str:
    """바탕 층 파일의 JSON 값을 기본값 칸 글로 씁니다(목록은 쉼표, 참거짓은 소문자)."""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, list):
        return ", ".join(formatBaseValueInternal(item) for item in value)
    return str(value)


def applyBaseLayerDefaultsInternal(repositoryRoot: Path, entry: ConfigFileEntry, listTypeDoc: list[TypeDoc]) -> None:
    """바탕 층(값의 정본이 파일이고 헤더는 형식만)이면 기본값 칸을 그 파일의 값으로 채웁니다. 구조체 칸은 그 타입 절에 씁니다."""
    if not entry.bBaseLayer or not listTypeDoc:
        return
    root = json.loads((repositoryRoot / entry.pathPattern).read_text(encoding="utf-8"))
    mapTypeValue: dict[str, dict] = {listTypeDoc[0].name: root}
    for typeDoc in listTypeDoc:
        values = mapTypeValue.get(typeDoc.name)
        if values is None:
            continue
        for fieldDoc in typeDoc.listField:
            if fieldDoc.name not in values:
                continue
            value = values[fieldDoc.name]
            if isinstance(value, dict):
                mapTypeValue[fieldDoc.typeName] = value
                continue
            fieldDoc.defaultText = formatBaseValueInternal(value)


def collectReferencedNamesInternal(typeName: str) -> list[str]:
    """타입 글에 든 식별자들(`vector<PhysicsLayerDef>` → `vector`, `PhysicsLayerDef`)입니다."""
    return re.findall(r"[A-Za-z_]\w*", typeName)


def makeEntryPage(repositoryRoot: Path, entry: ConfigFileEntry, index: SourceIndex, mapMacro: dict[str, str],
                  outMetadata: dict) -> str:
    """설정 파일 한 종류의 페이지입니다."""
    lines = [_kGeneratedBanner, "", f"# {entry.page}", "", f"[설정 색인](README.md) · [어디에 두나]({'../' + kHandWrittenIndex.split('/', 1)[1]})", ""]
    lines += ["| | |", "|---|---|",
              f"| 파일 | `{entry.pathPattern}` |", f"| 층 | {entry.layer} |", f"| 읽는 곳 | {entry.reader} |",
              f"| 언제 | {entry.readWhen} |", f"| 배포본 | {entry.shipping} |", f"| 커밋 | {'한다' if entry.bCommitted else '하지 않는다(로컬 · git 무시)'} |"]
    if entry.note:
        lines.append(f"| 참고 | {entry.note} |")
    lines.append("")
    keyStyle = describeKeyStyleInternal(entry)
    if keyStyle:
        lines += [keyStyle, ""]

    listTypeDoc: list[TypeDoc] = []
    listEnumDoc: list[EnumDoc] = []
    if entry.typeName:
        listPending = [entry.typeName]
        setSeen: set[str] = set()
        while listPending:
            name = listPending.pop(0)
            if name in setSeen:
                continue
            setSeen.add(name)
            typeDoc = parseStructInternal(name, index, mapMacro)
            if typeDoc is not None:
                listTypeDoc.append(typeDoc)
                for fieldDoc in typeDoc.listField:
                    for referenced in collectReferencedNamesInternal(fieldDoc.typeName):
                        if referenced in index.mapStruct and referenced not in setSeen:
                            listPending.append(referenced)
                        elif referenced in index.mapEnum and all(enumDoc.name != referenced for enumDoc in listEnumDoc):
                            enumDoc = parseEnumInternal(referenced, index)
                            if enumDoc is not None:
                                listEnumDoc.append(enumDoc)
    for tableName in entry.listKeyTable:
        typeDoc = parseKeyTableInternal(repositoryRoot, entry.keyTableSource, tableName)
        if typeDoc is not None:
            listTypeDoc.append(typeDoc)
    applyBaseLayerDefaultsInternal(repositoryRoot, entry, listTypeDoc)

    for typeIndex, typeDoc in enumerate(listTypeDoc):
        title = "필드" if typeIndex == 0 else f"`{typeDoc.name}`"
        lines += [f"## {title}", ""]
        if typeDoc.description:
            lines += [typeDoc.description, ""]
        lines += [f"원본: [`{typeDoc.source}`](../../{typeDoc.source})", ""]
        lines += makeFieldTableInternal(typeDoc)
        lines.append("")
    for enumDoc in listEnumDoc:
        lines += [f"## 열거 `{enumDoc.name}`", ""]
        if enumDoc.description:
            lines += [enumDoc.description, ""]
        lines += ["| 값 | 설명 |", "|---|---|"]
        lines += [f"| `{valueName}` | {escapeCellInternal(valueDoc)} |" for valueName, valueDoc in enumDoc.listValue]
        lines.append("")
    if entry.ownerDoc:
        lines += [f"형식 설명: [`{entry.ownerDoc}`](../../{entry.ownerDoc})", ""]

    # 메타데이터 JSON 의 계약(에디터 `ProjectSettingsCatalog` 가 읽는다)에서는 min · max · units 의 빈 글이 "적지 않음" 이다 — None 을 그 표기로 바꾸는 곳은 여기 하나다.
    outMetadata["files"].append({
        "page": entry.page, "path": entry.pathPattern, "layer": entry.layer, "type": entry.typeName,
        "types": [{"name": typeDoc.name, "source": typeDoc.source,
                   "fields": [{"name": fieldDoc.name, "type": fieldDoc.typeName, "default": fieldDoc.defaultText,
                               "min": fieldDoc.minText or "", "max": fieldDoc.maxText or "", "units": fieldDoc.units or "",
                               "description": fieldDoc.description}
                              for fieldDoc in typeDoc.listField]} for typeDoc in listTypeDoc],
        "enums": [{"name": enumDoc.name, "values": [valueName for valueName, _ in enumDoc.listValue]} for enumDoc in listEnumDoc]})
    return "\n".join(lines).rstrip() + "\n"


def makeIndexPage() -> str:
    """색인 — 층별 설정 파일 표와 다른 생성 페이지 고리입니다."""
    lines = [_kGeneratedBanner, "", "# 설정 참조", "",
             f"설정을 **어디에 두는가**(층, 우선순위, 배포본, 핫 리로드)는 손으로 쓴 [`{kHandWrittenIndex}`](../{kHandWrittenIndex.split('/', 1)[1]})에 있습니다.",
             "이 폴더는 코드에서 만듭니다. 필드를 고치려면 코드를 고치고 `py -3 Scripts/generate/GenerateConfigReference.py` 로 다시 만듭니다"
             "(문서가 낡으면 `CheckConfigReference` 게이트가 실패합니다).", "",
             "| 다른 목록 | 원본 |", "|---|---|",
             "| [전역 변수 `-gv_*`](GlobalVariables.md) | `SW_GLOBAL_VARIABLE`, `SW_TEST_GLOBAL_VARIABLE(_SHIPPED)` 정의 |",
             "| [명령줄 인자](CommandLine.md) | `Source/Core/Predefined/ArgumentList.xxx`, `Config/Engine/CookContract.json` |",
             "| [CMake 빌드 옵션](BuildOptions.md) | `option()`, `set( … CACHE … )`, `CMakePresets.json` |",
             "| [사용자 설정](UserSettings.md) | `Resource/**/*.settings.xml` |", ""]
    for layer in kListLayerOrder:
        listEntry = [entry for entry in kListConfigFile if entry.layer == layer]
        if not listEntry:
            continue
        lines += [f"## {layer}", "", "| 파일 | 읽는 곳과 시점 | 배포본 | 커밋 | 문서 |", "|---|---|---|---|---|"]
        for entry in listEntry:
            docText = f"[{entry.page}]({entry.page}.md)" if entry.page else (f"[형식 설명](../../{entry.ownerDoc})" if entry.ownerDoc else "")
            readText = f"{entry.reader} — {entry.readWhen}"
            if entry.note:
                readText += f". {entry.note}"
            lines.append(f"| `{entry.pathPattern}` | {escapeCellInternal(readText)} | {escapeCellInternal(entry.shipping)} | "
                         f"{'한다' if entry.bCommitted else '안 한다'} | {docText} |")
        lines.append("")
    lines += ["## 타입 읽는 법", "",
              "`string` = 글, `vector<T>` = 배열(XML 은 자식 원소 목록), `map<K, V>` = 오브젝트, `float2/3/4` = `\"x,y,z\"` 글, enum = 값 이름 글, "
              "`hashed_string` = 이름 글. 기본값이 `—` 이면 0, false, 빈 글, 빈 목록입니다.", ""]
    return "\n".join(lines).rstrip() + "\n"


def makeGlobalVariablePage(listVariable: list[GlobalVariableDoc], outMetadata: dict) -> str:
    """전역 변수 표입니다(폴더별)."""
    lines = [_kGeneratedBanner, "", "# 전역 변수 (`-gv_*`)", "", "[설정 색인](README.md)", "",
             "값은 명령줄 `-gv_<이름>=<값>`, 에디터의 Global Variables 패널, 개발 콘솔로 바꿉니다. "
             "종류는 셋입니다. **일반** 은 배포본에도 있습니다. **테스트** 는 배포본에서 등록되지 않아 기본값으로만 읽힙니다. "
             "**테스트 · 배포본에도** 는 스크립트가 배포 실행 파일을 조종할 때 쓰는 스위치입니다. "
             "사용자 설정이 값을 넣는 변수는 `UserSettingsVariables.cpp` 에 있고, 시작할 때는 명령줄 `-gv_*` 가 플레이어 값보다 우선합니다(`docs/07_Configuration.md`).", ""]
    mapGroup: dict[str, list[GlobalVariableDoc]] = {}
    for variable in listVariable:
        parts = variable.source.split("/")
        group = "/".join(parts[:3]) if len(parts) > 3 else "/".join(parts[:2])
        mapGroup.setdefault(group, []).append(variable)
    for group in sorted(mapGroup):
        lines += [f"## `{group}`", "", "| 이름 | 타입 | 기본값 | 종류 | 설명 | 정의 |", "|---|---|---|---|---|---|"]
        for variable in mapGroup[group]:
            defaultText = f"`{escapeCellInternal(variable.defaultText)}`" if variable.defaultText else "—"
            fileName = variable.source.rsplit("/", 1)[-1]
            lines.append(f"| `{variable.name}` | `{variable.typeName}` | {defaultText} | {variable.kind} | {escapeCellInternal(variable.description)} | "
                         f"[{fileName}](../../{variable.source}) |")
        lines.append("")
    outMetadata["globalVariables"] = [{"name": variable.name, "type": variable.typeName, "default": variable.defaultText, "kind": variable.kind,
                                       "description": variable.description, "source": variable.source} for variable in listVariable]
    return "\n".join(lines).rstrip() + "\n"


def makeArgumentPage(listArgument: list[ArgumentDoc], outMetadata: dict) -> str:
    """명령줄 인자 표입니다(목록 순서)."""
    lines = [_kGeneratedBanner, "", "# 명령줄 인자", "", "[설정 색인](README.md)", "",
             "키 앞의 하이픈은 `-` 든 `--` 든 같습니다. 전역 변수는 [`-gv_<이름>=<값>`](GlobalVariables.md) 로 따로 받습니다.", "",
             "| 철자 | 값 | 설명 | 코드 이름 |", "|---|---|---|---|"]
    for argument in listArgument:
        spelling = " · ".join(f"`-{item}`" for item in argument.listSpelling)
        lines.append(f"| {spelling} | {argument.valueKind} | {escapeCellInternal(argument.description)} | `{argument.enumName}` |")
    outMetadata["arguments"] = [{"name": argument.enumName, "spellings": argument.listSpelling, "value": argument.valueKind,
                                 "description": argument.description} for argument in listArgument]
    return "\n".join(lines).rstrip() + "\n"


def makeBuildOptionPage(listOption: list[BuildOptionDoc], outMetadata: dict) -> str:
    """CMake 캐시 옵션 표입니다."""
    lines = [_kGeneratedBanner, "", "# CMake 빌드 옵션 (`SW_*`)", "", "[설정 색인](README.md)", "",
             "configure 때 정한다(`cmake --preset <프리셋>` 또는 `-D<이름>=<값>`). 값을 바꾸면 다시 configure 한다. Dev 에서 게임을 바꾸는 것은 옵션이 아니라 "
             "실행 인자다(`App.exe -game=<게임>` — `Ninja-Debug` 가 게임 전부를 짓는다, `SW_GAMES`).", "",
             "| 이름 | 타입 | 기본값 | 고를 수 있는 값 | 설명 | 정하는 프리셋 | 선언 |", "|---|---|---|---|---|---|---|"]
    for option in listOption:
        choices = " · ".join(f"`{choice}`" for choice in option.listChoice)
        presets = ", ".join(option.listPreset) if len(option.listPreset) <= 4 else f"{', '.join(option.listPreset[:3])} 외 {len(option.listPreset) - 3}"
        lines.append(f"| `{option.name}` | {option.kind} | `{escapeCellInternal(option.defaultText)}` | {choices} | {escapeCellInternal(option.description)} | "
                     f"{presets} | [{option.source}](../../{option.source}) |")
    outMetadata["buildOptions"] = [{"name": option.name, "type": option.kind, "default": option.defaultText, "choices": option.listChoice,
                                    "description": option.description, "presets": option.listPreset} for option in listOption]
    return "\n".join(lines).rstrip() + "\n"


#: 사용자 설정 스키마를 읽는 C++ — 속성을 생략했을 때의 값(적용 시점 · 키 바인딩 대상)은 이 두 파일이 정본이다.
_kUserSettingsSchemaHeader = "Source/Engine/UserSettings/UserSettingsSchema.h"
_kUserSettingsSchemaSource = "Source/Engine/UserSettings/UserSettingsSchema.cpp"
_kApplyTimingDefaultRe = re.compile(r"\bUserSettingApplyTiming\s+_applyTiming\s*\{\s*UserSettingApplyTiming::(\w+)\s*\}")
_kApplyTimingNameRe = re.compile(r"\{\s*\"(\w+)\"\s*,\s*UserSettingApplyTiming::(\w+)\s*\}")
_kKeyBindingHandlerRe = re.compile(r"\bkKeyBindingHandler\s*\[\s*\]\s*=\s*\"([^\"]+)\"")


@dataclass(frozen=True)
class UserSettingDefaults:
    """`<Setting>` 이 속성을 생략했을 때 런타임이 쓰는 값 — C++ 스키마 읽기에서 뽑는다(문서 생성기가 따로 들고 있지 않는다)."""
    applyName: str          # `apply` 생략 → 이 철자(`UserSettingDef::_applyTiming` 의 기본 열거자를 이름 표로 되돌린 것)
    keyBindingTarget: str   # `type="keyBinding"` 이 `target` 을 생략 → 이 핸들러


def findUserSettingDefaults(repositoryRoot: Path) -> UserSettingDefaults:
    """C++ 스키마 읽기에서 생략값을 뽑습니다. 모양을 찾지 못하면 ValueError — 문서가 런타임과 다른 값을 적지 않게 멈춘다."""
    headerText = (repositoryRoot / _kUserSettingsSchemaHeader).read_text(encoding="utf-8")
    sourceText = (repositoryRoot / _kUserSettingsSchemaSource).read_text(encoding="utf-8")
    defaultMatch = _kApplyTimingDefaultRe.search(headerText)
    handlerMatch = _kKeyBindingHandlerRe.search(sourceText)
    mapTimingName = {enumerator: name for name, enumerator in _kApplyTimingNameRe.findall(sourceText)}
    if defaultMatch is None or handlerMatch is None or defaultMatch.group(1) not in mapTimingName:
        raise ValueError(f"[Config Reference] {_kUserSettingsSchemaHeader} 의 `_applyTiming{{ UserSettingApplyTiming::… }}` 기본값, "
                         f"{_kUserSettingsSchemaSource} 의 이름 표(`{{\"이름\", UserSettingApplyTiming::…}}`)와 `kKeyBindingHandler` 를 찾지 못했다 — "
                         "모양이 바뀌었으면 이 정규식을 고친다")
    return UserSettingDefaults(applyName=mapTimingName[defaultMatch.group(1)], keyBindingTarget=handlerMatch.group(1))


def makeUserSettingsPage(listSchema: list[tuple[str, list[dict[str, str]]]], defaults: UserSettingDefaults) -> str:
    """사용자 설정 표 — 스키마 파일마다 `<Setting>` 한 줄씩입니다. 생략한 속성은 런타임 값(@p defaults)으로 적는다."""
    lines = [_kGeneratedBanner, "", "# 사용자 설정 (플레이어 옵션)", "", "[설정 색인](README.md)", "",
             "플레이어 값은 `usersettings.json`(사용자 폴더)에 **기본값과 다른 것만** 쓰인다. `target` 이 `gv:` 면 그 전역 변수에 값을 넣는다 — "
             "기동 때 명령줄 `-gv_*` 로 준 변수는 덮지 않는다(명령줄이 이긴다, `docs/07_Configuration.md` 우선순위).", ""]
    for relative, listSetting in listSchema:
        lines += [f"## `{relative}`", "", "| id | 타입 | 기본값 | 범위 | 적용 | 대상 |", "|---|---|---|---|---|---|"]
        for setting in listSetting:
            rangeText = ""
            if "min" in setting or "max" in setting:
                rangeText = f"{setting.get('min', '-')} ~ {setting.get('max', '-')}"
                if "step" in setting:
                    rangeText += f" (눈금 {setting['step']})"
            # id · type · default 는 스키마의 필수 속성이다(없으면 KeyError). apply · target 생략은 런타임 생략값이다(`findUserSettingDefaults`).
            # target 이 없는 키 바인딩 밖의 설정은 대상이 없다 — 게임 코드가 값을 직접 읽는다.
            target = setting.get("target") or (defaults.keyBindingTarget if setting["type"] == "keyBinding" else "게임 코드")
            lines.append(f"| `{setting['id']}` | {setting['type']} | `{escapeCellInternal(setting['default'])}` | {rangeText} | "
                         f"{setting.get('apply') or defaults.applyName} | `{escapeCellInternal(target)}` |")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def findMacroValuesInternal(repositoryRoot: Path) -> dict[str, str]:
    """기본값 칸에 나오는 생성 매크로의 값입니다(`SW_RHI_BACKEND_DEFAULT` → 쿠킹 표의 기본 백엔드)."""
    contract = json.loads((repositoryRoot / kCookContractConfigRelative).read_text(encoding="utf-8"))
    return {"SW_RHI_BACKEND_DEFAULT": str(contract["default_rhi_backend"])}


def buildConfigReference(repositoryRoot: Path) -> tuple[dict[str, str], list[str]]:
    """생성 문서 전부(저장소 상대 경로 → 글)와 카탈로그 위반입니다. 같은 저장소면 바이트까지 같은 결과가 나옵니다."""
    reader = SourceTreeReader(repositoryRoot)
    index = buildSourceIndex(reader)
    mapMacro = findMacroValuesInternal(repositoryRoot)
    metadata: dict = {"files": [], "globalVariables": [], "arguments": [], "buildOptions": []}
    mapOutput: dict[str, str] = {}
    mapOutput[f"{kConfigReferenceDir}/README.md"] = makeIndexPage()
    setPage: set[str] = set()
    for entry in kListConfigFile:
        if not entry.page or entry.page in setPage or entry.page == "UserSettings":
            continue
        setPage.add(entry.page)
        mapOutput[f"{kConfigReferenceDir}/{entry.page}.md"] = makeEntryPage(repositoryRoot, entry, index, mapMacro, metadata)
    mapOutput[f"{kConfigReferenceDir}/UserSettings.md"] = makeUserSettingsPage(collectUserSettings(repositoryRoot),
                                                                                findUserSettingDefaults(repositoryRoot))
    mapOutput[f"{kConfigReferenceDir}/GlobalVariables.md"] = makeGlobalVariablePage(collectGlobalVariables(reader), metadata)
    mapOutput[f"{kConfigReferenceDir}/CommandLine.md"] = makeArgumentPage(collectArguments(repositoryRoot), metadata)
    mapOutput[f"{kConfigReferenceDir}/BuildOptions.md"] = makeBuildOptionPage(collectBuildOptions(reader), metadata)
    mapOutput[f"{kConfigReferenceDir}/{kConfigReferenceJson}"] = json.dumps(metadata, ensure_ascii=False, indent=1) + "\n"
    return mapOutput, findCatalogViolations(repositoryRoot, index) + findUndocumentedInternal(metadata)


def findUndocumentedInternal(metadata: dict) -> list[str]:
    """설명이 빈 설정 칸 · 명령줄 줄 · 전역 변수입니다 — 문서가 이름만 되풀이하는 표가 되지 않게 한다."""
    violations: list[str] = []
    for fileEntry in metadata["files"]:
        for typeEntry in fileEntry["types"]:
            for fieldEntry in typeEntry["fields"]:
                if not fieldEntry["description"]:
                    violations.append(f"[Config Reference] 설명 없는 칸: {typeEntry['name']}.{fieldEntry['name']} ({typeEntry['source']}) — "
                                      "선언 끝에 `///< …` 를 단다")
    for argument in metadata["arguments"]:
        if not argument["description"]:
            violations.append(f"[Config Reference] 설명 없는 명령줄 줄: {argument['name']} (Source/Core/Predefined/ArgumentList.xxx) — "
                              "줄 바로 위에 `// …` 를 단다")
    for variable in metadata["globalVariables"]:
        if not variable["description"]:
            violations.append(f"[Config Reference] 설명 없는 전역 변수: {variable['name']} ({variable['source']})")
    return violations
