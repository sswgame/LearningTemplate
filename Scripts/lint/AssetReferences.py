#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/AssetReferences.py

에셋 참조 검사 — 에셋은 경로 하나로 가리키므로(식별자 표 · `.meta` 는 없다, 언리얼 소프트 경로와 같다) 끊어진 경로를 커밋 전에 잡는다.

- 어떤 칸이 경로인지는 리플렉션 선언이 정하고 파서가 낸 표(`Config/Dev/AssetReferenceFields.generated.json`) 한 곳에 모인다. 도메인 · 확장자 · 임포트 폴더는
  `Config/Dev/AssetReferenceFields.json` 이다. 같은 파일을 C++ 검사(`App --check-asset-refs`,
  ctest `ToolCheckAssetRefs`)와 에디터 이름 바꾸기 · 옮기기(`AssetReferenceFields`), `py -3 -m Scripts move-asset` 이 읽는다.
  글 모양을 짐작하는 정규식으로 찾지 않는다 — 칸 이름이 목록에 있는 속성 · 요소 글 · JSON 키의 값만, 그 안에서도 도메인(`engine/` …)으로 시작하는 낱말만 경로다.
- 있음 판정은 **스테이지(인덱스)** 기준이다. 커밋하지 않은 파일은 없는 것으로 본다(작업 트리에만 있는 파일을 가리킨 커밋은 다른 체크아웃에서 끊긴다).
  임포트 산출물(`models/` · `textures/` · `heightfields/`)은 커밋하지 않으므로 같은 자리의 원본(`models_raw/<이름>.glb` 등)이 있으면 있다.
- 옮긴 에셋의 옛 id(도메인 뿌리의 `redirects.xml`, 런타임 `AssetRedirect`)는 엔진이 풀어 읽지만, 저장소 안의 참조는 새 id 로 고친다 — 남은 옛 참조를
  보고한다(언리얼 "Fix Up Redirectors" 자리). 리다이렉트는 저장소 밖(세이브 · 서버 · 배포된 팩)에 적힌 옛 경로를 살리려고 있다.
- 검사 범위는 인덱스의 모든 참조 파일(Content 의 글 에셋 + `listCMakeFile`)이다 — 지우거나 옮긴 에셋을 가리키던 다른 파일(역참조)도 같은 판정에서 잡힌다.
  참조 파일마다 뽑은 경로는 블롭 id 키 색인(`FactCache`)에 두어 커밋마다 바뀐 파일만 다시 읽는다. 저장소가 깨끗해야 하므로(ctest 가 지킨다) 걸린 것은 모두 이 커밋이 고칠 몫이다.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Git import resolveGitExecutable, runGit  # noqa: E402
from lint.FactCache import getIndexKeys, loadFacts  # noqa: E402
from lint.LintScope import LintScope  # noqa: E402

kConfigPath = "Config/Dev/AssetReferenceFields.json"
kGeneratedPath = "Config/Dev/AssetReferenceFields.generated.json"   # 리플렉션 파서가 낸 경로 칸 표 — Scripts/generate/AssetReferenceFieldList.py 가 쓴다
kContentFolder = "Content"
kRedirectFileName = "redirects.xml"   # 런타임 `AssetRedirect::kFileName` 과 같은 이름 — 도메인 뿌리마다 하나
# 경로 낱말을 가르는 글자(값 하나에 경로 여럿 — 쉼표 · 세미콜론 · 공백). 경로 안에는 오지 않는다(리소스 이름 규칙 `[a-z0-9_.-/]`).
kTokenSeparators = ",; \t\r\n#"  # `#` 뒤는 파일 안의 항목 id 다(`_base="common/data/items.xml#helmet_knight"`) - 참조는 앞의 파일이다
# 이 글자가 든 낱말은 경로가 아니라 틀이다(`{bind:…}` · CMake `${…}` · 와일드카드).
kTemplateCharacters = "{}$*%<>"


@dataclass(frozen=True)
class ImportedFolder:
    """임포트 산출물 폴더 하나 — 산출물은 커밋하지 않고 같은 자리의 원본 폴더에서 만든다."""

    folder: str
    sourceFolder: str
    listSourceExtension: tuple[str, ...]
    bGeneratedReferrers: bool   # 산출물 안의 글 에셋(임포터가 쓴 머티리얼 · 프리팹)은 참조자로 훑지 않는다 — 임포터의 몫이다


