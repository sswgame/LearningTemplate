"""
소스 파일 · 폴더를 옮기거나 C++ 이름을 바꾸고, 저장소의 참조를 한 번에 고친다 — 구조 개편 단계마다 같은 방법으로 옮기려고 둔다.

  py -3 -m Scripts move-source Engine/Object/SceneAnimation=Engine/Animation/Scene Engine/A/B.h=Engine/C/B.h [--dry-run]
  py -3 -m Scripts rename-symbol getOrCreateEntry=getOrCreateEntry [--dry-run]

move-source: 경로는 `Source/` 기준(폴더면 안의 파일 전부, 하위 폴더 포함). 하는 일:
  1. `git mv`
  2. 추적 중인 텍스트 파일(Source · Tools · cmake · Scripts · docs · 최상위 CMake)에서 옮긴 파일의 전체 경로(`Engine/X/Y.h`)와
     폴더 접두어(`Engine/X/` — CMake 정규식 · 주석)를 새 경로로
  3. 같은 폴더 짧은 include(`#include "Y.h"`)가 옮긴 뒤 다른 폴더를 가리키게 되면 전체 경로로
  4. 고친 C++ 파일의 include 순서를 `format-includes` 규칙으로
rename-symbol: 낱말 경계로 바꾼다(부분 일치 없음). 새 이름이 이미 쓰이고 있으면 겹친 파일을 알리고 멈춘다(`--force` 로 강행).
  C 계열 소스(C++ · HLSL)의 문자열 리터럴 안은 기본으로 건너뛰고 그 파일을 알린다(`--include-strings` 로 함께 바꾼다).
"""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

from common.ExitCode import ExitCode
from common.Git import runGit
from common.Paths import getProjectRoot

kTextSuffix = {".h", ".hpp", ".inl", ".cpp", ".c", ".cmake", ".txt", ".md", ".json", ".in", ".py", ".toml", ".xxx", ".natvis", ".hlsl", ".hlsli", ".xml", ".po", ".tpl"}
kCppSuffix = {".h", ".hpp", ".inl", ".cpp"}
kScanRoot = ["Source", "Content", "Source/Programs/ReflectionParser", "Source/Programs/OnlineLoadBot", "cmake", "Scripts", "docs", "Config", "CMakeLists.txt", "CMakePresets.json", "AGENTS.md", "CLAUDE.md"]
kBareInclude = re.compile(r'^(\s*#\s*include\s*")([^"/]+)(")', re.M)


# 규칙 문서는 "쓰지 말 것" 예로 옛 이름을 일부러 든다 — 이름 바꾸기가 그 예까지 바꾸면 규칙이 거꾸로 된다.
kNeverRewrite = ("docs/agents/", "docs/04_CodingGuidelines.md")

# C 계열 소스의 주석 · 문자열 · 문자 리터럴. rename-symbol 은 문자열 안을 기본으로 건너뛴다 — 문자열은 파일 매직("OggS") · JSON 키 · 내보내기
# 기호 · 데이터 이름처럼 바깥이 정한 철자가 많아, 약어 일괄 변경(1dafbae0c9)이 그 안까지 바꿔 .ogg 디코드 · 구매 응답 · 머티리얼 깃발을 깨뜨렸다.
kCLikeSuffix = kCppSuffix | {".c", ".hlsl", ".hlsli", ".xxx"}
kCLiteralOrComment = re.compile(
    r'(?P<comment>//[^\n]*|/\*.*?\*/)'
    r'|(?:u8|[uUL])?R"(?P<delim>[^()\\\s]{0,16})\(.*?\)(?P=delim)"'
    r'|(?:u8|[uUL])?"(?:\\.|[^"\\\n])*"'
    r"|'(?:\\.|[^'\\\n])+'",
    re.S,
)


def rewriteOutsideStrings(text: str, rewrite) -> tuple[str, int]:
    """C 계열 글에서 문자열 · 문자 리터럴 밖(코드 · 주석)에만 @p rewrite 를 적용한다. 건너뛴 리터럴 가운데 바뀌었을 것의 수를 함께 돌려준다."""
    listPart: list[str] = []
    skippedCount = 0
    position = 0
    for match in kCLiteralOrComment.finditer(text):
        listPart.append(rewrite(text[position:match.start()]))
        literal = match.group(0)
        if match.group("comment") is not None:
            listPart.append(rewrite(literal))
        else:
            skippedCount += rewrite(literal) != literal
            listPart.append(literal)
        position = match.end()
    listPart.append(rewrite(text[position:]))
    return "".join(listPart), skippedCount


