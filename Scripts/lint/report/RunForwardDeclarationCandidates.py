#!/usr/bin/env python3
"""
@file RunForwardDeclarationCandidates.py
@brief 헤더가 include 한 헤더의 타입을 포인터·참조로만 쓰는 자리를 찾아 보고합니다 — 전방 선언으로 바꿀 후보.

사용법:
    py -3 Scripts/lint/report/RunForwardDeclarationCandidates.py               # 보고만
    py -3 Scripts/lint/report/RunForwardDeclarationCandidates.py --filter Engine/Scene
    py -3 Scripts/lint/report/RunForwardDeclarationCandidates.py --apply       # 후보를 실제로 바꾼다 (빌드로 확인할 것)
    py -3 Scripts/lint/report/RunForwardDeclarationCandidates.py --apply --only Source/Engine/Foo.h

판정 규칙 (휴리스틱 — 최종 판정은 컴파일러가 한다. `--apply` 뒤에는 반드시 빌드와 RunHeaderSelfContained 를 돌린다):
  - include 한 헤더 P 가 정의하는 이름을 모은다: class/struct(템플릿 제외) · enum · using/typedef · 자유 함수 · 상수 · 매크로 · 템플릿.
  - 헤더 H 가 P 의 class/struct 이름을 **포인터 · 참조 · unique_ptr/shared_ptr/weak_ptr/vector<T*> · friend · 전방 선언**으로만 쓰고,
    P 의 다른 이름(enum · alias · 함수 · 상수 · 매크로 · 템플릿)을 하나도 쓰지 않으면 후보다.
  - P 가 아무것도 정의하지 않는 우산 헤더는 건드리지 않는다(그 아래 헤더의 이름을 쓰는지 알 수 없다).
  - `unique_ptr<T>` 멤버는 소멸자가 헤더 밖에 있어야 전방 선언으로 충분하다(삭제자가 T 의 정의를 본다). 헤더에 본문 없는 소멸자 선언
    (`~X();`)이 하나도 없으면 — 암시적이거나 헤더 안에서 정의하면 — `unique_ptr<T>` 를 값 사용으로 친다. 클래스마다가 아니라 헤더 단위라
    여전히 빌드가 마지막으로 거른다.

`--apply` 는 H 의 include 줄을 지우고, 같은 네임스페이스 블록 머리에 `class X;` 를 (struct → class, 알파벳 순으로) 넣고,
H 와 짝인 `.cpp` 에 그 include 를 옮겨 넣는다. 다른 TU 가 그 include 에 전이적으로 기대고 있었다면 빌드가 알려 준다 —
그 TU 에 직접 include 를 더하는 것이 이 저장소의 규칙이다(헤더 자립성).
"""
from __future__ import annotations

import argparse
import concurrent.futures as futures
import os
import re
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.HeaderSelfContained import (findHeaderProbeProblem, findSeedEntry, loadCompileDatabase, makeSeedIndex,  # noqa: E402
                                        runSyntaxOnly)

kRepositoryRoot = Path(__file__).resolve().parents[3]
kSourceRoot = kRepositoryRoot / "Source"