@dataclass(frozen=True)
class FieldConfig:
    """`Config/Dev/AssetReferenceFields.json` 의 내용."""

    listDomain: tuple[str, ...]
    listUncheckedDomain: tuple[str, ...]
    listTextExtension: tuple[str, ...]
    setField: frozenset[str]
    listImported: tuple[ImportedFolder, ...]
    listCMakeFile: tuple[str, ...]
    listCMakeArgument: tuple[str, ...]


@dataclass(frozen=True)
class Reference:
    """참조 하나 — 글 안 [start, end) 의 낱말이 경로 `path`(소문자 · `/`)다."""

    line: int
    field: str
    start: int
    end: int
    path: str


def _require(document: dict, key: str, kind: type):
    if key not in document or not isinstance(document[key], kind):
        raise ValueError(f"{kConfigPath}: '{key}' 가 없거나 {kind.__name__} 가 아닙니다")
    return document[key]


def loadConfig(projectRoot: Path) -> FieldConfig:
    """규칙(도메인 · 확장자 · 임포트 폴더 · CMake)과 칸 목록(파서가 낸 표 하나)을 읽습니다. 파일이 없거나 칸이 빠지면 예외다(기본값으로 검사하지 않는다)."""
    document = json.loads((projectRoot / kConfigPath).read_text(encoding="utf-8"))
    generated = json.loads((projectRoot / kGeneratedPath).read_text(encoding="utf-8"))
    setField: set[str] = set()
    for owner in _require(generated, "listOwner", list):
        setField.update(_require(owner, "listField", list))
    listImported = []
    for entry in _require(document, "listImportedFolder", list):
        listImported.append(ImportedFolder(_require(entry, "folder", str), _require(entry, "sourceFolder", str),
                                           tuple(_require(entry, "listSourceExtension", list)), _require(entry, "generatedReferrers", bool)))
    return FieldConfig(tuple(_require(document, "listDomain", list)), tuple(_require(document, "listUncheckedDomain", list)),
                       tuple(_require(document, "listTextExtension", list)), frozenset(setField), tuple(listImported),
                       tuple(_require(document, "listCMakeFile", list)), tuple(_require(document, "listCMakeArgument", list)))


# ----------------------------------------------------------------------------------------------------------------------------------
# 뽑기 — C++ `AssetReferenceFields::extract` 와 같은 규칙이다(둘을 고치면 함께 고친다)
# ----------------------------------------------------------------------------------------------------------------------------------
def _appendTokens(text: str, valueStart: int, valueEnd: int, field: str, config: FieldConfig, getLineOf: Callable[[int], int], out: list[Reference]) -> None:
    """값 [valueStart, valueEnd) 을 낱말로 갈라 도메인으로 시작하는 것만 참조로 더합니다."""
    cursor = valueStart
    while cursor < valueEnd:
        while cursor < valueEnd and text[cursor] in kTokenSeparators:
            cursor += 1
        tokenStart = cursor
        while cursor < valueEnd and text[cursor] not in kTokenSeparators:
            cursor += 1
        token = text[tokenStart:cursor]
        if not token or any(character in kTemplateCharacters for character in token):
            continue
        normalized = token.replace("\\", "/").lower()
        if any(normalized.startswith(domain + "/") for domain in config.listDomain):
            out.append(Reference(getLineOf(tokenStart), field, tokenStart, cursor, normalized))


def _makeLineOf(text: str) -> Callable[[int], int]:
    listLineStart = [0] + [match.end() for match in re.finditer("\n", text)]

    def getLineOf(offset: int) -> int:
        low, high = 0, len(listLineStart)
        while low + 1 < high:
            middle = (low + high) // 2
            if listLineStart[middle] <= offset:
                low = middle
            else:
                high = middle
        return low + 1
    return getLineOf


_kXMLAttribute = re.compile(r"([A-Za-z_][\w.:-]*)\s*=\s*(\"([^\"]*)\"|'([^']*)')")
_kXMLTagName = re.compile(r"</?\s*([A-Za-z_][\w.:-]*)")


