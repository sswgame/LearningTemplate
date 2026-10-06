#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/report/RunBuildScriptInventory.py

빌드 스크립트(CMake · 파이썬)의 재고 — **무엇이 있고, 누가 부르고, 무엇이 공통부를 비켜 가나.**

C++ 은 컴파일러가 안 쓰는 함수를 경고하고 `RunDuplicateCode` 가 복사를 잡는다. 빌드 스크립트는 둘 다 없다: CMake 함수는 아무도 안 불러도
조용하고, 파이썬 진입점이 `common` 의 도우미 대신 자기 것을 만들어도 조용하다. 글자 그대로의 복사가 아니라 **모양이 같은 것**(이름만 다른 팩토리,
진입점마다의 `build/<preset>` 조립)이라 복사 탐지기로는 안 보인다. 그래서 그 모양을 센다.

CMake
  - 함수 · 매크로마다 정의한 파일과 부르는 곳 수(정의한 파일 안의 호출 포함). **0 이면 죽은 함수.**
  - 파일마다 줄 수 — 400 줄을 넘는 파일(한 파일이 여러 일을 하는 신호).
  - 손 목록 — 소스 경로를 글자로 적은 줄 수(`"${CMAKE_CURRENT_SOURCE_DIR}/….cpp"`).
  - `add_library( … SHARED|MODULE )` 를 모듈 팩토리 밖에서 직접 부르는 곳.
파이썬
  - 진입점마다 `main(argv)` 인가 · 모듈 수준에서 `common` 을 import 하나 · `Test/PythonTest` 에 시험이 있나.
  - `common` 밖에서 공통부를 비켜 가는 호출 수 — `subprocess.*` · `"compile_commands.json"` · `/ "build" /` · `.reconfigure(` · `read_text(...) != ` 쓰기 비교.

종료 코드는 늘 0 이다(보고서). 숫자를 기준으로 두려면 `--json` 으로 떠서 견준다.

  py -3 Scripts/lint/report/RunBuildScriptInventory.py [--root <repo>] [--json <out.json>] [--zero-only]
