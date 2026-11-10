#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/AssetIdentity.py

에셋 정체성(GUID) 검사와 정리 — C++ `AssetDatabase`(Source/Engine/Asset/AssetDatabase.h)와 같은 규칙 · 같은 규칙 파일(`Config/Dev/AssetIdentity.json`)이다.

  py -3 -m Scripts asset-ids              # 작업 트리 검사(결함을 찍는다)
  py -3 -m Scripts asset-ids --fix        # 없는 GUID 를 만들고, 짝 잃은 .meta 를 새 자리 원본에 다시 붙이고, 복사본에 새 GUID 를 준다
  py -3 -m Scripts asset-ids --self-check # 머리 읽기 · 쓰기 규칙(C++ AssetGUIDTest 와 같은 사례)과 규칙 파일 대조

- 우리 형식 글(`listHeaderSuffix` — XML)은 맨 앞 처리 지시 `<?asset guid="…"?>`(XML 선언 바로 뒤)에, 원본(png · ogg · glb …)은 곁의 `<파일>.meta`
  (`<AssetMeta _guid="…" _source="<파일 이름>" />`)에 정체성을 든다. GUID 는 소문자 16진 32 자 하나의 철자다.
- `--fix` 는 조용히 새 GUID 를 만들지 않는다: 머리를 잃은 글은 HEAD 의 같은 경로에 있던 GUID 를 되살리고, 짝 잃은 `.meta` 는 원본을 내용(HEAD 의 블롭 해시)
  · 이름으로 찾아 다시 붙인다. 붙일 곳이 하나로 정해지지 않으면 오류다. 새 GUID 는 정체성이 처음 생기는 파일(새 글 · 새 원본)과 복사본에만 준다.
- 커밋 훅(`checkStagedAssetIdentity` — PreCommit 이 부른다): 바뀐 파일과 그 짝의 머리 없음 · 짝 없음, 새 GUID 와 인덱스의 겹침(`git grep`), 그리고 HEAD 에 있던
  경로의 GUID 가 바뀐 것(한 커밋에서 같은 경로를 지우고 다시 만들었다 — 내용 바꾸기는 GUID 를 지킨다)을 막는다. 저장소 전체는 `asset-ids` 가 본다.