_kCommentBlock = re.compile(r"/\*.*?\*/", re.S)
_kCommentLine = re.compile(r"//[^\n]*")
_kStringLiteral = re.compile(r'"(?:\\.|[^"\\\n])*"')
_kInclude = re.compile(r'^[ \t]*#include[ \t]+"([^"]+)"[ \t]*$', re.M)
_kNamespaceOpen = re.compile(r"\bnamespace\s+([A-Za-z_][\w:]*)\s*\{")
_kClassDef = re.compile(r"\b(class|struct)\s+(?:SW_[A-Z_]*API\s+)?([A-Za-z_]\w*)\s*(?:final\s*)?(?::|\{)")
_kTemplateHead = re.compile(r"\btemplate\s*<")
_kEnumDef = re.compile(r"\benum\s+(?:class\s+|struct\s+)?([A-Za-z_]\w*)")
_kAliasDef = re.compile(r"\busing\s+([A-Za-z_]\w*)\s*=")
_kTypedefDef = re.compile(r"\btypedef\b[^;]*?\b([A-Za-z_]\w*)\s*;")
_kMacroDef = re.compile(r"^[ \t]*#define[ \t]+([A-Za-z_]\w*)", re.M)
_kFunctionDef = re.compile(r"^[ \t]*(?!return\b|else\b|case\b)(?:[A-Za-z_][\w:<>,\s\*&]*?)\s+\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:\{|;)", re.M)
_kConstantDef = re.compile(r"\b(?:inline\s+)?constexpr\s+[\w:<>]+\s+([A-Za-z_]\w*)\s*(?:=|\{|\[)")
_kForwardDecl = re.compile(r"^[ \t]*(class|struct)\s+([A-Za-z_]\w*)\s*;[ \t]*$", re.M)


def stripCode(text: str) -> str:
    """주석과 문자열 리터럴을 지운 본문. 줄 수는 유지하지 않는다."""
    text = _kCommentBlock.sub(" ", text)
    text = _kCommentLine.sub(" ", text)
    text = _kStringLiteral.sub('""', text)
    return text


@dataclass
class HeaderDefinitions:
    """헤더 하나가 정의하는 이름들."""

    path: Path
    mapClassNamespace: dict[str, str] = field(default_factory=dict)  # class/struct name -> namespace ("sw", "sw::editor", "")
    setClassIsStruct: set[str] = field(default_factory=set)
    setTemplate: set[str] = field(default_factory=set)
    setOther: set[str] = field(default_factory=set)  # enum · alias · 함수 · 상수 · 매크로 — 쓰이면 include 가 필요하다

    def definesAnything(self) -> bool:
        return bool(self.mapClassNamespace or self.setTemplate or self.setOther)


def parseDefinitions(path: Path) -> HeaderDefinitions:
    raw = path.read_text(encoding="utf-8", errors="replace")
    text = stripCode(raw)
    defs = HeaderDefinitions(path=path)

    for m in _kMacroDef.finditer(raw):
        defs.setOther.add(m.group(1))

    # 네임스페이스 추적 — 중괄호를 세며 걷는다
    index = 0
    stack: list[tuple[str, int]] = []  # (namespace name, depth at open)
    depth = 0
    length = len(text)
    while index < length:
        ch = text[index]
        if ch == "{":
            depth += 1
            index += 1
            continue
        if ch == "}":
            depth -= 1
            if stack and stack[-1][1] == depth:
                stack.pop()
            index += 1
            continue
        if ch == "n" and text.startswith("namespace", index):
            m = _kNamespaceOpen.match(text, index)
            if m:
                depth += 1
                stack.append((m.group(1), depth - 1))
                index = m.end()
                continue
            index += 1
            continue
        if ch in "cs" and (text.startswith("class", index) or text.startswith("struct", index)):
            # 템플릿 헤드가 바로 앞에 있으면 템플릿이다
            lookBack = text[max(0, index - 200):index]
            isTemplate = bool(re.search(r"template\s*<[^{};]*>\s*$", lookBack))
            m = _kClassDef.match(text, index)
            if m:
                name = m.group(2)
                namespace = "::".join(n for n, _ in stack)
                # 중첩 타입(클래스 안)은 전방 선언할 수 없다 — 네임스페이스 깊이와 중괄호 깊이가 다르면 중첩이다
                isNested = depth != len(stack)
                if isTemplate or isNested:
                    defs.setTemplate.add(name) if isTemplate else defs.setOther.add(name)
                else:
                    defs.mapClassNamespace[name] = namespace
                    if m.group(1) == "struct":
                        defs.setClassIsStruct.add(name)
                index = m.end() - 1
                continue
            index += 1
            continue
        index += 1

    for m in _kEnumDef.finditer(text):
        defs.setOther.add(m.group(1))
    for m in _kAliasDef.finditer(text):
        defs.setOther.add(m.group(1))
    for m in _kTypedefDef.finditer(text):
        defs.setOther.add(m.group(1))
    for m in _kConstantDef.finditer(text):
        defs.setOther.add(m.group(1))
    for m in _kFunctionDef.finditer(text):
        name = m.group(1)
        if name in ("if", "for", "while", "switch", "sizeof", "static_assert", "alignas", "decltype") or name in defs.mapClassNamespace:
            continue
        defs.setOther.add(name)
    return defs