def _extractXML(text: str, config: FieldConfig) -> list[Reference]:
    """XML: 목록에 든 이름의 속성 값과, 목록에 든 이름의 요소 안 글(`<_listX><item>경로</item></_listX>` 포함). 주석은 건너뛴다."""
    out: list[Reference] = []
    getLineOf = _makeLineOf(text)
    listOpenField: list[str] = []
    cursor = 0
    length = len(text)
    while cursor < length:
        tagStart = text.find("<", cursor)
        textEnd = length if tagStart < 0 else tagStart
        if listOpenField and textEnd > cursor:
            _appendTokens(text, cursor, textEnd, listOpenField[-1], config, getLineOf, out)
        if tagStart < 0:
            break
        if text.startswith("<!--", tagStart):
            commentEnd = text.find("-->", tagStart + 4)
            cursor = length if commentEnd < 0 else commentEnd + 3
            continue
        if text.startswith("<?", tagStart) or text.startswith("<!", tagStart):
            tagEnd = text.find(">", tagStart)
            cursor = length if tagEnd < 0 else tagEnd + 1
            continue
        tagEnd = _findTagEnd(text, tagStart)
        tag = text[tagStart:tagEnd]
        nameMatch = _kXMLTagName.match(tag)
        if nameMatch is None:
            cursor = tagStart + 1
            continue
        name = nameMatch.group(1)
        if tag.startswith("</"):
            if listOpenField and listOpenField[-1] == name:
                listOpenField.pop()
        else:
            for attribute in _kXMLAttribute.finditer(tag, nameMatch.end()):
                if attribute.group(1) in config.setField:
                    group = 3 if attribute.group(3) is not None else 4
                    _appendTokens(text, tagStart + attribute.start(group), tagStart + attribute.end(group), attribute.group(1), config, getLineOf, out)
            bSelfClosing = tag.rstrip(">").rstrip().endswith("/")
            if not bSelfClosing and (name in config.setField or (listOpenField and name == "item")):
                listOpenField.append(name if name in config.setField else listOpenField[-1])
        cursor = tagEnd
    return out


def _findTagEnd(text: str, tagStart: int) -> int:
    """`>` 다음 자리입니다. 따옴표 안의 `>` 는 건너뛴다."""
    quote = ""
    for index in range(tagStart + 1, len(text)):
        character = text[index]
        if quote:
            if character == quote:
                quote = ""
        elif character in "\"'":
            quote = character
        elif character == ">":
            return index + 1
    return len(text)


_kJSONKey = re.compile(r"\"([A-Za-z_][\w.-]*)\"\s*:\s*")
_kJSONString = re.compile(r"\"((?:[^\"\\]|\\.)*)\"")


def _extractJSON(text: str, config: FieldConfig) -> list[Reference]:
    """JSON: 목록에 든 키의 글 값, 또는 글 배열의 원소."""
    out: list[Reference] = []
    getLineOf = _makeLineOf(text)
    for keyMatch in _kJSONKey.finditer(text):
        field = keyMatch.group(1)
        if field not in config.setField:
            continue
        cursor = keyMatch.end()
        if cursor < len(text) and text[cursor] == "\"":
            valueMatch = _kJSONString.match(text, cursor)
            if valueMatch:
                _appendTokens(text, valueMatch.start(1), valueMatch.end(1), field, config, getLineOf, out)
        elif cursor < len(text) and text[cursor] == "[":
            arrayEnd = text.find("]", cursor)
            for valueMatch in _kJSONString.finditer(text, cursor, len(text) if arrayEnd < 0 else arrayEnd):
                _appendTokens(text, valueMatch.start(1), valueMatch.end(1), field, config, getLineOf, out)
    return out


def _extractCMake(text: str, config: FieldConfig) -> list[Reference]:
    """CMake: `listCMakeArgument` 의 인자(`--render-portraits=<경로>`) 값. 앞은 공백 · 따옴표 · 줄 처음이어야 한다."""
    out: list[Reference] = []
    getLineOf = _makeLineOf(text)
    for argument in config.listCMakeArgument:
        prefix = argument + "="
        start = text.find(prefix)
        while start >= 0:
            bBoundary = start == 0 or text[start - 1] in " \t\r\n\"("
            valueStart = start + len(prefix)
            valueEnd = valueStart
            while valueEnd < len(text) and text[valueEnd] not in " \t\r\n\")":
                valueEnd += 1
            if bBoundary:
                _appendTokens(text, valueStart, valueEnd, argument, config, getLineOf, out)
            start = text.find(prefix, valueEnd)
    return out


