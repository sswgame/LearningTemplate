#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Source 아래 폴더 · 파일을 옮기고 그 경로를 적은 모든 곳을 새 경로로 다시 쓴다.

왜 있는가: 폴더 이동 단위가 여럿이면 손으로 옮길 때마다 include · CMake · 문서 · 데이터의 옛 경로가 남는다. 별칭 헤더나 옛 경로 리더는 두지 않는다 —
데이터를 다시 쓰고, 옛 경로 문자열이 하나라도 남으면 실패한다(`build/` · `graphify-out/` 같은 우리 코드가 아닌 폴더는 보지 않는다).

표(`--map`)는 TOML 의 `[move]` 하나, 키가 옛 경로 · 값이 새 경로이고 둘 다 `Source/` 기준이다(폴더도 파일도 된다).

  [move]
  "Editor/Workspace" = "Editor/Workspace/GUI"

순서: (1) 옛 경로가 있고 새 경로가 비었는지 본다 (2) 추적 텍스트에서 `Source/<옛>` · 따옴표 · 꺾쇠 바로 뒤의 `<옛>` 을 새 경로로 바꾸고, 옮긴 파일과 옛 형제 사이의 맨이름
include(`"Foo.h"`)는 전체 경로로 바꾼다 (3) CMake 글롭이 맞히는 파일 수가 옮긴 뒤 줄면 실패한다 (4) 옛 경로가 남으면 실패한다 — 여기까지는 아무것도 쓰지 않는다
(5) 쓰고 `git mv`(대소문자만 다른 이동 · 자기 안으로의 이동은 임시 이름을 거친다) (6) 바뀐 C++ 파일의 include 순서를 고친다 (7) 디스크에서 옛 경로를 다시 찾는다
(8) 손댄 파일 목록을 찍는다 — 병렬 단위들이 같은 파일을 건드리는지 목록 교집합으로 본다.

  py -3 -m Scripts move-sources --map Moves.toml --dry-run   # 쓰지 않고 할 일과 손댈 파일만 찍는다
  py -3 -m Scripts move-sources --map Moves.toml