_kPointerAfter = re.compile(r"^\s*(?:const\s+)?[\*&]")
_kSmartBefore = re.compile(r"(?:unique_ptr|shared_ptr|weak_ptr)\s*<\s*(?:const\s+)?$")
_kVectorPtrBefore = re.compile(r"(?:vector|list|span|unordered_set|unordered_map\s*<[^,]+,)\s*<?\s*(?:const\s+)?$")
_kFriendBefore = re.compile(r"friend\s+(?:class|struct)\s+$")
_kForwardBefore = re.compile(r"(?:^|\n)[ \t]*(?:class|struct)\s+$")


_kOutOfLineDestructor = re.compile(r"~\s*\w+\s*\(\s*\)\s*(?:override\s*)?;")


def classifyUses(text: str, name: str) -> tuple[int, int]:
    """(pointerLike, byValue) 사용 횟수."""
    pointerLike = 0
    byValue = 0
    # unique_ptr<T> 는 소멸자가 헤더 밖에 있을 때만 전방 선언으로 충분하다(모듈 머리말 참고).
    bUniquePtrNeedsDefinition = _kOutOfLineDestructor.search(text) is None
    for m in re.finditer(r"\b" + re.escape(name) + r"\b", text):
        before = text[max(0, m.start() - 80):m.start()]
        after = text[m.end():m.end() + 40]
        if _kForwardBefore.search(before) and after.lstrip().startswith(";"):
            pointerLike += 1  # 이미 전방 선언이 있다
            continue
        if _kFriendBefore.search(before):
            pointerLike += 1
            continue
        if _kPointerAfter.match(after):
            pointerLike += 1
            continue
        if _kSmartBefore.search(before) and after.lstrip().startswith(">"):
            if bUniquePtrNeedsDefinition and re.search(r"unique_ptr\s*<\s*(?:const\s+)?$", before):
                byValue += 1
            else:
                pointerLike += 1
            continue
        if _kVectorPtrBefore.search(before) and re.match(r"^\s*\*\s*>", after):
            pointerLike += 1
            continue
        byValue += 1
    return pointerLike, byValue


def resolveInclude(includePath: str) -> Path | None:
    candidate = kSourceRoot / includePath
    return candidate if candidate.is_file() else None


@dataclass
class Candidate:
    header: Path
    includeLine: str
    included: Path
    listName: list[tuple[str, str, bool]]  # (name, namespace, isStruct)