def extractReferences(relativePath: str, text: str, config: FieldConfig) -> list[Reference]:
    """파일 하나(저장소 기준 경로로 종류를 고른다)의 참조를 뽑습니다."""
    normalized = relativePath.replace("\\", "/")
    if normalized in config.listCMakeFile:
        return _extractCMake(text, config)
    if normalized.lower().endswith(".json"):
        return _extractJSON(text, config)
    return _extractXML(text, config)


# ----------------------------------------------------------------------------------------------------------------------------------
# 있음 판정
# ----------------------------------------------------------------------------------------------------------------------------------
class AssetSet:
    """있는 리소스 id(소문자, `Content/` 뒤)와 그 폴더들입니다."""

    def __init__(self, listAssetID: Iterable[str]):
        self.setFile: set[str] = set()
        self.setFolder: set[str] = set()
        for assetID in listAssetID:
            lowered = assetID.lower()
            self.setFile.add(lowered)
            slash = lowered.rfind("/")
            while slash > 0:
                folder = lowered[:slash]
                if folder in self.setFolder:
                    break
                self.setFolder.add(folder)
                slash = lowered.rfind("/", 0, slash)

    def contains(self, assetID: str) -> bool:
        return assetID in self.setFile or assetID in self.setFolder


def isImportedFromSource(assetID: str, assets: AssetSet, config: FieldConfig) -> bool:
    """`<…>/models/a/b/c.mesh` 처럼 임포트 산출물 자리면, 같은 자리의 원본(`<…>/models_raw/a/b/c.glb` → `a/b.glb` → `a.glb`)이 있는지 봅니다."""
    for imported in config.listImported:
        marker = "/" + imported.folder + "/"
        position = assetID.find(marker)
        while position >= 0:
            base = assetID[:position] + "/" + imported.sourceFolder + "/"
            listSegment = assetID[position + len(marker):].split("/")
            for count in range(len(listSegment), 0, -1):
                head = "/".join(listSegment[:count - 1])
                last = listSegment[count - 1]
                for stem in {last.split(".")[0], last.rsplit(".", 1)[0]}:
                    candidate = base + (head + "/" if head else "") + stem
                    if any(assets.contains(candidate + extension) for extension in imported.listSourceExtension):
                        return True
            position = assetID.find(marker, position + 1)
    return False


def resolves(assetID: str, assets: AssetSet, config: FieldConfig) -> bool:
    return assets.contains(assetID) or isImportedFromSource(assetID, assets, config)


def isChecked(reference: Reference, config: FieldConfig) -> bool:
    return not any(reference.path.startswith(domain + "/") for domain in config.listUncheckedDomain)


def isReferrer(relativePath: str, config: FieldConfig) -> bool:
    """참조를 훑는 파일인지 — Content 의 글 에셋(사본 도메인 · 임포터가 쓴 산출물 밖)과 CMake 파일."""
    normalized = relativePath.replace("\\", "/")
    if normalized in config.listCMakeFile:
        return True
    lowered = normalized.lower()
    if not lowered.startswith(kContentFolder.lower() + "/") or not lowered.endswith(config.listTextExtension):
        return False
    if lowered.rsplit("/", 1)[-1] == kRedirectFileName:
        return False   # 리다이렉트 표는 옛 id 를 적는 글이다 — 참조가 아니다(`from` 은 다른 형식의 경로 칸 이름이기도 하다, C++ `AssetReferenceFields::isReferrer` 와 같다)
    assetID = lowered[len(kContentFolder) + 1:]
    if any(assetID.startswith(domain + "/") for domain in config.listUncheckedDomain):
        return False
    return not any(("/" + imported.folder + "/") in "/" + assetID and imported.bGeneratedReferrers for imported in config.listImported)


# ----------------------------------------------------------------------------------------------------------------------------------
# 인덱스(스테이지) 읽기
# ----------------------------------------------------------------------------------------------------------------------------------
def listIndexFiles(projectRoot: Path) -> list[str]:
    result = runGit(["ls-files", "-z", "--cached"], cwd=projectRoot)
    if result.returnCode != 0:
        raise RuntimeError(f"git ls-files 실패: {result.stderr.strip()}")
    return [path for path in result.stdout.split("\0") if path]


