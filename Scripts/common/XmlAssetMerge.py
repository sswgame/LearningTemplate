#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
XML 에셋(씬 · 프리팹 · 머티리얼 · 카탈로그)의 의미 단위 비교와 3-way 병합 — 줄이 아니라 엔티티 id · 컴포넌트 · 속성으로 본다.

유니티 Smart Merge(UnityYAMLMerge)와 같은 자리다. 줄 단위 병합은 이 저장소의 XML 에서 두 가지로 무너진다:
  1. 엔진 저장기는 긴 시작 태그를 속성 하나당 한 줄로 접는다(`XMLDocument::wrapLongElementLines`) — 속성 하나를 고쳐도 정렬 공백이
     바뀌지 않으니 괜찮지만, **속성이 120 열을 넘나들면** 한 줄 ↔ 여러 줄로 바뀌어 같은 요소의 다른 속성을 고친 두 사람이 충돌한다.
  2. 두 사람이 같은 목록 끝에 엔티티 · 컴포넌트를 더하면 줄 병합은 늘 충돌한다. 의미로는 둘 다 넣으면 된다.

요소의 정체(key)는 부모 안에서 정한다: 씬 엔티티는 `id`, 컴포넌트는 `_componentName`(없으면 같은 타입의 몇 번째),
머티리얼 · 카탈로그 항목은 `id` · `name` · `key` · `layout` · `keyword` 가운데 처음 있는 것, 그 밖은 태그와 순번이다.
속성 순서 · 들여쓰기 · 줄 접기 · 주석 위치의 차이는 변경이 아니다.