def findCandidates(listHeader: list[Path], cache: dict[Path, HeaderDefinitions]) -> tuple[list[Candidate], list[tuple[Path, str]]]:
    listCandidate: list[Candidate] = []
    listUnused: list[tuple[Path, str]] = []
    for header in listHeader:
        raw = header.read_text(encoding="utf-8", errors="replace")
        text = stripCode(raw)
        # 스스로 정의하는 것이 없는 헤더(CoreMinimal · pch 같은 우산)의 include 는 다시 내보내는 것이 목적이다.
        if header not in cache:
            cache[header] = parseDefinitions(header)
        bUmbrella = cache[header].definesAnything() is False
        for m in _kInclude.finditer(raw):
            includePath = m.group(1)
            included = resolveInclude(includePath)
            if included is None or included == header:
                continue
            # X-매크로 표(`.xxx`)는 펼치려고 include 하고, `#undef` 헤더는 부작용(앞선 OS 헤더의 매크로 지우기)이 목적이다 — 이름 쓰임으로 볼 수 없다.
            if bUmbrella or includePath.endswith(".xxx") or "#undef" in included.read_text(encoding="utf-8", errors="replace"):
                continue
            if included not in cache:
                cache[included] = parseDefinitions(included)
            defs = cache[included]
            if defs.definesAnything() is False:
                continue  # 우산 헤더
            # 헤더 본문에서 include 줄은 뺀다
            body = _kInclude.sub("", text)
            usesOther = any(re.search(r"\b" + re.escape(n) + r"\b", body) for n in defs.setOther | defs.setTemplate)
            if usesOther:
                continue
            listName: list[tuple[str, str, bool]] = []
            allPointer = True
            anyUse = False
            for name, namespace in defs.mapClassNamespace.items():
                pointerLike, byValue = classifyUses(body, name)
                if byValue > 0:
                    allPointer = False
                    break
                if pointerLike > 0:
                    anyUse = True
                    listName.append((name, namespace, name in defs.setClassIsStruct))
            if allPointer is False:
                continue
            if anyUse is False:
                listUnused.append((header, includePath))
                continue
            listCandidate.append(Candidate(header, m.group(0), included, listName))
    return listCandidate, listUnused


def applyCandidate(candidate: Candidate) -> bool:
    header = candidate.header
    text = header.read_text(encoding="utf-8")
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.split(newline)
    includeLine = candidate.includeLine.rstrip("\r")
    if includeLine not in lines:
        return False
    lines.remove(includeLine)

    # 네임스페이스마다 전방 선언을 넣는다
    byNamespace: dict[str, list[tuple[str, bool]]] = {}
    for name, namespace, isStruct in candidate.listName:
        byNamespace.setdefault(namespace, []).append((name, isStruct))
    for namespace, listEntry in byNamespace.items():
        openLine = "namespace %s" % namespace if namespace else None
        if openLine is None:
            return False
        try:
            openIndex = next(i for i, l in enumerate(lines) if l.strip() == openLine and i + 1 < len(lines) and lines[i + 1].strip() == "{")
        except StopIteration:
            # 블록이 없으면 첫 namespace 앞에 새 블록을 만든다
            try:
                firstNamespace = next(i for i, l in enumerate(lines) if l.startswith("namespace "))
            except StopIteration:
                return False
            block = [openLine, "{"]
            for name, isStruct in sorted(listEntry, key=lambda e: (not e[1], e[0])):
                block.append("    %s %s;" % ("struct" if isStruct else "class", name))
            block += ["} // namespace %s" % namespace, ""]
            lines[firstNamespace:firstNamespace] = block
            continue
        bodyStart = openIndex + 2
        # 기존 전방 선언 묶음(블록 머리의 연속된 class/struct X; 줄, 빈 줄 허용)을 모은다
        existing: list[tuple[str, bool]] = []
        scan = bodyStart
        lastDecl = bodyStart - 1
        while scan < len(lines):
            stripped = lines[scan].strip()
            fm = re.match(r"^(class|struct)\s+([A-Za-z_]\w*)\s*;$", stripped)
            if fm:
                existing.append((fm.group(2), fm.group(1) == "struct"))
                lastDecl = scan
                scan += 1
                continue
            if stripped == "":
                scan += 1
                continue
            break
        names = {n for n, _ in existing}
        for name, isStruct in listEntry:
            if name not in names:
                existing.append((name, isStruct))
        structs = sorted({n for n, s in existing if s})
        classes = sorted({n for n, s in existing if not s})
        block: list[str] = ["    struct %s;" % n for n in structs]
        if structs and classes:
            block.append("")
        block += ["    class %s;" % n for n in classes]
        if lastDecl >= bodyStart:
            # 기존 묶음을 통째로 바꾼다 (묶음 뒤의 빈 줄은 그대로)
            lines[bodyStart:lastDecl + 1] = block
        else:
            lines[bodyStart:bodyStart] = block + [""]
    header.write_text(newline.join(lines), encoding="utf-8")

    # 짝 cpp 에 include 를 옮긴다
    cpp = header.with_suffix(".cpp")
    if cpp.is_file():
        ctext = cpp.read_text(encoding="utf-8")
        if includeLine.strip() not in ctext:
            cnl = "\r\n" if "\r\n" in ctext else "\n"
            clines = ctext.split(cnl)
            insertAt = None
            for i, l in enumerate(clines):
                if l.startswith('#include "') and not l.endswith('pch.h"'):
                    insertAt = i + 1
            if insertAt is None:
                insertAt = 1
            clines.insert(insertAt, includeLine.strip())
            cpp.write_text(cnl.join(clines), encoding="utf-8")
    return True