"""

from __future__ import annotations

import fnmatch
import json
import secrets
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from pathlib import Path
from xml.sax.saxutils import quoteattr

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Git import resolveGitExecutable, runGit  # noqa: E402

kRulesPath = "Config/Dev/AssetIdentity.json"
kReferenceFieldsPath = "Config/Dev/AssetReferenceFields.json"
kContentFolder = "Content"
kMetaExtension = ".meta"
kDeclaration = "<?xml"
kOpening = "<?asset"
kEndPI = "?>"
kGUIDKey = 'guid="'
kGUIDLength = 32
kHexDigits = frozenset("0123456789abcdef")

kSkip, kHeader, kSidecar, kSource = "skip", "header", "sidecar", "source"
kFound, kMissing, kInvalid = "found", "missing", "invalid"


@dataclass(frozen=True)
class Rules:
    """`Config/Dev/AssetIdentity.json` 의 내용(C++ `AssetIdentityRules`)."""

    listHeaderSuffix: tuple[str, ...]
    listSkipPattern: tuple[str, ...]
    listDerivedFolder: tuple[str, ...]


@dataclass(frozen=True)
class Issue:
    """결함 하나(C++ `AssetIdentityIssue`). `assetID` 는 Content 기준 리소스 id."""

    kind: str
    assetID: str
    other: str = ""

    def __str__(self) -> str:
        path = f"{kContentFolder}/{self.assetID}"
        return {
            "duplicate": f"{path}: GUID 가 {kContentFolder}/{self.other} 와 겹친다 — 복사본에는 새 GUID 를 준다(`asset-ids --fix`)",
            "missing-header": f"{path}: 머리 `<?asset guid?>` 가 없다(`asset-ids --fix`)",
            "invalid-header": f"{path}: 머리 `<?asset guid?>` 를 읽지 못했다 · 자리가 틀렸다 · 둘이다 — 손으로 고친다",
            "missing-meta": f"{path}: 곁에 .meta 가 없다(`asset-ids --fix`)",
            "orphan-meta": f"{path}: 원본이 없다 — 원본과 함께 옮기거나 지운다(`asset-ids --fix` 가 옮긴 원본을 내용 · 이름으로 찾아 다시 붙인다)",
            "invalid-meta": f"{path}: .meta 를 읽지 못했거나 다른 원본을 적었다 — 손으로 고친다",
            "unreadable": f"{path}: 읽지 못했다(UTF-8 이 아니다)",
            "changed": f"{path}: HEAD 의 GUID {self.other} 가 바뀌었다 — 같은 경로를 지우고 다시 만들었나? 내용 바꾸기는 GUID 를 지킨다(`asset-ids --fix` 는 바뀐 GUID 를 되돌리지 않는다 — HEAD 의 머리 · .meta 를 되살린다)",
        }[self.kind]


def loadRules(projectRoot: Path) -> Rules:
    document = json.loads((projectRoot / kRulesPath).read_text(encoding="utf-8"))
    listValue = []
    for key in ("listHeaderSuffix", "listSkipPattern", "listDerivedFolder"):
        value = document.get(key)
        if not isinstance(value, list) or not value or not all(isinstance(item, str) and item for item in value):
            raise ValueError(f"{kRulesPath}: '{key}' 는 빈 글이 없는 목록이어야 한다")
        listValue.append(tuple(item.lower() for item in value))
    return Rules(*listValue)


def classify(assetID: str, rules: Rules) -> str:
    lower = assetID.lower()
    if any(fnmatch.fnmatchcase(lower, pattern) for pattern in rules.listSkipPattern):
        return kSkip
    if lower.endswith(kMetaExtension):
        return kSidecar
    if lower.endswith(rules.listHeaderSuffix):
        return kHeader
    return kSource


def isGUID(text: str) -> bool:
    return len(text) == kGUIDLength and set(text) <= kHexDigits and set(text) != {"0"}


def createGUID() -> str:
    while True:
        guid = secrets.token_hex(kGUIDLength // 2)
        if isGUID(guid):
            return guid


def _skipSpace(text: str, position: int) -> int:
    while position < len(text) and text[position] in " \t\r\n":
        position += 1
    return position


def _startsPI(text: str, position: int, opening: str) -> bool:
    after = position + len(opening)
    return text.startswith(opening, position) and after < len(text) and (text[after] in " \t\r\n?")


def _findHead(text: str) -> tuple[int, int, int] | None:
    """(넣을 자리, 머리 시작, 머리 끝) — 머리가 없으면 시작 · 끝은 -1. XML 선언이 닫히지 않았으면 None."""
    position = _skipSpace(text, 1 if text.startswith("﻿") else 0)
    if _startsPI(text, position, kDeclaration):
        end = text.find(kEndPI, position)
        if end < 0:
            return None
        position = end + len(kEndPI)
        if text.startswith("\r", position):
            position += 1
        if text.startswith("\n", position):
            position += 1
    insertAt = position
    position = _skipSpace(text, position)
    if _startsPI(text, position, kOpening):
        end = text.find(kEndPI, position)
        if end >= 0:
            return insertAt, position, end + len(kEndPI)
    return insertAt, -1, -1


def readHeader(text: str) -> tuple[str, str | None]:
    """C++ `AssetGUIDHeader::read` 와 같은 규칙 — (found · missing · invalid, GUID)."""
    head = _findHead(text)
    if head is None:
        return kInvalid, None
    _, begin, end = head
    bStray = text.find(kOpening, end if begin >= 0 else 0) >= 0
    if begin < 0:
        return (kInvalid if bStray else kMissing), None
    body = text[begin + len(kOpening):end - len(kEndPI)]
    position = _skipSpace(body, 0)
    if not body.startswith(kGUIDKey, position):
        return kInvalid, None
    position += len(kGUIDKey)
    quote = body.find('"', position)
    if quote < 0 or _skipSpace(body, quote + 1) != len(body) or bStray or not isGUID(body[position:quote]):
        return kInvalid, None
    return kFound, body[position:quote]


def writeHeader(text: str, guid: str) -> str:
    """C++ `AssetGUIDHeader::write` 와 같은 규칙 — 머리가 있으면 그 자리를, 없으면 선언 뒤(없으면 맨 앞)에 한 줄."""
    head = _findHead(text)
    if head is None:
        raise ValueError("XML 선언이 닫히지 않았다")
    insertAt, begin, end = head
    header = f'{kOpening} guid="{guid}"{kEndPI}'
    if begin >= 0:
        return text[:begin] + header + text[end:]
    newLine = "\r\n" if "\r\n" in text else "\n"
    return text[:insertAt] + header + newLine + text[insertAt:]


def makeMetaText(sourceID: str, guid: str) -> str:
    """C++ `AssetMeta::makeText`(리플렉션 직렬화)와 같은 글."""
    return f"<AssetMeta _guid={quoteattr(guid)} _source={quoteattr(sourceID.rsplit('/', 1)[-1])} />"


def parseMeta(text: str) -> tuple[str, str] | None:
    """(GUID, 원본) — 다른 꼴(모르는 속성 · 자식 · 틀린 GUID)이면 None(C++ 은 엄격한 리플렉션 읽기)."""
    try:
        root = ElementTree.fromstring(text)
    except ElementTree.ParseError:
        return None
    if root.tag != "AssetMeta" or set(root.attrib) != {"_guid", "_source"} or len(root) or not isGUID(root.attrib["_guid"]):
        return None
    return root.attrib["_guid"], root.attrib["_source"]


def readIdentity(assetID: str, text: str | None, rules: Rules) -> str | None:
    """정체성을 드는 파일(머리 글 · .meta)의 GUID — 없거나 읽지 못하면 None."""
    kind = classify(assetID, rules)
    if text is None:
        return None
    if kind == kHeader:
        state, guid = readHeader(text)
        return guid if state == kFound else None
    if kind == kSidecar:
        meta = parseMeta(text)
        return meta[0] if meta else None
    return None


def collectIssues(mapText: dict[str, str | None], rules: Rules) -> tuple[list[Issue], dict[str, str]]:
    """C++ `AssetDatabase::build` 와 같은 규칙. `mapText` 는 리소스 id → 글(머리 글 · .meta 만, 읽지 못하면 None). 반환은 (결함, GUID → 리소스 id)."""
    listIssue: list[Issue] = []
    mapGUID: dict[str, str] = {}
    setSource = {assetID.lower() for assetID in mapText if classify(assetID, rules) == kSource}
    setSidecar = {assetID.lower() for assetID in mapText if classify(assetID, rules) == kSidecar}

    def register(guid: str, assetID: str) -> None:
        if guid in mapGUID:
            listIssue.append(Issue("duplicate", assetID, mapGUID[guid]))
        else:
            mapGUID[guid] = assetID

    for assetID in sorted(mapText):
        kind = classify(assetID, rules)
        text = mapText[assetID]
        if kind == kSkip:
            continue
        if kind in (kHeader, kSidecar) and text is None:
            listIssue.append(Issue("unreadable", assetID))
        elif kind == kHeader:
            state, guid = readHeader(text)
            if state == kFound:
                register(guid, assetID)
            else:
                listIssue.append(Issue("missing-header" if state == kMissing else "invalid-header", assetID))
        elif kind == kSidecar:
            sourceID = assetID[:-len(kMetaExtension)]
            if sourceID.lower() not in setSource:
                listIssue.append(Issue("orphan-meta", assetID))
                continue
            meta = parseMeta(text)
            if meta is None or meta[1] != sourceID.rsplit("/", 1)[-1]:
                listIssue.append(Issue("invalid-meta", assetID))
                continue
            register(meta[0], sourceID)
        elif (assetID + kMetaExtension).lower() not in setSidecar:
            listIssue.append(Issue("missing-meta", assetID))
    return listIssue, mapGUID


# --- git ------------------------------------------------------------------------------------------------------------------------


def _git(projectRoot: Path, listArgument: list[str], request: bytes | None = None) -> bytes:
    completed = subprocess.run([resolveGitExecutable() or "git", *listArgument], input=request, cwd=projectRoot, capture_output=True, check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"git {' '.join(listArgument[:2])} 실패: {completed.stderr.decode('utf-8', 'replace').strip()}")
    return completed.stdout


def readBlobs(projectRoot: Path, listSpec: list[str]) -> dict[str, bytes | None]:
    """`git cat-file --batch` 로 `<rev>:<경로>` 들을 읽습니다. 없는 것은 None."""
    output = _git(projectRoot, ["cat-file", "--batch"], "".join(f"{spec}\n" for spec in listSpec).encode("utf-8")) if listSpec else b""
    mapBlob: dict[str, bytes | None] = {}
    cursor = 0
    for spec in listSpec:
        headerEnd = output.index(b"\n", cursor)
        header = output[cursor:headerEnd].split()
        cursor = headerEnd + 1
        if len(header) < 3 or header[1] != b"blob":
            mapBlob[spec] = None   # `<spec> missing`
            continue
        size = int(header[2])
        mapBlob[spec] = output[cursor:cursor + size]
        cursor += size + 1
    return mapBlob


def _decode(data: bytes | None) -> str | None:
    if data is None:
        return None
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return None


def listWorktreeIDs(projectRoot: Path) -> list[str]:
    """작업 트리에 있는 Content 파일(추적 + 무시하지 않는 새 파일)의 리소스 id — 무시하는 임포트 산출물은 보지 않는다."""
    output = _git(projectRoot, ["ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", kContentFolder])
    prefix = kContentFolder + "/"
    listID = {path[len(prefix):] for path in output.decode("utf-8").split("\0") if path.startswith(prefix)}
    return sorted(assetID for assetID in listID if (projectRoot / prefix / assetID).is_file())


def readWorktreeTexts(projectRoot: Path, listID: list[str], rules: Rules) -> dict[str, str | None]:
    mapText: dict[str, str | None] = {}
    for assetID in listID:
        kind = classify(assetID, rules)
        mapText[assetID] = _decode((projectRoot / kContentFolder / assetID).read_bytes()) if kind in (kHeader, kSidecar) else ""
    return mapText


def readHeadIdentities(projectRoot: Path, listID: list[str], rules: Rules) -> dict[str, str]:
    """HEAD 의 같은 경로에 있던 GUID — 리소스 id(머리 글 · .meta) → GUID."""
    listSpec = [f"HEAD:{kContentFolder}/{assetID}" for assetID in listID]
    mapBlob = readBlobs(projectRoot, listSpec)
    mapGUID: dict[str, str] = {}
    for assetID, spec in zip(listID, listSpec):
        guid = readIdentity(assetID, _decode(mapBlob[spec]), rules)
        if guid:
            mapGUID[assetID] = guid
    return mapGUID


# --- 커밋 훅 --------------------------------------------------------------------------------------------------------------------


def _pairOf(assetID: str, rules: Rules) -> str | None:
    """원본 ↔ 곁의 .meta 짝 — 머리 글 · 건너뛰는 파일은 짝이 없다."""
    kind = classify(assetID, rules)
    if kind == kSource:
        return assetID + kMetaExtension
    if kind == kSidecar:
        return assetID[:-len(kMetaExtension)]
    return None


def _findGUIDHolders(projectRoot: Path, listGUID: list[str], rules: Rules) -> dict[str, list[str]]:
    """인덱스에서 GUID 들을 든 파일 — GUID → 정체성을 든 리소스 id 목록. `git grep` 이 인덱스를 훑는다(전체 블롭을 읽지 않는다 — 에셋이 수십만이어도 커밋마다 돈다)."""
    if not listGUID:
        return {}
    listArgument = ["grep", "--cached", "-l", "-F"]
    for guid in listGUID:
        listArgument += ["-e", guid]
    completed = subprocess.run([resolveGitExecutable() or "git", *listArgument, "--", kContentFolder], cwd=projectRoot, capture_output=True, check=False)
    if completed.returncode not in (0, 1):   # 1 은 "찾은 것 없음"
        raise RuntimeError(f"git grep 실패: {completed.stderr.decode('utf-8', 'replace').strip()}")
    prefix = kContentFolder + "/"
    listHit = [line[len(prefix):] for line in completed.stdout.decode("utf-8").splitlines() if line.startswith(prefix)]
    listHit = [assetID for assetID in listHit if classify(assetID, rules) in (kHeader, kSidecar)]
    mapBlob = readBlobs(projectRoot, [f":{prefix}{assetID}" for assetID in listHit])
    setWanted = set(listGUID)
    mapHolder: dict[str, list[str]] = {}
    for assetID in listHit:
        guid = readIdentity(assetID, _decode(mapBlob[f":{prefix}{assetID}"]), rules)
        if guid in setWanted:
            mapHolder.setdefault(guid, []).append(assetID if classify(assetID, rules) == kHeader else assetID[:-len(kMetaExtension)])
    return mapHolder


def checkStagedAssetIdentity(projectRoot: Path) -> list[str] | None:
    """
    커밋 전 검사 — 스테이지에 Content 변경이 없으면 None. 있으면 **바뀐 파일과 그 짝**만 본다(인덱스 전체를 읽지 않는다 — 에셋 수가 수십만이 되어도 커밋마다 돈다):
    머리 없음 · 깨짐, 원본만 지우거나 옮겨 남은 .meta, .meta 없는 새 원본, 새 GUID 가 인덱스의 다른 파일과 겹침(`git grep`), HEAD 에 있던 경로의 GUID 가 바뀜.
    저장소 전체는 `py -3 -m Scripts asset-ids` 가 본다(묶음 검증).
    """
    status = runGit(["diff", "--cached", "--name-status", "--no-renames"], cwd=projectRoot)
    if status.returnCode != 0:
        raise RuntimeError(f"git diff --cached 실패: {status.stderr.strip()}")
    prefix = kContentFolder + "/"
    listChange = [line.split("\t", 1) for line in status.stdout.splitlines() if "\t" in line]
    listChange = [(code.strip(), path.strip().replace("\\", "/")[len(prefix):]) for code, path in listChange if path.strip().replace("\\", "/").startswith(prefix)]
    if not listChange:
        return None
    rules = loadRules(projectRoot)
    setIndex = set(_git(projectRoot, ["ls-files", "-z", "--cached", "--", kContentFolder]).decode("utf-8").split("\0"))
    setLocal: set[str] = set()
    for _, assetID in listChange:
        for candidate in (assetID, _pairOf(assetID, rules)):
            if candidate and prefix + candidate in setIndex:
                setLocal.add(candidate)
    listCarrier = sorted(assetID for assetID in setLocal if classify(assetID, rules) in (kHeader, kSidecar))
    mapBlob = readBlobs(projectRoot, [f":{prefix}{assetID}" for assetID in listCarrier])
    mapText: dict[str, str | None] = {assetID: "" for assetID in setLocal}
    for assetID in listCarrier:
        mapText[assetID] = _decode(mapBlob[f":{prefix}{assetID}"])
    listIssue, mapGUID = collectIssues(mapText, rules)
    # 이 커밋의 GUID 가 인덱스의 다른 파일과 겹치는가(복사).
    for guid, listHolder in _findGUIDHolders(projectRoot, sorted(mapGUID), rules).items():
        for holder in sorted(listHolder):
            if holder != mapGUID[guid] and not any(issue.kind == "duplicate" and issue.assetID == holder for issue in listIssue):
                listIssue.append(Issue("duplicate", max(holder, mapGUID[guid]), min(holder, mapGUID[guid])))
    # HEAD 에 있던 경로의 GUID 가 바뀌었는가 — 고친(M) 파일만 본다(같은 경로를 지우고 다시 만들면 git 에는 고침으로 보인다).
    listModified = [assetID for code, assetID in listChange if code == "M" and classify(assetID, rules) in (kHeader, kSidecar)]
    mapHead = readHeadIdentities(projectRoot, listModified, rules)
    for assetID in listModified:
        current = readIdentity(assetID, mapText.get(assetID), rules)
        if assetID in mapHead and current and current != mapHead[assetID]:
            listIssue.append(Issue("changed", assetID, mapHead[assetID]))
    return [str(issue) for issue in listIssue]


# --- 고치기 ---------------------------------------------------------------------------------------------------------------------


def _hashObjects(projectRoot: Path, listPath: list[str]) -> dict[str, str]:
    """작업 트리 파일의 git 블롭 id(필터 적용 — LFS 포인터면 포인터의 id)."""
    if not listPath:
        return {}
    output = _git(projectRoot, ["hash-object", "--stdin-paths"], "".join(f"{path}\n" for path in listPath).encode("utf-8"))
    return dict(zip(listPath, output.decode("ascii").split()))


def _headBlobIDs(projectRoot: Path, listPath: list[str]) -> dict[str, str]:
    if not listPath:
        return {}
    output = _git(projectRoot, ["cat-file", "--batch-check"], "".join(f"HEAD:{path}\n" for path in listPath).encode("utf-8"))
    mapID: dict[str, str] = {}
    for path, line in zip(listPath, output.decode("utf-8").splitlines()):
        parts = line.split()
        if len(parts) >= 2 and parts[1] == "blob":
            mapID[path] = parts[0]
    return mapID


def _write(path: Path, text: str) -> None:
    path.write_bytes(text.encode("utf-8"))


def fixWorktree(projectRoot: Path) -> int:
    """`--fix` — 고친 뒤 남은 결함 수를 돌려준다(0 이면 깨끗하다)."""
    rules = loadRules(projectRoot)
    contentRoot = projectRoot / kContentFolder
    listID = listWorktreeIDs(projectRoot)
    mapText = readWorktreeTexts(projectRoot, listID, rules)
    listIssue, _ = collectIssues(mapText, rules)
    mapIssue: dict[str, list[Issue]] = {}
    for issue in listIssue:
        mapIssue.setdefault(issue.kind, []).append(issue)

    listCarrier = [assetID for assetID in listID if classify(assetID, rules) in (kHeader, kSidecar)]
    mapHead = readHeadIdentities(projectRoot, listCarrier, rules)
    setUsed = {guid for assetID in listCarrier if (guid := readIdentity(assetID, mapText[assetID], rules))}
    counts = {"header-new": 0, "header-restored": 0, "meta-new": 0, "meta-reattached": 0, "copy-renewed": 0}

    def setIdentity(assetID: str, guid: str) -> None:
        """머리 글이면 머리를, 원본이면 곁의 .meta 를 쓴다."""
        if classify(assetID, rules) == kHeader:
            text = writeHeader(mapText[assetID] or "", guid)
            _write(contentRoot / assetID, text)
            mapText[assetID] = text
        else:
            _write(contentRoot / (assetID + kMetaExtension), makeMetaText(assetID, guid))
        setUsed.add(guid)

    # ① 겹친 GUID — HEAD 에서 그 GUID 를 들던 경로가 원본이고 나머지가 복사본이다. 원본을 가를 수 없으면(둘 다 새 파일) 정렬 순서로 먼저인 쪽이 남는다.
    mapDuplicate: dict[str, list[str]] = {}
    for issue in mapIssue.get("duplicate", []):
        mapDuplicate.setdefault(issue.other, []).append(issue.assetID)
    def carrierOf(assetID: str) -> str:
        """정체성을 든 파일 — 머리 글은 그 자신, 원본은 곁의 .meta."""
        return assetID if classify(assetID, rules) == kHeader else assetID + kMetaExtension

    for first, listCopy in mapDuplicate.items():
        listHolder = [first] + listCopy
        guid = readIdentity(carrierOf(first), mapText.get(carrierOf(first)), rules)
        listOwner = [holder for holder in listHolder if mapHead.get(carrierOf(holder)) == guid]
        keep = listOwner[0] if len(listOwner) == 1 else first
        for holder in listHolder:
            if holder != keep:
                setIdentity(holder, createGUID())
                counts["copy-renewed"] += 1

    # ② 머리 없는 글 — HEAD 의 같은 경로에 있던 GUID 를 되살린다(편집 도구가 머리를 빼고 다시 썼다). 처음 생긴 글만 새 GUID.
    for issue in mapIssue.get("missing-header", []):
        restored = mapHead.get(issue.assetID)
        if restored and restored not in setUsed:
            setIdentity(issue.assetID, restored)
            counts["header-restored"] += 1
        else:
            setIdentity(issue.assetID, createGUID())
            counts["header-new"] += 1

    # ③ 짝 잃은 .meta — 원본을 옮긴 것이다. HEAD 의 옛 원본 블롭과 내용이 같은 새 원본, 없으면 이름이 같은 새 원본이 하나뿐일 때 다시 붙인다.
    listOrphan = [issue.assetID for issue in mapIssue.get("orphan-meta", [])]
    listLonely = [issue.assetID for issue in mapIssue.get("missing-meta", [])]
    mapLonelyBlob = _hashObjects(projectRoot, [f"{kContentFolder}/{assetID}" for assetID in listLonely])
    mapOldBlob = _headBlobIDs(projectRoot, [f"{kContentFolder}/{orphan[:-len(kMetaExtension)]}" for orphan in listOrphan])
    listUnattached: list[str] = []
    for orphan in listOrphan:
        oldSource = orphan[:-len(kMetaExtension)]
        meta = parseMeta(mapText.get(orphan) or "")
        oldBlob = mapOldBlob.get(f"{kContentFolder}/{oldSource}")
        listMatch = [assetID for assetID in listLonely if oldBlob and mapLonelyBlob.get(f"{kContentFolder}/{assetID}") == oldBlob]
        if len(listMatch) != 1:
            listMatch = [assetID for assetID in listLonely if assetID.rsplit("/", 1)[-1] == oldSource.rsplit("/", 1)[-1]]
        if meta is None or len(listMatch) != 1:
            listUnattached.append(orphan)
            continue
        target = listMatch[0]
        _write(contentRoot / (target + kMetaExtension), makeMetaText(target, meta[0]))
        (contentRoot / orphan).unlink()
        listLonely.remove(target)
        counts["meta-reattached"] += 1

    # ④ 남은 .meta 없는 원본 — 처음 들어온 원본이다.
    for assetID in listLonely:
        setIdentity(assetID, createGUID())
        counts["meta-new"] += 1

    print(f"[asset-ids] 고침 — 새 머리 {counts['header-new']} · 되살린 머리 {counts['header-restored']} · 새 .meta {counts['meta-new']} · "
          f"다시 붙인 .meta {counts['meta-reattached']} · 복사본 새 GUID {counts['copy-renewed']}")
    for orphan in listUnattached:
        print(f"  - {kContentFolder}/{orphan}: 다시 붙일 원본을 하나로 정하지 못했다 — 원본을 지웠으면 .meta 도 지우고, 옮겼으면 그 자리 곁으로 손으로 옮긴다")
    listLeft, _ = collectIssues(readWorktreeTexts(projectRoot, listWorktreeIDs(projectRoot), rules), rules)
    for issue in listLeft:
        print(f"  - {issue}")
    return len(listLeft)


def writeKeepingIdentity(path: Path, text: str) -> None:
    """생성기가 우리 형식 글을 통째로 다시 쓸 때 — 있던 파일의 머리(정체성)를 새 글에 옮겨 쓴다(C++ `XMLDocument::saveToFile` 과 같은 규칙)."""
    if path.is_file() and readHeader(text)[0] == kMissing:
        state, guid = readHeader(path.read_text(encoding="utf-8"))
        if state == kFound:
            text = writeHeader(text, guid)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def runSelfCheck(projectRoot: Path) -> None:
    """C++ `AssetGUIDTest` 와 같은 사례 · 규칙 파일의 산출물 폴더가 참조 칸 표의 임포트 폴더와 같은가."""
    sample = "0123456789abcdef0011223344556677"
    withDeclaration = writeHeader('<?xml version="1.0" encoding="utf-8"?>\n<!-- note -->\n<Root/>\n', sample)
    assert withDeclaration == f'<?xml version="1.0" encoding="utf-8"?>\n<?asset guid="{sample}"?>\n<!-- note -->\n<Root/>\n', withDeclaration
    assert readHeader(withDeclaration) == (kFound, sample)
    assert writeHeader("<Root/>\r\n", sample) == f'<?asset guid="{sample}"?>\r\n<Root/>\r\n'
    other = createGUID()
    assert readHeader(writeHeader(withDeclaration, other)) == (kFound, other) and len(writeHeader(withDeclaration, other)) == len(withDeclaration)
    assert readHeader('<?xml version="1.0"?>\n<Root/>')[0] == kMissing
    assert readHeader('<?asset guid="nope"?>\n<Root/>')[0] == kInvalid
    assert readHeader(f'<?asset guid="{sample}" extra="1"?>\n<Root/>')[0] == kInvalid
    assert readHeader(f'<Root/>\n<?asset guid="{sample}"?>')[0] == kInvalid
    assert readHeader(f'<?asset guid="{sample}"?>\n<?asset guid="{sample}"?>\n<Root/>')[0] == kInvalid
    assert readHeader('<?xml version="1.0"\n<Root/>')[0] == kInvalid
    assert not isGUID(sample.upper()) and not isGUID("0" * kGUIDLength) and not isGUID(sample[:-1])
    assert parseMeta(makeMetaText("a/b/hit.ogg", sample)) == (sample, "hit.ogg")
    assert parseMeta(f'<AssetMeta _guid="{sample}" _source="hit.ogg" _extra="1" />') is None

    rules = loadRules(projectRoot)
    imported = json.loads((projectRoot / kReferenceFieldsPath).read_text(encoding="utf-8"))["listImportedFolder"]
    setImported = {entry["folder"].lower() for entry in imported}
    assert set(rules.listDerivedFolder) == setImported, f"{kRulesPath} listDerivedFolder {sorted(rules.listDerivedFolder)} != {kReferenceFieldsPath} {sorted(setImported)}"
    listIssue, _ = collectIssues({"a/x.prefab.xml": writeHeader("<P/>", sample), "a/y.prefab.xml": writeHeader("<P/>", sample), "a/z.ogg": "",
                                  "a/w.ogg.meta": makeMetaText("a/w.ogg", other), "a/golden/q.ppm": ""}, rules)
    assert [(issue.kind, issue.assetID) for issue in listIssue] == [("orphan-meta", "a/w.ogg.meta"), ("duplicate", "a/y.prefab.xml"), ("missing-meta", "a/z.ogg")], listIssue


def main(argv: list[str] | None = None) -> int:
    import argparse
    from common.ExitCode import ExitCode
    from common.Paths import getProjectRoot
    parser = argparse.ArgumentParser(description="에셋 정체성(GUID — 우리 형식 글의 머리 · 원본의 .meta)을 검사하고 --fix 로 채운다")
    parser.add_argument("--fix", action="store_true", help="없는 GUID 를 만들고, 짝 잃은 .meta 를 다시 붙이고, 복사본에 새 GUID 를 준다")
    parser.add_argument("--self-check", action="store_true", help="머리 읽기 · 쓰기 규칙과 규칙 파일 대조만 본다")
    arguments = parser.parse_args(argv)
    projectRoot = getProjectRoot()
    if arguments.self_check:
        runSelfCheck(projectRoot)
        print("[asset-ids] self-check OK")
        return ExitCode.Passed
    if arguments.fix:
        left = fixWorktree(projectRoot)
        print(f"[asset-ids] {'FAIL' if left else 'OK'} — 남은 결함 {left}건")
        return ExitCode.Violation if left else ExitCode.Passed
    rules = loadRules(projectRoot)
    listIssue, mapGUID = collectIssues(readWorktreeTexts(projectRoot, listWorktreeIDs(projectRoot), rules), rules)
    for issue in listIssue:
        print(f"  - {issue}")
    print(f"[asset-ids] {'FAIL' if listIssue else 'OK'} — 정체성 {len(mapGUID)}개, 결함 {len(listIssue)}건")
    return ExitCode.Violation if listIssue else ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