"""

from __future__ import annotations

import argparse
import ast
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintReport

from common import kNotOurDirNames  # noqa: E402
from LintReport import LintReport, ReportContext  # noqa: E402

_kTag = "BuildScriptInventory"
_kCmakeDefinitionRe = re.compile(r"^\s*(function|macro)\s*\(\s*([A-Za-z_0-9]+)", re.MULTILINE | re.IGNORECASE)
_kCmakeHandListRe = re.compile(r'"\$\{CMAKE_CURRENT_SOURCE_DIR\}/[^"]+\.(cpp|c)"')
_kCmakeDirectLibraryRe = re.compile(r"^\s*add_library\(\s*\S+\s+(SHARED|MODULE)\b", re.MULTILINE)
#: 모듈 라이브러리를 직접 만들어도 되는 파일(팩토리 자신) — 저장소 기준.
_kDirectLibraryAllowed = ("cmake/Engine/ModuleTargets.cmake",)
_kLargeCmakeFileLines = 400
#: `common` 밖에서 보이면 공통부를 비켜 간 것.
_kBypassPattern: dict[str, re.Pattern[str]] = {
    "subprocess": re.compile(r"\bsubprocess\.(run|Popen|check_output|check_call|call)\("),
    "compile_commands": re.compile(r"[\"']compile_commands\.json[\"']"),
    "build/<preset>": re.compile(r"/\s*[\"']build[\"']\s*/|os\.path\.join\([^)]*[\"']build[\"']"),
    "reconfigure": re.compile(r"\.reconfigure\("),
    "write-if-changed": re.compile(r"previous\s*!=\s*content|read_text\([^)]*\)\s*==\s*content"),
}


def listTrackedFilesInternal(repositoryRoot: Path, suffixes: tuple[str, ...], fileNames: tuple[str, ...] = ()) -> list[Path]:
    listPath: list[Path] = []
    for path in repositoryRoot.rglob("*"):
        if not path.is_file() or not (path.suffix in suffixes or path.name in fileNames):
            continue
        listPart = path.relative_to(repositoryRoot).parts
        if any(part in kNotOurDirNames or part == "vcpkg-port" for part in listPart[:-1]):
            continue
        listPath.append(path)
    return sorted(listPath)


def blankCmakeStringsAndCommentsInternal(text: str) -> str:
    """따옴표 문자열과 `#` 주석을 빈칸으로 — 메시지 · 주석에 적힌 함수 이름을 호출로 세지 않게(줄 수는 그대로)."""
    listChar: list[str] = []
    bInString = False
    bInComment = False
    index = 0
    while index < len(text):
        character = text[index]
        if bInComment:
            if character == "\n":
                bInComment = False
                listChar.append(character)
            else:
                listChar.append(" ")
        elif bInString:
            if character == "\\" and index + 1 < len(text):
                listChar.append("  ")
                index += 2
                continue
            if character == '"':
                bInString = False
            listChar.append("\n" if character == "\n" else " ")
        elif character == '"':
            bInString = True
            listChar.append(" ")
        elif character == "#":
            bInComment = True
            listChar.append(" ")
        else:
            listChar.append(character)
        index += 1
    return "".join(listChar)


def inventoryCmake(repositoryRoot: Path) -> dict[str, Any]:
    """CMake 함수 · 호출 수 · 큰 파일 · 손 목록 · 직접 만든 모듈 라이브러리."""
    listFile = listTrackedFilesInternal(repositoryRoot, (".cmake",), ("CMakeLists.txt",))
    mapText = {path.relative_to(repositoryRoot).as_posix(): path.read_text(encoding="utf-8", errors="replace") for path in listFile}
    mapCode = {relPath: blankCmakeStringsAndCommentsInternal(text) for relPath, text in mapText.items()}

    mapDefinition: dict[str, str] = {}
    for relPath, text in mapCode.items():
        for match in _kCmakeDefinitionRe.finditer(text):
            mapDefinition[match.group(2)] = relPath

    mapCallCount: Counter[str] = Counter()
    for text in mapCode.values():
        for name in mapDefinition:
            mapCallCount[name] += len(re.findall(rf"(?<![A-Za-z_0-9]){re.escape(name)}\s*\(", text, re.IGNORECASE))

    return {
        "functions": {name: {"file": relPath, "calls": mapCallCount[name]} for name, relPath in sorted(mapDefinition.items())},
        "largeFiles": {relPath: text.count("\n") for relPath, text in mapText.items() if text.count("\n") > _kLargeCmakeFileLines},
        "handListLines": {relPath: count for relPath, text in mapText.items() if (count := len(_kCmakeHandListRe.findall(text)))},
        "directModuleLibraries": sorted(relPath for relPath, text in mapText.items()
                                        if _kCmakeDirectLibraryRe.search(text) and relPath not in _kDirectLibraryAllowed),
    }


def isModuleLevelCommonImportInternal(tree: ast.Module) -> bool:
    for node in tree.body:
        if isinstance(node, ast.ImportFrom) and node.module and node.module.split(".")[0] == "common":
            return True
        if isinstance(node, ast.Import) and any(alias.name.split(".")[0] == "common" for alias in node.names):
            return True
    return False


def mainSignatureInternal(tree: ast.Module) -> str:
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == "main":
            return "main(argv)" if node.args.args else "main()"
        if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == "main" for target in node.targets):
            return "main = X.run"
    return "-"


def inventoryPython(repositoryRoot: Path) -> dict[str, Any]:
    """진입점 · common 사용 · 시험 유무 · 공통부를 비켜 간 호출."""
    scriptsRoot = repositoryRoot / "Scripts"
    testText = "\n".join(path.read_text(encoding="utf-8", errors="replace")
                         for path in sorted((repositoryRoot / "Test" / "PythonTest").glob("Test*.py")))
    mapEntry: dict[str, Any] = {}
    mapBypass: dict[str, dict[str, int]] = {}
    for path in listTrackedFilesInternal(scriptsRoot, (".py",)):
        relPath = path.relative_to(repositoryRoot).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        if "/common/" not in relPath:
            mapHit = {name: len(pattern.findall(text)) for name, pattern in _kBypassPattern.items() if pattern.search(text)}
            if mapHit:
                mapBypass[relPath] = mapHit
        if "__main__" not in text:
            continue
        try:
            tree = ast.parse(text)
        except SyntaxError:
            continue
        mapEntry[relPath] = {
            "main": mainSignatureInternal(tree),
            "common": isModuleLevelCommonImportInternal(tree),
            "tested": path.stem in testText,
        }
    return {"entryPoints": mapEntry, "bypass": mapBypass}


def printReportInternal(cmake: dict[str, Any], python: dict[str, Any], bZeroOnly: bool) -> None:
    listZero = [f"{name} ({info['file']})" for name, info in cmake["functions"].items() if info["calls"] == 0]
    print(f"[{_kTag}] CMake 함수 {len(cmake['functions'])} · 부르는 곳 0: {len(listZero)}")
    for line in listZero:
        print(f"  - {line}")
    if bZeroOnly:
        return
    print(f"[{_kTag}] {_kLargeCmakeFileLines} 줄 넘는 CMake 파일:")
    for relPath, lineCount in sorted(cmake["largeFiles"].items(), key=lambda item: -item[1]):
        print(f"  - {relPath}: {lineCount}")
    print(f"[{_kTag}] 손 목록(소스 경로를 글자로 적은 줄):")
    for relPath, count in sorted(cmake["handListLines"].items(), key=lambda item: -item[1]):
        print(f"  - {relPath}: {count}")
    print(f"[{_kTag}] 모듈 팩토리 밖의 SHARED/MODULE add_library: {len(cmake['directModuleLibraries'])}")
    for relPath in cmake["directModuleLibraries"]:
        print(f"  - {relPath}")

    mapEntry = python["entryPoints"]
    countBy = Counter(info["main"] for info in mapEntry.values())
    print(f"[{_kTag}] 파이썬 진입점 {len(mapEntry)} — " + " · ".join(f"{name} {count}" for name, count in sorted(countBy.items())))
    print(f"  common 을 모듈 수준에서 import 하지 않는 진입점: {sum(1 for info in mapEntry.values() if not info['common'])}")
    print(f"  PythonTest 에 이름이 나오지 않는 진입점: {sum(1 for info in mapEntry.values() if not info['tested'])}")
    totalBypass: Counter[str] = Counter()
    for mapHit in python["bypass"].values():
        totalBypass.update(mapHit)
    print(f"[{_kTag}] common 밖에서 공통부를 비켜 간 호출: " + " · ".join(f"{name} {count}" for name, count in sorted(totalBypass.items())))
    for relPath, mapHit in sorted(python["bypass"].items()):
        print(f"  - {relPath}: " + ", ".join(f"{name}×{count}" for name, count in sorted(mapHit.items())))


class RunBuildScriptInventoryReport(LintReport):
    description = "빌드 스크립트(CMake · 파이썬) 재고 — 죽은 함수 · 공통부를 비켜 간 곳"

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--json", type=Path, default=None, help="결과를 JSON 으로도 쓴다")
        parser.add_argument("--zero-only", action="store_true", help="부르는 곳이 0 인 CMake 함수만")

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        repositoryRoot = context.repositoryRoot
        cmake = inventoryCmake(repositoryRoot)
        python = inventoryPython(repositoryRoot)
        printReportInternal(cmake, python, args.zero_only)
        if args.json is not None:
            args.json.parent.mkdir(parents=True, exist_ok=True)
            args.json.write_text(json.dumps({"cmake": cmake, "python": python}, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        return 0


main = RunBuildScriptInventoryReport.run


if __name__ == "__main__":
    sys.exit(main())