"""

from __future__ import annotations

import argparse
import fnmatch
import functools
import posixpath
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.Constants import kNotOurDirNames  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.Process import runProcess  # noqa: E402
from common.RuleData import RuleDataError, readTomlText  # noqa: E402
from common.Search import collectRepositoryFiles  # noqa: E402

_kTextSuffixes = (".h", ".cpp", ".inl", ".in", ".cmake", ".py", ".toml", ".json", ".md", ".po", ".xml")
_kTextFileNames = ("CMakeLists.txt",)
_kCppSuffixes = frozenset({".h", ".cpp", ".inl"})
_kSourcePrefix = "Source/"
_kIncludeRe = re.compile(r'(#[ \t]*include[ \t]*)([<"])([^>"\r\n]+)([>"])')
_kGlobRe = re.compile(r"\bfile\s*\(\s*(GLOB_RECURSE|GLOB)\s+[^\s)]+([^)]*)\)", re.IGNORECASE)
_kGlobKeywords = frozenset({"CONFIGURE_DEPENDS", "LIST_DIRECTORIES", "FOLLOW_SYMLINKS", "TRUE", "FALSE", "RELATIVE"})
_kBoundary = r"(?![\w-]|\.\w)"


class MoveError(Exception):
    """표 · 상태가 이동할 수 없는 모양이다. 메시지가 그대로 출력된다."""

    def __init__(self, message: str, exitCode: ExitCode = ExitCode.Violation) -> None:
        super().__init__(message)
        self.exitCode = exitCode


@dataclass(frozen=True)
class MoveContext:
    """한 번의 이동 계획 — 저장소 뿌리, 옛 → 새(`Source/` 포함 저장소 기준 경로), 이동 전 파일 집합."""

    root: Path
    mapMove: dict[str, str]
    setFile: frozenset[str]


# --- 표 · 상태 ------------------------------------------------------------------


def loadMoveMapInternal(mapPath: Path) -> dict[str, str]:
    """TOML 의 `[move]` 를 `Source/` 를 붙인 저장소 기준 경로 표로 읽는다."""
    try:
        table = readTomlText(mapPath.read_text(encoding="utf-8"), str(mapPath)).get("move")
    except (OSError, RuleDataError) as error:
        raise MoveError(f"표를 읽지 못했습니다: {mapPath} ({error})", ExitCode.CheckInvalid) from error
    if not isinstance(table, dict) or not table:
        raise MoveError(f"표에 비지 않은 [move] 가 없습니다: {mapPath}", ExitCode.CheckInvalid)
    mapMove: dict[str, str] = {}
    for old, new in table.items():
        if not isinstance(new, str) or not old.strip("/") or not new.strip("/"):
            raise MoveError(f"[move] 항목이 문자열 경로 쌍이 아닙니다: {old!r} = {new!r}", ExitCode.CheckInvalid)
        mapMove[_kSourcePrefix + old.strip("/")] = _kSourcePrefix + new.strip("/")
    return mapMove


def listRepositoryFilesInternal(root: Path) -> frozenset[str]:
    """추적 파일 + 추적 대상이 될 파일(git 이 무시하지 않는 것)의 저장소 기준 경로 집합."""
    result = runProcess(["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=root)
    if result.returnCode != 0:
        raise MoveError(f"git ls-files 실패: {result.stderr.strip()}", ExitCode.PrerequisiteMissing)
    return frozenset(name for name in result.stdout.split("\0") if name and ((root / name).exists() or (root / name).is_symlink()))


def isUnderInternal(rel: str, folder: str) -> bool:
    return rel == folder or rel.startswith(folder + "/")


def mapPathInternal(rel: str, mapMove: dict[str, str]) -> str:
    """파일 경로가 옮겨진 뒤의 경로. 옮겨지지 않으면 그대로."""
    for old in sorted(mapMove, key=len, reverse=True):
        if isUnderInternal(rel, old):
            return mapMove[old] + rel[len(old):]
    return rel


def validateMovesInternal(context: MoveContext) -> None:
    """(1) 옛 경로는 있고 새 경로는 비었는지 — 아니면 멈춘다."""
    listOld = sorted(context.mapMove)
    for old in listOld:
        new = context.mapMove[old]
        if not any(isUnderInternal(name, old) for name in context.setFile):
            raise MoveError(f"옛 경로에 파일이 없습니다: {old}")
        if any(isUnderInternal(other, old) for other in listOld if other != old):
            raise MoveError(f"표의 옛 경로가 서로 겹칩니다: {old}")
        bCaseOnly = old.lower() == new.lower()
        if any(isUnderInternal(name, new) for name in context.setFile):
            raise MoveError(f"새 경로가 이미 있습니다: {new}")
        if not bCaseOnly and (context.root / new).exists():
            raise MoveError(f"새 경로가 이미 디스크에 있습니다: {new}")
    if len(set(context.mapMove.values())) != len(context.mapMove):
        raise MoveError("표의 새 경로가 서로 같습니다")


# --- 글 다시 쓰기 ----------------------------------------------------------------


def buildPathRegexInternal(mapMove: dict[str, str]) -> re.Pattern[str]:
    """`Source/<옛>` 과 따옴표 · 꺾쇠 바로 뒤의 `<옛>`(Source 기준 옛 include 경로)을 찾는다."""
    alternation = "|".join(re.escape(old[len(_kSourcePrefix):]) for old in sorted(mapMove, key=len, reverse=True))
    return re.compile(rf"(?P<src>(?<![\w.-])Source/(?:{alternation}){_kBoundary})|(?P<quo>(?<=[\"<])(?:{alternation}){_kBoundary})")


def rewritePrefixInternal(path: str, mapMove: dict[str, str]) -> str:
    """Source 기준 경로 문자열의 앞이 옮겨지는 폴더면 새 경로로."""
    return mapPathInternal(_kSourcePrefix + path, mapMove)[len(_kSourcePrefix):]


def rewriteIncludeInternal(inc: str, includerOld: str, context: MoveContext) -> str:
    """include 경로 하나의 새 글. Source 기준 경로 · 옛 형제 상대 경로(맨이름 포함) 모두 파일로 풀어 옮겨진 곳으로 다시 쓴다."""
    sourceTarget = _kSourcePrefix + inc
    relativeTarget = posixpath.normpath(posixpath.join(posixpath.dirname(includerOld), inc))
    if sourceTarget in context.setFile:
        return mapPathInternal(sourceTarget, context.mapMove)[len(_kSourcePrefix):]
    if relativeTarget not in context.setFile:
        return rewritePrefixInternal(inc, context.mapMove)
    newTarget = mapPathInternal(relativeTarget, context.mapMove)
    newIncluderDir = posixpath.dirname(mapPathInternal(includerOld, context.mapMove))
    if posixpath.normpath(posixpath.join(newIncluderDir, inc)) == newTarget:
        return inc
    if newTarget.startswith(_kSourcePrefix):
        return newTarget[len(_kSourcePrefix):]
    return posixpath.relpath(newTarget, newIncluderDir)


def rewriteTextInternal(text: str, includerOld: str, context: MoveContext, pathRe: re.Pattern[str]) -> str:
    """한 번의 걷기로 include 줄과 나머지 경로 글을 바꾼다(바꾼 글을 다시 바꾸지 않는다)."""
    if "include" not in text and not any(old[len(_kSourcePrefix):] in text for old in context.mapMove):
        return text   # 정규식이 큰 파일 수천 개를 훑는 시간을 아낀다 — 이 글에는 바꿀 것이 없다
    combined = re.compile(f"{_kIncludeRe.pattern}|{pathRe.pattern}")

    def replace(match: re.Match[str]) -> str:
        if match.group(3) is not None:
            return f"{match.group(1)}{match.group(2)}{rewriteIncludeInternal(match.group(3), includerOld, context)}{match.group(4)}"
        found = match.group("src") or match.group("quo")
        if match.group("src"):
            return mapPathInternal(found, context.mapMove)
        return rewritePrefixInternal(found, context.mapMove)

    return combined.sub(replace, text)


def readTextInternal(path: Path) -> str:
    # 못 읽는 파일(UTF-8 이 아님)은 멈춘다 — 건너뛰면 그 파일만 옛 경로로 남는다.
    try:
        with path.open("r", encoding="utf-8", errors="strict", newline="") as file:
            return file.read()
    except UnicodeDecodeError as error:
        raise MoveError(f"UTF-8 로 읽지 못하는 텍스트 파일: {path} ({error})", ExitCode.CheckInvalid) from error


def collectTextFilesInternal(root: Path, setFile: frozenset[str]) -> list[str]:
    """치환 대상 텍스트의 저장소 기준 경로 — `collectRepositoryFiles` 가 걷고, git 이 무시하는 파일은 뺀다."""
    listPath = collectRepositoryFiles(root, suffixes=_kTextSuffixes, fileNames=_kTextFileNames)
    return [rel for rel in (path.relative_to(root).as_posix() for path in listPath) if rel in setFile]


def rewriteAllInternal(context: MoveContext, listText: list[str]) -> tuple[dict[str, str], dict[str, str]]:
    """(원본 글, 바뀐 글만) — 둘 다 옛 경로 키."""
    pathRe = buildPathRegexInternal(context.mapMove)
    mapOriginal: dict[str, str] = {}
    mapChanged: dict[str, str] = {}
    for rel in listText:
        text = readTextInternal(context.root / rel)
        mapOriginal[rel] = text
        newText = rewriteTextInternal(text, rel, context, pathRe)
        if newText != text:
            mapChanged[rel] = newText
    return mapOriginal, mapChanged


# --- 검사 ----------------------------------------------------------------------


def findLeftoversInternal(mapText: dict[str, str], mapMove: dict[str, str]) -> list[str]:
    """(5) 옛 경로 문자열이 남은 곳. 새 경로가 옛 경로로 시작하는 이동(`A` → `A/B`)의 새 글은 남은 것이 아니다."""
    pathRe = buildPathRegexInternal(mapMove)
    listLeft: list[str] = []
    for rel, text in sorted(mapText.items()):
        for match in pathRe.finditer(text):
            found = match.group("src") or match.group("quo")
            bSource = match.group("src") is not None
            oldFull = found if bSource else _kSourcePrefix + found
            newFull = mapPathInternal(oldFull, mapMove)
            newFound = newFull if bSource else newFull[len(_kSourcePrefix):]
            if text[match.start():match.start() + len(newFound)] != newFound:
                listLeft.append(f"{rel}: {found}")
                break
    return listLeft


def parseGlobsInternal(cmakeRel: str, text: str) -> list[tuple[bool, str]]:
    """CMake 글 속 `file(GLOB[_RECURSE] …)` 의 (재귀 여부, 저장소 기준 패턴). 변수를 못 푸는 패턴은 `?` 로 시작해 돌려준다."""
    listGlob: list[tuple[bool, str]] = []
    cmakeDir = posixpath.dirname(cmakeRel)
    textNoComment = re.sub(r"(?m)^[ \t]*#.*$", "", text)
    for match in _kGlobRe.finditer(textNoComment):
        bRecurse = match.group(1).upper() == "GLOB_RECURSE"
        for token in match.group(2).split():
            pattern = token.strip('"')
            if pattern.upper() in _kGlobKeywords or pattern in ("${CMAKE_CURRENT_SOURCE_DIR}", "${CMAKE_CURRENT_LIST_DIR}"):   # 폴더 자체(LIST_DIRECTORIES 의 대상) — 파일 패턴이 아니다
                continue
            listGlob.append((bRecurse, resolveGlobPatternInternal(pattern, cmakeDir)))
    return listGlob


def resolveGlobPatternInternal(pattern: str, cmakeDir: str) -> str:
    for variable, base in (("${CMAKE_CURRENT_SOURCE_DIR}/", cmakeDir), ("${CMAKE_CURRENT_LIST_DIR}/", cmakeDir), ("${CMAKE_SOURCE_DIR}/", "")):
        if pattern.startswith(variable):
            return posixpath.normpath(posixpath.join(base, pattern[len(variable):]))
    if "${" in pattern:
        return "?" + pattern
    return posixpath.normpath(posixpath.join(cmakeDir, pattern))


@functools.lru_cache(maxsize=None)
def compileGlobInternal(pattern: str) -> re.Pattern[str]:
    return re.compile(fnmatch.translate(pattern))


def indexByDirInternal(setFile: frozenset[str]) -> dict[str, list[str]]:
    mapDir: dict[str, list[str]] = {}
    for name in setFile:
        mapDir.setdefault(posixpath.dirname(name), []).append(name)
    return mapDir


def countGlobMatchesInternal(bRecurse: bool, pattern: str, setFile: frozenset[str], mapDir: dict[str, list[str]]) -> int:
    """CMake `file(GLOB)` 는 `*` 가 `/` 를 넘지 않고 `GLOB_RECURSE` 는 넘는다."""
    compiled = compileGlobInternal(pattern)
    directory = posixpath.dirname(pattern)
    if bRecurse or any(char in directory for char in "*?["):
        return sum(1 for name in setFile if compiled.match(name) and (bRecurse or name.count("/") == pattern.count("/")))
    return sum(1 for name in mapDir.get(directory, ()) if compiled.match(name))


def checkGlobCountsInternal(context: MoveContext, mapOriginal: dict[str, str], mapChanged: dict[str, str]) -> tuple[list[str], list[str]]:
    """(7) 같은 CMake 글롭이 맞히는 파일 수를 옮기기 전 · 후로 센다. (줄어든 글롭 목록, 평가 못 한 패턴 목록)"""
    setAfter = frozenset(mapPathInternal(name, context.mapMove) for name in context.setFile)
    mapDirBefore, mapDirAfter = indexByDirInternal(context.setFile), indexByDirInternal(setAfter)
    listShrunk: list[str] = []
    listSkipped: list[str] = []
    for rel, text in sorted(mapOriginal.items()):
        if posixpath.basename(rel) != "CMakeLists.txt" and not rel.endswith(".cmake"):
            continue
        listBefore = parseGlobsInternal(rel, text)
        listAfter = parseGlobsInternal(mapPathInternal(rel, context.mapMove), mapChanged.get(rel, text))
        for (bRecurse, before), (_, after) in zip(listBefore, listAfter):
            if before.startswith("?"):
                if rel.startswith(_kSourcePrefix) or mapPathInternal(rel, context.mapMove) != rel:   # 옮기는 소스 쪽 CMake 의 것만 알린다
                    listSkipped.append(f"{rel}: {before[1:]}")
                continue
            countBefore = countGlobMatchesInternal(bRecurse, before, context.setFile, mapDirBefore)
            countAfter = countGlobMatchesInternal(bRecurse, after, setAfter, mapDirAfter)
            if countAfter < countBefore:
                listShrunk.append(f"{rel}: {'GLOB_RECURSE' if bRecurse else 'GLOB'} {before} → {after} ({countBefore} → {countAfter}개)")
    return listShrunk, listSkipped


# --- 쓰기 ----------------------------------------------------------------------


def runGitInternal(root: Path, *arguments: str) -> None:
    result = runProcess(["git", *arguments], cwd=root)
    if result.returnCode != 0:
        raise MoveError(f"git {' '.join(arguments)} 실패: {result.stderr.strip() or result.stdout.strip()}", ExitCode.PrerequisiteMissing)


def moveEntryInternal(root: Path, old: str, new: str) -> None:
    """`git mv` — 대소문자만 바뀌거나 자기 안으로 들어가면 같은 이름이 겹치므로 임시 이름을 거친다."""
    (root / new).parent.mkdir(parents=True, exist_ok=True)
    if old.lower() == new.lower() or new.startswith(old + "/"):
        temporary = old + ".movetmp"
        runGitInternal(root, "mv", old, temporary)
        (root / new).parent.mkdir(parents=True, exist_ok=True)
        runGitInternal(root, "mv", temporary, new)
    else:
        runGitInternal(root, "mv", old, new)


def writeAndMoveInternal(context: MoveContext, mapChanged: dict[str, str]) -> None:
    """바뀐 글을 옛 위치에 쓴 뒤 옮긴다."""
    for rel, text in mapChanged.items():
        with (context.root / rel).open("w", encoding="utf-8", newline="") as file:
            file.write(text)
    for old in sorted(context.mapMove):
        moveEntryInternal(context.root, old, context.mapMove[old])


def fixIncludeOrderInternal(context: MoveContext, listChangedNew: list[str]) -> list[str]:
    """(6) 바뀐 C++ 파일에 include 순서 정리(`FormatIncludeOrder` 와 같은 변환)를 돌린다. 더 바뀐 파일 목록."""
    from lint import IncludeOrderRules
    headerMaps = IncludeOrderRules.buildHeaderLookupMap(context.root)
    listFixed: list[str] = []
    for rel in listChangedNew:
        if posixpath.splitext(rel)[1].lower() not in _kCppSuffixes:
            continue
        text = readTextInternal(context.root / rel)
        newText, bChanged = IncludeOrderRules.fixedTextOf(lambda source: IncludeOrderRules.inspectSource(source, headerMaps), text, rel)
        if bChanged:
            with (context.root / rel).open("w", encoding="utf-8", newline="") as file:
                file.write(newText)
            listFixed.append(rel)
    return listFixed


# --- 진입 ----------------------------------------------------------------------


def planMovesInternal(root: Path, mapMove: dict[str, str]) -> tuple[MoveContext, dict[str, str], dict[str, str], list[str]]:
    """쓰기 전 단계 (1) (2) (3) (4) — 하나라도 어긋나면 MoveError. (맥락, 원본 글, 바뀐 글, 평가 못 한 글롭)"""
    context = MoveContext(root, mapMove, listRepositoryFilesInternal(root))
    validateMovesInternal(context)
    mapOriginal, mapChanged = rewriteAllInternal(context, collectTextFilesInternal(root, context.setFile))
    listShrunk, listSkipped = checkGlobCountsInternal(context, mapOriginal, mapChanged)
    if listShrunk:
        raise MoveError("CMake 글롭이 맞히는 파일이 줄었습니다(일부만 옮기면 조용히 빠진다 — 표에 나머지를 더하거나 글롭을 고치십시오):\n  " + "\n  ".join(listShrunk))
    listLeft = findLeftoversInternal({rel: mapChanged.get(rel, text) for rel, text in mapOriginal.items()}, mapMove)
    if listLeft:
        raise MoveError("고친 뒤에도 옛 경로가 남습니다:\n  " + "\n  ".join(listLeft))
    return context, mapOriginal, mapChanged, listSkipped


def printReportInternal(context: MoveContext, listTouched: list[str], listSkipped: list[str], bDryRun: bool) -> None:
    print(f"[MoveSources] {'(dry-run) ' if bDryRun else ''}이동 {len(context.mapMove)}건, 손댄 파일 {len(listTouched)}개")
    for old, new in sorted(context.mapMove.items()):
        print(f"  move {old} -> {new}")
    for rel in listTouched:
        print(f"  touched {rel}")
    for item in listSkipped:
        print(f"  [경고] 변수를 풀지 못해 세지 못한 글롭: {item}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Source 폴더 · 파일을 옮기고 옛 경로를 적은 모든 곳을 새 경로로 다시 쓴다(표: TOML [move])")
    parser.add_argument("--map", required=True, type=Path, help="이동 표 TOML — [move] 의 옛 경로 = 새 경로(Source 기준)")
    parser.add_argument("--dry-run", action="store_true", help="쓰지 않고 할 일과 손댈 파일만 찍는다")
    parser.add_argument("--root", type=Path, default=None, help="저장소 뿌리(기본: 이 저장소)")
    args = parser.parse_args(argv)
    root = (args.root or getProjectRoot()).resolve()
    try:
        context, mapOriginal, mapChanged, listSkipped = planMovesInternal(root, loadMoveMapInternal(args.map))
        listTouched = sorted({mapPathInternal(rel, context.mapMove) for rel in mapChanged})
        if not args.dry_run:
            writeAndMoveInternal(context, mapChanged)
            listTouched = sorted(set(listTouched) | set(fixIncludeOrderInternal(context, listTouched)))
            listLeft = findLeftoversInternal({rel: readTextInternal(root / rel) for rel in collectTextFilesInternal(root, listRepositoryFilesInternal(root))},
                                            context.mapMove)
            if listLeft:
                raise MoveError("옮긴 뒤 디스크에 옛 경로가 남습니다:\n  " + "\n  ".join(listLeft))
    except MoveError as error:
        print(f"[MoveSources] {error}", file=sys.stderr)
        return int(error.exitCode)
    printReportInternal(context, listTouched, listSkipped, args.dry_run)
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