def readIndexBlobs(projectRoot: Path, listRelativePath: list[str]) -> dict[str, str]:
    """인덱스의 내용(`:경로`)을 한 프로세스(`git cat-file --batch`)로 읽습니다."""
    gitExe = resolveGitExecutable() or "git"
    request = "".join(f":{path}\n" for path in listRelativePath).encode("utf-8")
    completed = subprocess.run([gitExe, "cat-file", "--batch"], input=request, cwd=projectRoot, capture_output=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"git cat-file 실패: {completed.stderr.decode('utf-8', 'replace').strip()}")
    output = completed.stdout
    mapText: dict[str, str] = {}
    cursor = 0
    for path in listRelativePath:
        headerEnd = output.index(b"\n", cursor)
        header = output[cursor:headerEnd].decode("utf-8", "replace").split()
        cursor = headerEnd + 1
        if len(header) < 3 or header[1] != "blob":
            raise RuntimeError(f"git cat-file: '{path}' 를 읽지 못했습니다({' '.join(header)})")
        size = int(header[2])
        mapText[path] = output[cursor:cursor + size].decode("utf-8", "replace")
        cursor += size + 1
    return mapText




def isRedirectTable(relativePath: str) -> bool:
    """`Content/<도메인>/redirects.xml`(게임 팩은 `Content/game/<팩>/redirects.xml`)인지."""
    parts = relativePath.replace("\\", "/").split("/")
    return parts[0] == kContentFolder and parts[-1] == kRedirectFileName and len(parts) in (3, 4)


def parseRedirects(mapText: dict[str, str]) -> dict[str, str]:
    """리다이렉트 표들의 옛 id → 새 id 입니다. 틀린 표는 런타임이 기동을 멈추므로 여기서는 읽히는 줄만 모은다."""
    mapOldToNew: dict[str, str] = {}
    for relativePath, text in mapText.items():
        try:
            root = ElementTree.fromstring(text)
        except ElementTree.ParseError:
            continue
        for node in root.iter("Redirect"):
            source, target = node.get("from"), node.get("to")
            if source and target:
                mapOldToNew[source] = target
    return mapOldToNew


def makeRedirectTableID(assetID: str) -> str:
    """리소스 id 가 속한 도메인의 표 id(`<도메인>/redirects.xml`) — 도메인은 첫 마디, `game/<팩>` · `dlc/<묶음>` 은 두 마디(C++ `AssetRedirect::makeTableID`)."""
    parts = assetID.split("/")
    domain = "/".join(parts[:2]) if parts[0] in ("game", "dlc") and len(parts) > 1 else parts[0]
    return f"{domain}/{kRedirectFileName}"


kRedirectIDCharacters = frozenset("abcdefghijklmnopqrstuvwxyz0123456789_.-/")


def addRedirects(tableText: str | None, listMove: list[tuple[str, str]]) -> str:
    """표 글(없으면 None)에 옮기기(옛 id, 새 id)를 더한 글을 돌려준다 — C++ `AssetRedirect::addToXMLText` 와 같은 규칙 · 같은 글 모양(에디터 옮기기와 diff 가 같게).

    옮긴 자리(새 id)를 옛 id 로 둔 줄은 뺀다(그 id 에 다시 에셋이 산다 — 남기면 되돌린 옮기기가 순환이 된다). 같은 옛 id 의 줄은 새 줄로 바꾼다. 사슬은 둔다.
    틀린 표 · 맞춤 꼴이 아닌 id · 순환이면 ValueError — 런타임이 기동을 멈출 표를 쓰지 않는다.
    """
    listEntry: list[tuple[str, str]] = []
    if tableText is not None:
        root = ElementTree.fromstring(tableText)   # 틀린 XML 은 ParseError(ValueError 가 아니다 — 부른 쪽이 그대로 멈춘다)
        if root.tag != "Redirects":
            raise ValueError(f"리다이렉트 표의 뿌리가 <Redirects> 가 아니다: <{root.tag}>")
        for node in root:
            if node.tag != "Redirect" or not node.get("from") or not node.get("to"):
                raise ValueError(f"리다이렉트 표에 틀린 줄이 있다: <{node.tag} {node.attrib}>")
            listEntry.append((node.get("from"), node.get("to")))
    for oldID, newID in listMove:
        for assetID in (oldID, newID):
            if not assetID or not set(assetID) <= kRedirectIDCharacters or assetID.startswith("/") or "//" in assetID or "/../" in f"/{assetID}/":
                raise ValueError(f"'{assetID}' 는 맞춤 꼴 리소스 id 가 아니다")
        if oldID == newID:
            raise ValueError(f"'{oldID}' 를 자기 자신으로 옮긴다")
        listEntry = [entry for entry in listEntry if entry[0] not in (oldID, newID)] + [(oldID, newID)]
    mapOldToNew = dict(listEntry)
    if len(mapOldToNew) != len(listEntry):
        raise ValueError("리다이렉트 표에 같은 옛 id 가 두 번 있다")
    for start in mapOldToNew:
        target, steps = mapOldToNew[start], 0
        while target in mapOldToNew:
            target, steps = mapOldToNew[target], steps + 1
            if steps > len(mapOldToNew):
                raise ValueError(f"리다이렉트가 '{start}' 에서 순환한다")
    return "<Redirects>\n" + "".join(f'\t<Redirect from="{source}" to="{target}" />\n' for source, target in listEntry) + "</Redirects>\n"