def verifyUnusedIncludes(listUnused: list[tuple[Path, str]], buildDir: Path) -> dict[str, list[tuple[Path, str, str]]]:
    """
    "쓰임을 못 찾은 include" 를 실제 컴파일로 가린다 — 헤더를 그대로 한 번, 그 include 줄을 뺀 사본으로 한 번 단독 컴파일한다(`-fsyntax-only`).

    글자로만 보면 그 헤더를 **거쳐** 들어오는 이름(헤더가 다시 include 하는 것)에 기대는 include 와 정말 안 쓰는 include 를 가를 수 없다.
    결과: "removable"(빼도 선다) · "needed"(빼면 선다지 못한다 — 거쳐 오는 이름이 있다, 첫 오류를 함께) · "unknown"(원래 헤더부터 혼자 서지 못한다).
    """
    result: dict[str, list[tuple[Path, str, str]]] = {"removable": [], "needed": [], "unknown": []}
    problem = findHeaderProbeProblem(buildDir)
    if problem:
        print("[ForwardDeclaration] %s" % problem)
        return result
    seeds = makeSeedIndex(loadCompileDatabase(buildDir), buildDir, kRepositoryRoot)

    def compileText(header: Path, text: str, probeDir: Path, tag: str) -> str | None:
        entry = findSeedEntry(header.resolve().as_posix(), seeds)
        if entry is None:
            return "컴파일 DB 에 가까운 TU 가 없다"
        stem = header.relative_to(kSourceRoot).as_posix().replace("/", "_")[:-2]
        copy = probeDir / ("%s_%s.h" % (stem, tag))
        copy.write_text(text, encoding="utf-8")
        probe = probeDir / ("%s_%s_probe.cpp" % (stem, tag))
        probe.write_text('#include "%s"\n' % copy.as_posix(), encoding="utf-8")
        return runSyntaxOnly(entry, probe, buildDir)

    def classify(item: tuple[int, Path, str], probeDir: Path) -> tuple[str, Path, str, str]:
        index, header, includePath = item
        raw = header.read_text(encoding="utf-8", errors="replace")
        baseline = compileText(header, raw, probeDir, "base%d" % index)
        if baseline is not None:
            return "unknown", header, includePath, baseline
        without = re.sub(r'^[ \t]*#include[ \t]+"%s"[ \t]*\r?\n' % re.escape(includePath), "", raw, count=1, flags=re.M)
        error = compileText(header, without, probeDir, "without%d" % index)
        return ("removable", header, includePath, "") if error is None else ("needed", header, includePath, error)

    with tempfile.TemporaryDirectory(prefix="swUnusedInclude") as probeDirName:
        probeDir = Path(probeDirName)
        listItem = [(index, header, inc) for index, (header, inc) in enumerate(listUnused)]
        with futures.ThreadPoolExecutor(max_workers=max(1, (os.cpu_count() or 4) - 1)) as pool:
            for kind, header, inc, detail in pool.map(lambda item: classify(item, probeDir), listItem):
                result[kind].append((header, inc, detail))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="전방 선언으로 바꿀 수 있는 include 후보를 보고합니다")
    parser.add_argument("--filter", default="", help="경로에 이 문자열이 든 헤더만")
    parser.add_argument("--apply", action="store_true", help="후보를 실제로 바꾼다")
    parser.add_argument("--only", nargs="*", default=[], help="이 헤더들만 (저장소 상대 경로)")
    parser.add_argument("--skip", nargs="*", default=[], help="경로에 이 문자열이 든 헤더는 뺀다")
    parser.add_argument("--show-unused", action="store_true", help="정의된 이름의 쓰임을 못 찾은 include 도 보인다 (우산 · 기본형 헤더가 섞여 시끄럽다)")
    parser.add_argument("--verify-unused", action="store_true",
                        help="쓰임을 못 찾은 include 를 실제로 빼고 단독 컴파일해 '빼도 선다' 만 보인다 (컴파일 DB 필요, 몇 분)")
    parser.add_argument("--build", default="build/Ninja-Debug", help="--verify-unused 가 쓸 컴파일 DB 의 빌드 폴더")
    args = parser.parse_args()

    listHeader = sorted(p for p in kSourceRoot.rglob("*.h") if ".gen." not in p.name)
    if args.filter:
        listHeader = [p for p in listHeader if args.filter in p.as_posix()]
    if args.only:
        wanted = {(kRepositoryRoot / o).resolve() for o in args.only}
        listHeader = [p for p in listHeader if p.resolve() in wanted]
    for token in args.skip:
        listHeader = [p for p in listHeader if token not in p.as_posix()]

    cache: dict[Path, HeaderDefinitions] = {}
    listCandidate, listUnused = findCandidates(listHeader, cache)

    print("[ForwardDeclaration] 헤더 %d개 · 전방 선언 후보 %d건 · 쓰임을 못 찾은 include %d건" % (len(listHeader), len(listCandidate), len(listUnused)))
    for c in listCandidate:
        names = ", ".join(n for n, _, _ in c.listName)
        print("  %s  ←  %s  [%s]" % (c.header.relative_to(kRepositoryRoot).as_posix(), c.included.relative_to(kSourceRoot).as_posix(), names))
    if listUnused and args.verify_unused and not args.apply:
        buildDir = Path(args.build)
        if not buildDir.is_absolute():
            buildDir = kRepositoryRoot / buildDir
        verified = verifyUnusedIncludes(listUnused, buildDir)
        print("\n  쓰임을 못 찾은 include %d건을 컴파일로 가렸다: 빼도 선다 %d · 거쳐 오는 이름이 있다 %d · 헤더가 혼자 서지 못한다 %d"
              % (len(listUnused), len(verified["removable"]), len(verified["needed"]), len(verified["unknown"])))
        for header, inc, _ in sorted(verified["removable"]):
            print("  빼도 선다  %s  ←  %s" % (header.relative_to(kRepositoryRoot).as_posix(), inc))
        for header, inc, detail in sorted(verified["unknown"]):
            print("  모름      %s  ←  %s  (%s)" % (header.relative_to(kRepositoryRoot).as_posix(), inc, detail))
    elif listUnused and args.show_unused and not args.apply:
        print("\n  쓰임을 못 찾은 include (자동으로 바꾸지 않는다 — 손으로 볼 것):")
        for header, inc in listUnused:
            print("  %s  ←  %s" % (header.relative_to(kRepositoryRoot).as_posix(), inc))

    if args.apply:
        applied = 0
        for c in listCandidate:
            if applyCandidate(c):
                applied += 1
        print("\n[ForwardDeclaration] %d건을 바꿨다 — 빌드와 RunHeaderSelfContained 로 확인할 것" % applied)
    return 0


if __name__ == "__main__":
    sys.exit(main())