출력 형식은 엔진 저장기와 같다(탭 들여쓰기 · 선언 없음 · 120 열 넘는 시작 태그 접기) — 그래서 병합 결과를 엔진이 다시 저장해도 줄이
바뀌지 않는다. 손으로 쓴 파일(선언 · 주석 · 공백 들여쓰기)은 그 들여쓰기와 선언 · 주석을 지키고 접지 않는다.
"""

from __future__ import annotations

import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field

#: 요소의 정체로 쓰는 속성 — 앞에서부터 처음 있는 것.
kKeyAttribute: tuple[str, ...] = ("id", "_componentName", "name", "key", "layout", "keyword")

#: 엔진 저장기가 시작 태그를 접는 열(`kXMLWrapColumn`).
kWrapColumn = 120

#: 병합 충돌을 남기는 주석의 머리 — 이 글이 든 주석이 남아 있으면 아직 풀지 않은 충돌이다.
kConflictMarker = "MERGE CONFLICT"


def isCommentInternal(node: ElementTree.Element) -> bool:
    return node.tag is ElementTree.Comment


@dataclass
class XmlAsset:
    """읽은 XML 에셋 하나 — 뿌리와 원래 서식(선언 유무 · 들여쓰기 단위 · 엔진 서식인가)."""

    root: ElementTree.Element
    bDeclaration: bool = False
    indentUnit: str = "\t"
    listLeadingComment: list[str] = field(default_factory=list)

    @property
    def bEngineFormat(self) -> bool:
        return self.indentUnit == "\t" and not self.bDeclaration


@dataclass(frozen=True)
class XmlChange:
    """비교 결과 한 줄. `kind` 는 added · removed · attribute · text."""

    kind: str
    path: str
    name: str = ""
    before: str | None = None
    after: str | None = None

    def describe(self) -> str:
        if self.kind == "added":
            return f"+ {self.path}"
        if self.kind == "removed":
            return f"- {self.path}"
        if self.kind == "text":
            return f"~ {self.path} text: {self.before!r} -> {self.after!r}"
        if self.before is None:
            return f"~ {self.path} @{self.name}: (added) {self.after!r}"
        if self.after is None:
            return f"~ {self.path} @{self.name}: {self.before!r} (removed)"
        return f"~ {self.path} @{self.name}: {self.before!r} -> {self.after!r}"


@dataclass(frozen=True)
class XmlConflict:
    """양쪽이 같은 것을 다르게 바꾼 자리."""

    path: str
    name: str
    base: str | None
    ours: str | None
    theirs: str | None

    def describe(self) -> str:
        where = f"{self.path} @{self.name}" if self.name else self.path
        return f"{where}: base={self.base!r} ours={self.ours!r} theirs={self.theirs!r}"


# ------------------------------------------------------------------------------
# 읽기 · 쓰기
# ------------------------------------------------------------------------------
def parseXmlAsset(text: str) -> XmlAsset:
    """XML 텍스트를 읽습니다. 주석을 지키고, 선언 유무와 들여쓰기 단위를 기억합니다. 틀린 XML 이면 ElementTree.ParseError 입니다."""
    text = text.replace("\r\n", "\n")
    builder = ElementTree.TreeBuilder(insert_comments=True)
    parser = ElementTree.XMLParser(target=builder)
    parser.feed(text)
    root = parser.close()
    stripped = text.lstrip("﻿")
    bDeclaration = stripped.startswith("<?xml")
    indentUnit = "\t"
    for line in stripped.split("\n")[1:]:
        if line.startswith("\t"):
            break
        if line.startswith(" ") and line.strip().startswith("<"):
            indentUnit = line[:len(line) - len(line.lstrip(" "))]
            break
    # 뿌리 앞의 주석(파일 머리 설명)은 파서가 버린다 — 따로 모아 다시 쓴다.
    listLeadingComment: list[str] = []
    body = stripped[stripped.find("?>") + 2:] if bDeclaration else stripped
    while True:
        body = body.lstrip()
        if not body.startswith("<!--"):
            break
        end = body.find("-->")
        if end < 0:
            break
        listLeadingComment.append(body[4:end])
        body = body[end + 3:]
    return XmlAsset(root, bDeclaration, indentUnit, listLeadingComment)


def escapeAttributeInternal(value: str) -> str:
    return (value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")
            .replace("\n", "&#10;").replace("\r", "&#13;").replace("\t", "&#9;"))


def escapeTextInternal(value: str) -> str:
    return value.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def wrapStartTagInternal(indent: str, tag: str, listAttribute: list[str], tail: str) -> str:
    """엔진 저장기와 같은 접기 — 120 열을 넘고 속성이 둘 이상이면 첫 속성 옆 열에 나머지를 세운다."""
    line = f"{indent}<{tag}{''.join(' ' + attribute for attribute in listAttribute)}{tail}"
    if len(line) <= kWrapColumn or len(listAttribute) < 2:
        return line
    alignment = " " * (len(tag) + 2)
    listLine = [f"{indent}<{tag} {listAttribute[0]}"]
    listLine.extend(f"{indent}{alignment}{attribute}" for attribute in listAttribute[1:])
    listLine[-1] += tail
    return "\n".join(listLine)


def serializeXmlAsset(asset: XmlAsset) -> str:
    """엔진 저장기와 같은 서식으로 씁니다(끝에 줄바꿈 하나). 손으로 쓴 파일은 들여쓰기 · 선언 · 주석을 지키고 접지 않습니다."""
    listLine: list[str] = []
    if asset.bDeclaration:
        listLine.append('<?xml version="1.0" encoding="utf-8"?>')
    listLine.extend(f"<!--{comment}-->" for comment in asset.listLeadingComment)
    bWrap = asset.bEngineFormat

    def writeInternal(element: ElementTree.Element, depth: int) -> None:
        indent = asset.indentUnit * depth
        if isCommentInternal(element):
            listLine.append(f"{indent}<!--{element.text or ''}-->")
            return
        listAttribute = [f'{name}="{escapeAttributeInternal(value)}"' for name, value in element.attrib.items()]
        listChild = list(element)
        text = (element.text or "").strip()
        if not listChild and not text:
            tail = " />"
            listLine.append(wrapStartTagInternal(indent, element.tag, listAttribute, tail) if bWrap
                            else f"{indent}<{element.tag}{''.join(' ' + a for a in listAttribute)}{tail}")
            return
        if not listChild:
            closing = f">{escapeTextInternal(text)}</{element.tag}>"
            listLine.append(wrapStartTagInternal(indent, element.tag, listAttribute, closing) if bWrap
                            else f"{indent}<{element.tag}{''.join(' ' + a for a in listAttribute)}{closing}")
            return
        listLine.append(wrapStartTagInternal(indent, element.tag, listAttribute, ">") if bWrap
                        else f"{indent}<{element.tag}{''.join(' ' + a for a in listAttribute)}>")
        for child in listChild:
            writeInternal(child, depth + 1)
        listLine.append(f"{indent}</{element.tag}>")

    writeInternal(asset.root, 0)
    return "\n".join(listLine) + "\n"


# ------------------------------------------------------------------------------
# 요소의 정체(key)
# ------------------------------------------------------------------------------
def computeChildKeys(parent: ElementTree.Element) -> list[tuple[str, ElementTree.Element]]:
    """부모 안에서 자식마다 정체를 정합니다(같은 부모 안에서 겹치지 않게 순번을 붙입니다)."""
    listKeyed: list[tuple[str, ElementTree.Element]] = []
    mapOccurrence: dict[str, int] = {}
    uniqueKey: set[str] = set()
    for child in parent:
        if isCommentInternal(child):
            base = f"!--{(child.text or '').strip()}"
        else:
            identity = next((child.get(name) for name in kKeyAttribute if child.get(name)), None)
            base = f"{child.tag}#{identity}" if identity is not None else f"{child.tag}"
        occurrence = mapOccurrence.get(base, 0)
        mapOccurrence[base] = occurrence + 1
        key = base if occurrence == 0 and "#" in base else f"{base}[{occurrence}]"
        while key in uniqueKey:
            occurrence += 1
            key = f"{base}[{occurrence}]"
        uniqueKey.add(key)
        listKeyed.append((key, child))
    return listKeyed


def describeElementInternal(key: str, element: ElementTree.Element) -> str:
    """경로 조각 — 엔티티는 이름을 덧붙여 읽기 쉽게 한다."""
    name = element.get("name") if not isCommentInternal(element) else None
    if element.tag == "entity" and name:
        return f"{key}({name})"
    return key


# ------------------------------------------------------------------------------
# 비교
# ------------------------------------------------------------------------------
def diffElements(before: ElementTree.Element, after: ElementTree.Element, path: str = "") -> list[XmlChange]:
    """두 요소(같은 정체)의 차이를 의미 단위로 모읍니다."""
    listChange: list[XmlChange] = []
    here = path or before.tag
    for name in list(before.attrib) + [name for name in after.attrib if name not in before.attrib]:
        valueBefore, valueAfter = before.get(name), after.get(name)
        if valueBefore != valueAfter:
            listChange.append(XmlChange("attribute", here, name, valueBefore, valueAfter))
    textBefore, textAfter = (before.text or "").strip(), (after.text or "").strip()
    if textBefore != textAfter and not isCommentInternal(before):
        listChange.append(XmlChange("text", here, "", textBefore, textAfter))
    mapBefore = dict(computeChildKeys(before))
    mapAfter = dict(computeChildKeys(after))
    for key, child in mapBefore.items():
        childPath = f"{here}/{describeElementInternal(key, child)}"
        if key not in mapAfter:
            if not isCommentInternal(child):
                listChange.append(XmlChange("removed", childPath))
        else:
            listChange.extend(diffElements(child, mapAfter[key], childPath))
    for key, child in mapAfter.items():
        if key not in mapBefore and not isCommentInternal(child):
            listChange.append(XmlChange("added", f"{here}/{describeElementInternal(key, child)}"))
    return listChange


def diffAssets(before: XmlAsset, after: XmlAsset) -> list[XmlChange]:
    if before.root.tag != after.root.tag:
        return [XmlChange("removed", before.root.tag), XmlChange("added", after.root.tag)]
    return diffElements(before.root, after.root)


# ------------------------------------------------------------------------------
# 3-way 병합
# ------------------------------------------------------------------------------
def deepCopyInternal(element: ElementTree.Element) -> ElementTree.Element:
    if isCommentInternal(element):
        return ElementTree.Comment(element.text)
    copy = ElementTree.Element(element.tag, dict(element.attrib))
    copy.text = element.text
    for child in element:
        copy.append(deepCopyInternal(child))
    return copy


def elementsEqualInternal(left: ElementTree.Element | None, right: ElementTree.Element | None) -> bool:
    if left is None or right is None:
        return left is right
    return not diffElements(left, right) and left.tag == right.tag


class XmlMerger:
    """3-way 병합 한 번. `prefer` 가 "ours" · "theirs" 면 충돌을 그쪽으로 풀고 주석을 남기지 않는다."""

    def __init__(self, prefer: str | None = None) -> None:
        if prefer not in (None, "ours", "theirs"):
            raise ValueError(f"prefer must be ours or theirs, not {prefer!r}")
        self.prefer = prefer
        self.listConflict: list[XmlConflict] = []

    def recordConflictInternal(self, target: ElementTree.Element, conflict: XmlConflict) -> None:
        self.listConflict.append(conflict)
        if self.prefer is None:
            target.insert(0, ElementTree.Comment(f" {kConflictMarker}: {conflict.describe()} "))

    def pickInternal(self, ours: str | None, theirs: str | None) -> str | None:
        return theirs if self.prefer == "theirs" else ours

    def mergeScalarInternal(self, base: str | None, ours: str | None, theirs: str | None) -> tuple[str | None, bool]:
        if ours == theirs:
            return ours, False
        if ours == base:
            return theirs, False
        if theirs == base:
            return ours, False
        return self.pickInternal(ours, theirs), True

    def mergeElement(self, base: ElementTree.Element | None, ours: ElementTree.Element, theirs: ElementTree.Element,
                     path: str) -> ElementTree.Element:
        """정체가 같은 세 요소를 합칩니다(base 가 None 이면 양쪽이 따로 더한 것)."""
        if isCommentInternal(ours):
            return deepCopyInternal(ours)
        merged = ElementTree.Element(ours.tag)
        listConflictHere: list[XmlConflict] = []
        listName = list(ours.attrib) + [name for name in theirs.attrib if name not in ours.attrib]
        for name in listName:
            baseValue = base.get(name) if base is not None else None
            value, bConflict = self.mergeScalarInternal(baseValue, ours.get(name), theirs.get(name))
            if bConflict:
                listConflictHere.append(XmlConflict(path, name, baseValue, ours.get(name), theirs.get(name)))
            if value is not None:
                merged.set(name, value)
        baseText = (base.text or "").strip() if base is not None else None
        text, bConflict = self.mergeScalarInternal(baseText, (ours.text or "").strip(), (theirs.text or "").strip())
        if bConflict:
            listConflictHere.append(XmlConflict(path, "#text", baseText, (ours.text or "").strip(), (theirs.text or "").strip()))
        merged.text = text or None

        for child in self.mergeChildrenInternal(base, ours, theirs, path):
            merged.append(child)
        for conflict in reversed(listConflictHere):
            self.recordConflictInternal(merged, conflict)
        return merged

    def mergeChildrenInternal(self, base: ElementTree.Element | None, ours: ElementTree.Element, theirs: ElementTree.Element,
                              path: str) -> list[ElementTree.Element]:
        listBase = computeChildKeys(base) if base is not None else []
        listOurs = computeChildKeys(ours)
        listTheirs = computeChildKeys(theirs)
        mapBase, mapOurs, mapTheirs = dict(listBase), dict(listOurs), dict(listTheirs)

        # 순서: 우리 쪽 순서에, 우리에게 없는 저쪽 것(저쪽이 더한 것 · 우리가 지운 것)을 저쪽에서 바로 앞에 있던 것 뒤에 끼운다.
        # 우리가 지운 것도 넣어 봐야 "지움 대 고침" 충돌을 본다 — 결과에 남을지는 아래가 정한다.
        listKey = [key for key, _ in listOurs]
        for index, (key, _) in enumerate(listTheirs):
            if key in mapOurs:
                continue
            previousKey = next((listTheirs[back][0] for back in range(index - 1, -1, -1) if listTheirs[back][0] in listKey), None)
            listKey.insert(listKey.index(previousKey) + 1 if previousKey is not None else 0, key)

        listMerged: list[ElementTree.Element] = []
        for key in listKey:
            baseChild, oursChild, theirsChild = mapBase.get(key), mapOurs.get(key), mapTheirs.get(key)
            childPath = f"{path}/{describeElementInternal(key, oursChild if oursChild is not None else theirsChild)}"
            if oursChild is not None and theirsChild is not None:
                listMerged.append(self.mergeElement(baseChild, oursChild, theirsChild, childPath))
            elif oursChild is not None:
                if baseChild is None:
                    listMerged.append(deepCopyInternal(oursChild))  # 우리만 더했다
                elif not elementsEqualInternal(baseChild, oursChild) and not isCommentInternal(oursChild):
                    # 저쪽은 지웠고 우리는 고쳤다 — 고친 것을 남기고 알린다.
                    kept = deepCopyInternal(oursChild)
                    if self.prefer == "theirs":
                        self.listConflict.append(XmlConflict(childPath, "", "present", "modified", "deleted"))
                        continue
                    self.recordConflictInternal(kept, XmlConflict(childPath, "", "present", "modified", "deleted"))
                    listMerged.append(kept)
                # 고치지 않았는데 저쪽이 지웠다 — 지운다
            elif theirsChild is not None:
                if baseChild is None:
                    listMerged.append(deepCopyInternal(theirsChild))  # 저쪽만 더했다
                elif not elementsEqualInternal(baseChild, theirsChild) and not isCommentInternal(theirsChild):
                    kept = deepCopyInternal(theirsChild)
                    if self.prefer == "ours":
                        self.listConflict.append(XmlConflict(childPath, "", "present", "deleted", "modified"))
                        continue
                    self.recordConflictInternal(kept, XmlConflict(childPath, "", "present", "deleted", "modified"))
                    listMerged.append(kept)
        return listMerged


def mergeAssets(base: XmlAsset, ours: XmlAsset, theirs: XmlAsset, prefer: str | None = None) -> tuple[XmlAsset, list[XmlConflict]]:
    """3-way 병합. 결과의 서식은 우리 쪽을 따릅니다. 충돌은 우리 쪽 값(`prefer` 가 있으면 그쪽)을 두고 주석으로 표시합니다."""
    merger = XmlMerger(prefer)
    if ours.root.tag != theirs.root.tag:
        merger.listConflict.append(XmlConflict(ours.root.tag, "#root", base.root.tag, ours.root.tag, theirs.root.tag))
        return XmlAsset(deepCopyInternal(ours.root), ours.bDeclaration, ours.indentUnit, list(ours.listLeadingComment)), merger.listConflict
    mergedRoot = merger.mergeElement(base.root, ours.root, theirs.root, ours.root.tag)
    listComment = list(ours.listLeadingComment) + [comment for comment in theirs.listLeadingComment if comment not in ours.listLeadingComment
                                                     and comment not in base.listLeadingComment]
    return XmlAsset(mergedRoot, ours.bDeclaration, ours.indentUnit, listComment), merger.listConflict