def collectProblems(listRelativePath: list[str], mapText: dict[str, str], assets: AssetSet, config: FieldConfig, mapRedirect: dict[str, str] | None = None) -> list[str]:
    """참조 파일들에서 없는 경로와 옮긴 에셋의 옛 id 를 `파일:줄: 칸 = '경로'` 로 모읍니다."""
    return collectReferenceProblems({path: extractReferences(path, mapText[path], config) for path in listRelativePath}, assets, config, mapRedirect)


def collectReferenceProblems(mapReference: dict[str, list[Reference]], assets: AssetSet, config: FieldConfig, mapRedirect: dict[str, str] | None = None) -> list[str]:
    listProblem: list[str] = []
    mapRedirect = mapRedirect or {}
    for relativePath, listReference in mapReference.items():
        for reference in listReference:
            if not isChecked(reference, config):
                continue
            if reference.path in mapRedirect:
                listProblem.append(f"{relativePath}:{reference.line}: {reference.field} = '{reference.path}' 는 옮긴 에셋의 옛 id 다 — '{mapRedirect[reference.path]}' 로 고친다")
            elif not resolves(reference.path, assets, config):
                listProblem.append(f"{relativePath}:{reference.line}: {reference.field} = '{reference.path}' 가 없습니다")
    return listProblem


def listMetaProblems(listIndex: list[str]) -> list[str]:
    """인덱스에 오른 `Content/**/*.meta` 입니다 — 에셋은 경로 하나로 가리키고 `.meta` 는 없다.
    `.gitignore` 는 새 파일만 막고, 옛 커밋을 옮기는 체리픽 · 리베이스는 그 파일을 그대로 들여온다(작업자 브랜치 병합에서 아홉 개가 다시 들어왔다)."""
    prefix = kContentFolder + "/"
    return [f"{path}: .meta 는 쓰지 않는다 — 지운다(에셋은 경로로만 가리킨다)" for path in listIndex if path.startswith(prefix) and path.endswith(".meta")]


def checkAssetReferences(projectRoot: Path, scope: LintScope) -> list[str]:
    """
    린트 규칙(`Rules.py`): 인덱스 전체의 끊어진 참조 — 지우거나 옮긴 에셋을 가리키던 다른 파일(역참조)까지 잡으려고 판정은 늘 전체로 낸다.
    참조 파일마다 뽑은 경로는 블롭 id 를 키로 한 색인(`FactCache`)에 두어 바뀐 파일만 다시 읽는다. 커밋 훅은 Content · CMake 참조 파일이 바뀌었을 때만 돈다
    (칸 목록이 바뀌면 `Rule.inputs` 가 저장소 전체로 돌린다).
    """
    config = loadConfig(projectRoot)
    if not scope.bWholeRepo and not scope.touches((kContentFolder,), files=config.listCMakeFile):
        return []
    mapKey = getIndexKeys(projectRoot)
    listIndex = list(mapKey)
    prefix = kContentFolder + "/"
    assets = AssetSet(path[len(prefix):] for path in listIndex if path.startswith(prefix))
    listReferrer = [path for path in listIndex if isReferrer(path, config)]
    listRedirect = [path for path in listIndex if isRedirectTable(path)]
    mapRedirect = parseRedirects(readIndexBlobs(projectRoot, listRedirect)) if listRedirect else {}
    mapFact = loadFacts(projectRoot, "asset-refs", [__file__, str(projectRoot / kConfigPath), str(projectRoot / kGeneratedPath)], listReferrer,
                        lambda path, text: [[reference.line, reference.field, reference.start, reference.end, reference.path] for reference in extractReferences(path, text, config)],
                        mapKey=mapKey, readTexts=lambda listPath: readIndexBlobs(projectRoot, listPath))
    mapReference = {path: [Reference(*row) for row in listRow] for path, listRow in mapFact.items()}
    return listMetaProblems(listIndex) + collectReferenceProblems(mapReference, assets, config, mapRedirect)