def checkRewriteOutsideStrings() -> None:
    """문자열 건너뛰기의 자체 확인 — 코드 · 주석은 바뀌고 일반 · 원시 문자열과 주석 속 따옴표는 지킨다."""
    rename = lambda part: re.sub(r"\bOgg\b", "OGG", part)  # noqa: E731
    text = 'Ogg a; // Ogg "x\nf( "Ogg", R"(Ogg)", \'"\' ); /* Ogg */ Ogg b;'
    rewritten, skippedCount = rewriteOutsideStrings(text, rename)
    assert rewritten == 'OGG a; // OGG "x\nf( "Ogg", R"(Ogg)", \'"\' ); /* OGG */ OGG b;', rewritten
    assert skippedCount == 2, skippedCount


def listTrackedText(root: Path) -> list[Path]:
    result = runGit(["ls-files", "--", *kScanRoot], cwd=root)
    if not result.bSucceeded:
        raise RuntimeError(f"git ls-files 실패: {result.stderr.strip()}")
    return [root / line for line in result.stdout.splitlines()
            if Path(line).suffix.lower() in kTextSuffix and not line.startswith(kNeverRewrite) and (root / line).is_file()]


def readText(path: Path) -> str:
    return path.read_bytes().decode("utf-8", "surrogateescape")


def writeText(path: Path, text: str) -> None:
    path.write_bytes(text.encode("utf-8", "surrogateescape"))


def expandMoves(source: Path, listPair: list[str]) -> dict[str, str]:
    """`old=new` 쌍을 파일 단위 사상(Source 기준 posix 경로)으로 편다. 없는 원본 · 이미 있는 대상은 오류다."""
    mapFile: dict[str, str] = {}
    for pair in listPair:
        old, sep, new = pair.partition("=")
        if not sep or not old or not new:
            raise ValueError(f"'{pair}' 은 old=new 모양이 아니다")
        old, new = old.strip("/"), new.strip("/")
        oldPath = source / old
        if oldPath.is_file():
            mapFile[old] = new
        elif oldPath.is_dir():
            for file in sorted(p for p in oldPath.rglob("*") if p.is_file()):
                relative = file.relative_to(oldPath).as_posix()
                mapFile[f"{old}/{relative}"] = f"{new}/{relative}"
        else:
            raise FileNotFoundError(f"Source/{old} 이 없다")
    for new in mapFile.values():
        if (source / new).exists():
            raise FileExistsError(f"Source/{new} 이 이미 있다")
    return mapFile


def folderPrefixes(listPair: list[str], source: Path) -> list[tuple[str, str]]:
    """폴더 이동의 접두어 쌍(긴 것 먼저 — 안쪽 폴더가 바깥 폴더 규칙에 먼저 먹히지 않게)."""
    listPrefix = []
    for pair in listPair:
        old, _, new = pair.partition("=")
        if (source / old.strip("/")).is_dir():
            listPrefix.append((old.strip("/") + "/", new.strip("/") + "/"))
    return sorted(listPrefix, key=lambda item: -len(item[0]))


def rewriteBareIncludes(text: str, oldFolder: str, newFolder: str, source: Path, mapFile: dict[str, str]) -> str:
    """파일이 `oldFolder` 에서 `newFolder` 로 갔을 때, 짧은 include 가 다른 폴더를 가리키게 되면 전체 경로로 바꾼다."""

    def replace(match: re.Match[str]) -> str:
        name = match.group(2)
        oldTarget = f"{oldFolder}/{name}" if oldFolder else name
        if not (source / oldTarget).is_file():
            return match.group(0)  # pch.h · 생성 헤더처럼 이 폴더 밖에서 풀리는 이름
        newTarget = mapFile.get(oldTarget, oldTarget)
        if newTarget.rpartition("/")[0] == newFolder:
            return match.group(0)
        return f"{match.group(1)}{newTarget}{match.group(3)}"

    return kBareInclude.sub(replace, text)