def checkWorkingTree(projectRoot: Path) -> list[str]:
    """작업 트리 전체 검사(손으로 돌릴 때) — 인덱스의 파일 목록, 작업 트리의 내용."""
    config = loadConfig(projectRoot)
    listIndex = [path for path in listIndexFiles(projectRoot) if (projectRoot / path).is_file()]
    prefix = kContentFolder + "/"
    assets = AssetSet(path[len(prefix):] for path in listIndex if path.startswith(prefix))
    listReferrer = [path for path in listIndex if isReferrer(path, config)]
    mapText = {path: (projectRoot / path).read_text(encoding="utf-8", errors="replace") for path in listReferrer}
    mapRedirect = parseRedirects({path: (projectRoot / path).read_text(encoding="utf-8", errors="replace") for path in listIndex if isRedirectTable(path)})
    return listMetaProblems(listIndex) + collectProblems(listReferrer, mapText, assets, config, mapRedirect)


def runSelfCheck() -> None:
    """리다이렉트 표 쓰기 규칙 — C++ `AssetRedirectTest.MovesAddLinesAndMovingBackReplacesThem` 과 같은 사례 · 같은 글."""
    assert makeRedirectTableID("game/empty/maps/a.scene.xml") == "game/empty/redirects.xml"
    assert makeRedirectTableID("common/data/a.xml") == "common/redirects.xml"
    first = addRedirects(None, [("common/data/a.xml", "common/data/b.xml")])
    assert first == '<Redirects>\n\t<Redirect from="common/data/a.xml" to="common/data/b.xml" />\n</Redirects>\n', first
    chained = addRedirects(first, [("common/data/b.xml", "common/data/c.xml")])
    assert parseRedirects({"t": chained}) == {"common/data/a.xml": "common/data/b.xml", "common/data/b.xml": "common/data/c.xml"}
    back = addRedirects(chained, [("common/data/c.xml", "common/data/a.xml")])
    assert parseRedirects({"t": back}) == {"common/data/b.xml": "common/data/c.xml", "common/data/c.xml": "common/data/a.xml"}, back
    for bad in (lambda: addRedirects("<Moves/>", [("common/a.xml", "common/b.xml")]), lambda: addRedirects(None, [("Common/A.xml", "common/b.xml")]),
                lambda: addRedirects(None, [("common/a.xml", "common/a.xml")])):
        try:
            bad()
        except ValueError:
            continue
        raise AssertionError("틀린 표 · id 를 받았다")


def main(argv: list[str] | None = None) -> int:
    import argparse
    from common.ExitCode import ExitCode
    from common.Paths import getProjectRoot
    parser = argparse.ArgumentParser(description="작업 트리의 에셋 참조(Config/Dev/AssetReferenceFields.json 의 칸)가 모두 있는지 본다")
    parser.add_argument("--self-check", action="store_true", help="리다이렉트 표 쓰기 규칙만 본다")
    if parser.parse_args(argv).self_check:
        runSelfCheck()
        print("[AssetReferences] self-check OK")
        return ExitCode.Passed
    listProblem = checkWorkingTree(getProjectRoot())
    for problem in listProblem:
        print(f"  - {problem}")
    print(f"[AssetReferences] {'FAIL' if listProblem else 'OK'} — 끊어진 참조 {len(listProblem)}건")
    return ExitCode.Violation if listProblem else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