def mainMove(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts move-source", description="소스를 옮기고 참조를 고친다")
    parser.add_argument("pair", nargs="+", help="Source 기준 old=new (파일 또는 폴더)")
    parser.add_argument("--dry-run", action="store_true", help="옮길 파일 목록만 찍는다")
    args = parser.parse_args(argv)

    root = getProjectRoot()
    source = root / "Source"
    mapFile = expandMoves(source, args.pair)
    if args.dry_run:
        for old, new in mapFile.items():
            print(f"{old} -> {new}")
        return ExitCode.Passed

    # 짧은 include 는 옮기기 전 자리에서 풀어야 하므로 내용 재작성을 먼저 계산한다.
    mapMovedText: dict[str, str] = {}
    for old, new in mapFile.items():
        path = source / old
        if path.suffix.lower() in kCppSuffix:
            mapMovedText[new] = rewriteBareIncludes(readText(path), old.rpartition("/")[0], new.rpartition("/")[0], source, mapFile)
    listNeighbourText: dict[Path, str] = {}
    movedSet = set(mapFile)
    for path in listTrackedText(root):
        relative = path.relative_to(source).as_posix() if path.is_relative_to(source) else None
        if relative is None or relative in movedSet or path.suffix.lower() not in kCppSuffix:
            continue
        folder = relative.rpartition("/")[0]
        text = readText(path)
        rewritten = rewriteBareIncludes(text, folder, folder, source, mapFile)
        if rewritten != text:
            listNeighbourText[path] = rewritten

    for old, new in mapFile.items():
        (source / new).parent.mkdir(parents=True, exist_ok=True)
        for _ in range(20):  # IDE 의 git 이 잠깐 잡는 index.lock 은 기다리면 풀린다 — 반쯤 옮긴 채 멈추지 않게
            result = runGit(["mv", f"Source/{old}", f"Source/{new}"], cwd=root)
            if result.bSucceeded or "index.lock" not in result.stderr:
                break
            time.sleep(0.5)
        if not result.bSucceeded:
            raise RuntimeError(f"git mv {old} 실패: {result.stderr.strip()}")
    for new, text in mapMovedText.items():
        writeText(source / new, text)
    for path, text in listNeighbourText.items():
        writeText(path, text)
    for old in mapFile:
        folder = (source / old).parent
        while folder != source and folder.is_dir() and not any(folder.iterdir()):
            folder.rmdir()
            folder = folder.parent

    # 전체 경로는 낱말 경계로 — `Engine/A/B.h` 가 `MyEngine/A/B.h` 안에서 바뀌지 않게(`Source/Engine/A/B.h` 처럼 앞에 `/` 가 오는 것은 바꾼다).
    listRule = [(re.compile(r"(?<![\w.-])" + re.escape(old) + r"(?![\w.])"), lambda _, new=new: new) for old, new in sorted(mapFile.items(), key=lambda item: -len(item[0]))]
    listRule += [(re.compile(r"(?<![\w.-])" + re.escape(old)), lambda _, new=new: new) for old, new in folderPrefixes(args.pair, source)]
    # `@file` 은 최상위 폴더 없이 적은 곳도 있다(`@file Utility/X.h`).
    listRule += [(re.compile(r"(@file\s+)" + re.escape(old.partition("/")[2]) + r"\b"), lambda match, new=new: match.group(1) + new) for old, new in mapFile.items() if "/" in old]
    changedCppSet = {source / new for new in mapMovedText} | set(listNeighbourText)
    for path in listTrackedText(root):
        text = readText(path)
        rewritten = text
        for pattern, replacement in listRule:
            rewritten = pattern.sub(replacement, rewritten)
        if rewritten != text:
            writeText(path, rewritten)
            if path.suffix.lower() in kCppSuffix:
                changedCppSet.add(path)
    listChangedCpp = sorted(changedCppSet)

    from lint.Format import FormatIncludeOrderFixer, runClangFormatBatch

    FormatIncludeOrderFixer().processFiles(listChangedCpp, checkOnly=False)
    runClangFormatBatch(listChangedCpp, cwd=root)  # 경로 길이가 바뀌면 정렬 칸이 어긋난다
    print(f"[move-source] {len(mapFile)} 파일을 옮기고 C++ {len(listChangedCpp)} 파일의 참조 · include 순서를 고쳤다")
    return ExitCode.Passed


def mainRename(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="py -3 -m Scripts rename-symbol", description="C++ 이름을 낱말 경계로 바꾼다")
    parser.add_argument("pair", nargs="+", help="Old=New")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--force", action="store_true", help="새 이름이 이미 쓰여도 바꾼다")
    parser.add_argument("--regex", action="store_true", help="Old 를 정규식(낱말 경계 없이)으로, New 를 치환식으로 — 이름 일부를 바꾼다. 충돌 검사는 하지 않으니 빌드로 확인한다")
    parser.add_argument("--include-strings", action="store_true",
                        help="C 계열 소스의 문자열 리터럴 안도 바꾼다(기본은 건너뛰고 그 자리를 알린다 — 매직 · JSON 키 · 내보내기 기호는 바깥 철자다)")
    args = parser.parse_args(argv)
    checkRewriteOutsideStrings()

    root = getProjectRoot()
    listPair = [pair.partition("=")[::2] for pair in args.pair]
    listSkipped: list[str] = []

    def rewriteFile(path: Path, text: str, rewrite) -> str:
        if args.include_strings or path.suffix.lower() not in kCLikeSuffix:
            return rewrite(text)
        rewritten, skippedCount = rewriteOutsideStrings(text, rewrite)
        if skippedCount:
            listSkipped.append(f"{path.relative_to(root).as_posix()}({skippedCount})")
        return rewritten

    def reportSkipped() -> None:
        if listSkipped:
            print(f"[rename-symbol] 문자열 안은 건너뛰었다 — 데이터 · 바깥 이름이면 그대로 두고, 우리 이름이면 손으로 고치거나 --include-strings: "
                  + ", ".join(listSkipped[:20]) + (" …" if len(listSkipped) > 20 else ""))

    if args.regex:
        listRule = [(re.compile(old), new) for old, new in listPair]

        def rewriteRegex(part: str) -> str:
            for pattern, replacement in listRule:
                part = pattern.sub(replacement, part)
            return part

        changedCount = 0
        listChanged: list[Path] = []
        for path in listTrackedText(root):
            text = readText(path)
            rewritten = rewriteFile(path, text, rewriteRegex)
            if rewritten != text:
                changedCount += 1
                listChanged.append(path)
                if not args.dry_run:
                    writeText(path, rewritten)
        print(f"[rename-symbol] {changedCount} 파일" + (" (dry-run)" if args.dry_run else ""))
        reportSkipped()
        return ExitCode.Passed if args.dry_run else formatChanged(root, listChanged)
    listFile = [path for path in listTrackedText(root) if path.suffix.lower() in kCppSuffix | {".xxx", ".natvis", ".txt", ".cmake"}]
    mapText = {path: readText(path) for path in listFile}
    bClash = False
    for old, new in listPair:
        if not old or not new:
            raise ValueError("Old=New 모양이어야 한다")
        listClash = [path for path, text in mapText.items() if re.search(rf"\b{re.escape(new)}\b", text)]
        if listClash and not args.force:
            bClash = True
            print(f"[rename-symbol] '{new}' 이 이미 쓰인다: " + ", ".join(str(path.relative_to(root)) for path in listClash[:5]), file=sys.stderr)
    if bClash:
        return ExitCode.Violation
    listWordRule = [(re.compile(rf"\b{re.escape(old)}\b"), new) for old, new in listPair]

    def rewriteWords(part: str) -> str:
        for pattern, new in listWordRule:
            part = pattern.sub(lambda _, new=new: new, part)
        return part

    changedCount = 0
    listChanged = []
    for path, text in mapText.items():
        rewritten = rewriteFile(path, text, rewriteWords)
        if rewritten != text:
            changedCount += 1
            listChanged.append(path)
            if not args.dry_run:
                writeText(path, rewritten)
    print(f"[rename-symbol] {changedCount} 파일" + (" (dry-run)" if args.dry_run else ""))
    reportSkipped()
    return ExitCode.Passed if args.dry_run else formatChanged(root, listChanged)


def formatChanged(root: Path, listPath: list[Path]) -> int:
    """이름 길이가 바뀌면 clang-format 의 정렬 칸이 어긋난다 — 바꾼 C++ 파일만 다시 포맷한다."""
    from lint.Format import runClangFormatBatch

    listCpp = [path for path in listPath if path.suffix.lower() in kCppSuffix]
    return runClangFormatBatch(listCpp, cwd=root) if listCpp else ExitCode.Passed
