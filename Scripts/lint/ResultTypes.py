#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/ResultTypes.py

결과 형식 래칫. 값 또는 실패 이유는 `sw::expected<T, E>` 한 벌(`Core/Foundation/Expected.h`, "없음이 정상" 은 `sw::optional<T>`)로 돌려준다 —
규칙은 `docs/agents/Errors.md`. 이 검사는 옛 모양 넷을 센다:

- `std-optional`: `std::optional` · `std::nullopt` 직접 사용(주석 · 문자열 · 전처리 줄은 세지 않는다).
- `out-error`: `string& outError` · `string& outReason`(`sw::string&` · `outErrorText` 처럼 이름이 이어져도) 출력 인자 모양.
- `out-error-pointer`: 같은 결함의 포인터 철자 `string* pOutError` · `pOutReason` · `pOutMessage`(널이면 이유를 버린다 — 2026-11-09, 참조형만 세던 사이 남은 자리).
- `out-text-on-failure`: 이름은 달라도(`outReply` · `outUnknown` · `outFailureText` …) 실패 갈래에서만 쓰는 글 출력 인자 — 함수 본문의 모든 쓰기 바로 뒤가
  `return false` · `nullptr` · `nullopt` · `unexpected` 이고 그 인자를 다른 함수에 넘기지 않는 정의만 센다(넘기면 성공 쪽 쓰기를 볼 수 없어 세지 않는다).

**왜 래칫인가.** 형식(`sw::expected`)은 새 함수가 옛 모양을 쓰지 못하게 막지 못한다 — `std::optional` 도 `string&` 도 언어가 준다. 그래서:
- `Source/Core/` 는 **0** 이어야 한다(옮김이 끝났다). 예외는 기준선 파일의 `_coreExemptions`(`경로` → `종류` → 까닭)에만 둔다.
- 그 밖은 파일마다 종류별 수를 기준선(`Config/Dev/ResultTypeBaseline.json`)에 두고, **늘면** 커밋이 멈추고 **줄면** `--ratchet` 으로 함께 낮춘다
  (`MagicLiterals.py` 와 같은 모양). 올리는 길은 `--accept <파일…>` 하나(통합에서 규칙 전 코드를 합칠 때).

    py -3 -m Scripts result-types                    # 폴더별 남은 수
    py -3 -m Scripts result-types --check [--all]    # 래칫 검사(커밋 훅은 staged 파일)
    py -3 -m Scripts result-types --ratchet          # 줄어든 파일의 기준선을 낮춘다(올리지 않는다)
    py -3 -m Scripts result-types --accept <파일…>   # 그 파일의 기준선을 지금 수로(올리는 유일한 길)
    py -3 -m Scripts result-types --self-check
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.ExitCode import ExitCode  # noqa: E402
from lint.CppShape import findParamListEnd, kBodyOpen, matchBrace, stripCode  # noqa: E402
from lint.Format import getModifiedCppFiles  # noqa: E402
from lint.MagicLiterals import isSourceFile, listSourceFiles, readStagedRenames  # noqa: E402


kBaselinePath = "Config/Dev/ResultTypeBaseline.json"
kKinds = ("std-optional", "out-error", "out-error-pointer", "out-text-on-failure")
kPattern = {
    "std-optional": re.compile(r"\bstd\s*::\s*(?:optional\b|nullopt\b|nullopt_t\b)"),
    "out-error": re.compile(r"\b(?:sw\s*::\s*)?string\s*&\s*out\w*(?:Error|Reason)\w*\b"),
    "out-error-pointer": re.compile(r"\b(?:sw\s*::\s*)?string\s*\*\s*pOut\w*(?:Error|Reason|Message)\w*\b"),
}
# out-text-on-failure: 이름이 이유를 말하지 않는 `string& outX` 정의(이유 이름은 위 out-error 가 센다)
kOutTextParam = re.compile(r"\b(?:sw\s*::\s*)?string\s*&\s*(out\w+)\s*(?=[,)])")
kReasonName = re.compile(r"Error|Reason|Message")
kFailureReturn = re.compile(r"\s*return\s+(?:false\b|nullptr\b|(?:std\s*::\s*|sw\s*::\s*)?nullopt\b|(?:sw\s*::\s*)?unexpected\b)")
# 0 이어야 하는 폴더(옮김이 끝난 곳). 그 안의 예외는 기준선 파일의 `_coreExemptions` 데이터다.
kStrictPrefix = "Source/Core/"
kAdvice = {
    "std-optional": "`sw::optional<T>`(없음이 정상) 이나 `sw::expected<T, E>`(이유가 둘 이상)로 — docs/agents/Errors.md",
    "out-error": "`bool f( …, string& outError )` 대신 `sw::expected<T>` · `sw::expected<void>` 를 돌려준다 — docs/agents/Errors.md",
    "out-error-pointer": "`bool f( …, string* pOutError = nullptr )` 도 같은 결함이다 — `sw::expected<T>` 를 돌려주고 부르는 쪽이 버릴지 정한다(docs/agents/Errors.md)",
    "out-text-on-failure": "실패 때만 쓰는 글 출력 인자는 이유다 — `sw::expected<T>`(또는 `expected<T, DomainError>`)의 오류로 돌려준다(docs/agents/Errors.md)",
}


def isFailureOnlyWrite(body: str, write: re.Match[str]) -> bool:
    """쓰기 문장 바로 다음 문장이 실패 반환인지 봅니다."""
    semicolon = body.find(";", write.end())
    return semicolon >= 0 and kFailureReturn.match(body, semicolon + 1) is not None


def countFailureOnlyText(code: str) -> int:
    """`string& outX` 인자를 실패 갈래에서만 쓰는 함수 정의 수입니다(선언 · 쓰지 않는 정의 · 다른 함수에 넘기는 정의는 세지 않는다)."""
    count = 0
    for match in kOutTextParam.finditer(code):
        name = match.group(1)
        if kReasonName.search(name):
            continue
        opening = kBodyOpen.match(code, findParamListEnd(code, match.end()) + 1)
        if opening is None:
            continue
        body = code[opening.end() - 1:matchBrace(code, opening.end() - 1)]
        listWrite = list(re.finditer(r"(?<![\w.>])" + name + r"\s*(?:=(?!=)|\+=|\.\s*(?:append|assign|push_back)\s*\()", body))
        if not listWrite or re.search(r"[(,]\s*" + name + r"\s*[,)]", body):
            continue
        if all(isFailureOnlyWrite(body, write) for write in listWrite):
            count += 1
    return count


def countText(text: str) -> dict[str, int]:
    """주석 · 문자열을 지운 코드에서 종류별 수입니다(0 인 종류는 적지 않는다)."""
    code = stripCode(text)
    counts = {kind: len(pattern.findall(code)) for kind, pattern in kPattern.items()}
    counts["out-text-on-failure"] = countFailureOnlyText(code)
    return {kind: count for kind, count in counts.items() if count > 0}


def countFiles(projectRoot: Path, listFile: Iterable[Path]) -> dict[str, dict[str, int]]:
    result: dict[str, dict[str, int]] = {}
    for file in listFile:
        counts = countText(file.read_text(encoding="utf-8", errors="ignore"))
        if counts:
            result[file.relative_to(projectRoot).as_posix()] = counts
    return result


def readBaseline(projectRoot: Path) -> dict[str, dict[str, int]]:
    """기준선입니다. 파일이 없으면 오류로 멈춘다(빈 기준선으로 돌면 검사 없이 지나갈 수 있다)."""
    return json.loads((projectRoot / kBaselinePath).read_text(encoding="utf-8"))["_files"]


def readCoreExemptions(projectRoot: Path) -> dict[str, dict[str, str]]:
    """Core 0 규칙의 예외 `경로` → `종류` → 까닭입니다. 칸이 없으면 오류로 멈춘다."""
    return json.loads((projectRoot / kBaselinePath).read_text(encoding="utf-8"))["_coreExemptions"]


def writeBaseline(projectRoot: Path, baseline: dict[str, dict[str, int]]) -> None:
    comment = ("결과 형식 래칫 기준선(Scripts/lint/ResultTypes.py). Core 밖 파일마다 std::optional · string& outError · string* pOutError · 실패 때만 쓰는 글 출력 인자의 수 — "
               "늘면 커밋이 멈추고, 줄면 `py -3 -m Scripts result-types --ratchet` 로 낮춘다. 손으로 올리지 않는다(올리는 길은 --accept 하나, docs/agents/Errors.md). "
               "_coreExemptions 는 Core 0 규칙의 예외(경로 → 종류 → 까닭)다.")
    exemptions = readCoreExemptions(projectRoot)
    listExemption = [f"    {json.dumps(path)}: {json.dumps(exemptions[path], ensure_ascii=False)}" for path in sorted(exemptions)]
    listLine = [f"    {json.dumps(path)}: {json.dumps(baseline[path])}" for path in sorted(baseline) if baseline[path]]
    text = (f"{{\n  \"_comment\": {json.dumps(comment, ensure_ascii=False)},\n"
            "  \"_coreExemptions\": {\n" + ",\n".join(listExemption) + "\n  },\n"
            "  \"_files\": {\n" + ",\n".join(listLine) + "\n  }\n}\n")
    (projectRoot / kBaselinePath).write_text(text, encoding="utf-8", newline="\n")


def findProblems(actual: dict[str, dict[str, int]], baseline: dict[str, dict[str, int]], listPath: Iterable[str], mapOld: dict[str, str],
                 coreExemptions: dict[str, dict[str, str]]) -> list[str]:
    """`listPath` 파일마다: Core 는 0(예외 종류 말고), 그 밖은 기준선과 같아야 한다(늘면 옮기기, 줄면 기준선 낮추기)."""
    listProblem: list[str] = []
    for path in sorted(set(listPath)):
        have = actual.get(path, {})
        if path.startswith(kStrictPrefix):
            exempt = coreExemptions.get(path, {})
            listProblem += [f"{path}: {kind} {count} — Core 는 0 이어야 한다: {kAdvice[kind]}" for kind, count in have.items() if kind not in exempt]
            continue
        allowed = baseline.get(path, baseline.get(mapOld.get(path, ""), {}))
        for kind in kKinds:
            count, limit = have.get(kind, 0), allowed.get(kind, 0)
            if count > limit:
                listProblem.append(f"{path}: {kind} {limit} -> {count} — 늘었다: {kAdvice[kind]}")
            elif count < limit:
                listProblem.append(f"{path}: {kind} {limit} -> {count} — 줄었다: `py -3 -m Scripts result-types --ratchet` 로 기준선을 낮추고 함께 커밋한다")
    return listProblem


def checkResultTypes(projectRoot: Path, listFile: Sequence[Path]) -> list[str]:
    """커밋 훅: staged C++ 파일의 래칫 검사입니다."""
    listSource = [path for path in listFile if isSourceFile(path.relative_to(projectRoot).as_posix())]
    if not listSource:
        return []
    actual = countFiles(projectRoot, listSource)
    return findProblems(actual, readBaseline(projectRoot), [path.relative_to(projectRoot).as_posix() for path in listSource], readStagedRenames(projectRoot),
                        readCoreExemptions(projectRoot))


def lowerBaseline(baseline: dict[str, dict[str, int]], actual: dict[str, dict[str, int]], listExisting: set[str]) -> dict[str, dict[str, int]]:
    """줄어든 수만 낮춘다(늘어난 수는 기준선을 지킨다). 없어진 파일은 지운다."""
    lowered: dict[str, dict[str, int]] = {}
    for path, allowed in baseline.items():
        if path not in listExisting:
            continue
        have = actual.get(path, {})
        lowered[path] = {kind: min(allowed[kind], have.get(kind, 0)) for kind in allowed if min(allowed[kind], have.get(kind, 0)) > 0}
    return lowered


def printSummary(actual: dict[str, dict[str, int]]) -> None:
    countByFolder: collections.Counter[tuple[str, str]] = collections.Counter()
    for path, counts in actual.items():
        folder = "/".join(path.split("/")[:2])
        for kind, count in counts.items():
            countByFolder[(folder, kind)] += count
    listFolder = sorted({folder for folder, _ in countByFolder})
    print(f"{'folder':<28}" + "".join(f"{kind:>14}" for kind in kKinds))
    for folder in listFolder:
        print(f"{folder:<28}" + "".join(f"{countByFolder[(folder, kind)]:>14}" for kind in kKinds))
    print(f"{'total':<28}" + "".join(f"{sum(count for (_, k), count in countByFolder.items() if k == kind):>14}" for kind in kKinds))


def runSelfCheck() -> int:
    failures = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal failures
        if not condition:
            print(f"[ResultTypes] self-check: {message}")
            failures += 1

    expect(countText("std::optional<int32> find();") == {"std-optional": 1}, "std::optional is counted")
    expect(countText("return std::nullopt;") == {"std-optional": 1}, "std::nullopt is counted")
    expect(countText("sw::optional<int32> find(); // std::optional was here\nconst char* p = \"std::optional\";") == {}, "comments, strings and sw::optional are not counted")
    expect(countText("#include <optional>\n") == {}, "the include line is not counted")
    expect(countText("bool parse( string_view text, string& outError );") == {"out-error": 1}, "string& outError is counted")
    expect(countText("bool gate( sw::string_view path, sw::string& outReason );") == {"out-error": 1}, "sw::string& outReason is counted")
    expect(countText("bool take( StreamCloseReason& outReason );") == {}, "an enum out parameter is not counted")
    expect(countText("void f( string& outText );") == {}, "other string outputs are not counted")
    expect(countText("bool load( string_view text, string* pOutError = nullptr );") == {"out-error-pointer": 1}, "string* pOutError is counted")
    expect(countText("bool decode( sw::string* pOutReason ); void g( string* pOutMessage );") == {"out-error-pointer": 2}, "pOutReason · pOutMessage are counted")
    expect(countText("bool resolve( string* pOutAbsPath );") == {}, "a pointer to a value output is not counted")
    failureOnly = "bool find( int32 id, string& outReply )\n{\n    if ( id < 0 )\n    {\n        outReply = \"bad id\";\n        return false;\n    }\n    return true;\n}\n"
    expect(countText(failureOnly) == {"out-text-on-failure": 1}, "a text output written only before `return false` is counted")
    expect(countText("Item* pick( string& outUnknown ) { if ( a ) { outUnknown = name; return nullptr; } return p; }") == {"out-text-on-failure": 1}, "`return nullptr` is a failure return")
    bothSides = "bool run( string& outReply )\n{\n    if ( a )\n    {\n        outReply = \"no\";\n        return false;\n    }\n    outReply = \"done\";\n    return true;\n}\n"
    expect(countText(bothSides) == {}, "a text output also written on success is not counted")
    expect(countText("bool run( string& outReply ) { if ( a ) { outReply = \"no\"; return false; } return inner( outReply ); }") == {}, "a text output passed on is not counted")
    expect(countText("bool run( string& outReply );") == {}, "a declaration is not counted")
    baseline = {"Source/Engine/A.cpp": {"std-optional": 2}, "Source/Engine/Old.cpp": {"out-error": 1}}
    coreExemptions = {"Source/Core/Foundation/Expected.h": {"std-optional": "bridge"}}
    expect(findProblems({"Source/Engine/A.cpp": {"std-optional": 2}}, baseline, ["Source/Engine/A.cpp"], {}, coreExemptions) == [], "equal counts pass")
    expect(len(findProblems({"Source/Engine/A.cpp": {"std-optional": 3}}, baseline, ["Source/Engine/A.cpp"], {}, coreExemptions)) == 1, "a grown count fails")
    expect(len(findProblems({"Source/Engine/A.cpp": {"std-optional": 1}}, baseline, ["Source/Engine/A.cpp"], {}, coreExemptions)) == 1, "a shrunk count asks to lower the baseline")
    expect(len(findProblems({"Source/Engine/New.cpp": {"out-error": 1}}, baseline, ["Source/Engine/New.cpp"], {}, coreExemptions)) == 1, "a new file starts at zero")
    expect(len(findProblems({"Source/Engine/A.cpp": {"std-optional": 2, "out-error-pointer": 1}}, baseline, ["Source/Engine/A.cpp"], {}, coreExemptions)) == 1, "a new pointer out-error fails")
    expect(findProblems({"Source/Engine/Moved.cpp": {"out-error": 1}}, baseline, ["Source/Engine/Moved.cpp"], {"Source/Engine/Moved.cpp": "Source/Engine/Old.cpp"}, coreExemptions) == [], "a moved file keeps its baseline")
    expect(len(findProblems({"Source/Core/X.h": {"std-optional": 1}}, {"Source/Core/X.h": {"std-optional": 1}}, ["Source/Core/X.h"], {}, coreExemptions)) == 1, "Core must be zero even with a baseline")
    expect(findProblems({"Source/Core/Foundation/Expected.h": {"std-optional": 4}}, {}, ["Source/Core/Foundation/Expected.h"], {}, coreExemptions) == [], "an exempt kind in Core passes")
    expect(len(findProblems({"Source/Core/Foundation/Expected.h": {"out-error": 1}}, {}, ["Source/Core/Foundation/Expected.h"], {}, coreExemptions)) == 1, "a Core exemption covers only its kind")
    lowered = lowerBaseline({"Source/Engine/A.cpp": {"std-optional": 2, "out-error": 1}, "Source/Engine/Gone.cpp": {"out-error": 1}}, {"Source/Engine/A.cpp": {"std-optional": 3}}, {"Source/Engine/A.cpp"})
    expect(lowered == {"Source/Engine/A.cpp": {"std-optional": 2}}, f"--ratchet lowers only and drops missing files: {lowered}")
    print(f"[ResultTypes] self-check {'OK' if failures == 0 else 'FAIL'}")
    return ExitCode.Passed if failures == 0 else ExitCode.Violation


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="결과 형식 래칫: std::optional · string& outError · string* pOutError · 실패 때만 쓰는 글 출력 인자 모양이 Core 에 없고 그 밖에서 늘지 않게")
    parser.add_argument("--check", action="store_true", help="수정된 C++ 파일(작업 트리 대 HEAD)을 검사한다(--all 은 모든 파일)")
    parser.add_argument("--all", action="store_true", help="--check 와 함께: 모든 Source 파일")
    parser.add_argument("--ratchet", action="store_true", help="줄어든 파일의 기준선을 낮추고 없어진 파일을 지운다(올리지 않는다)")
    parser.add_argument("--accept", nargs="+", default=None, metavar="FILE", help="그 파일들의 기준선을 지금 수로 바꾼다(올리는 유일한 길 — 통합에서만)")
    parser.add_argument("--self-check", action="store_true", help="세기 · 래칫 규칙의 자체 시험")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if args.self_check:
        return runSelfCheck()

    projectRoot = Path(__file__).resolve().parents[2]
    if args.check:
        listFile = listSourceFiles(projectRoot) if args.all else getModifiedCppFiles(projectRoot)
        listRelative = [path.relative_to(projectRoot).as_posix() for path in listFile]
        listSource = [path for path, relative in zip(listFile, listRelative) if isSourceFile(relative)]
        baseline = readBaseline(projectRoot)
        listProblem = findProblems(countFiles(projectRoot, listSource), baseline, [relative for relative in listRelative if isSourceFile(relative)], {}, readCoreExemptions(projectRoot))
        if args.all:
            setExisting = set(listRelative)
            listProblem += [f"{path}: 없는 파일의 기준선 — `--ratchet` 으로 지운다" for path in sorted(baseline) if path not in setExisting]
        for problem in listProblem:
            print(f"  - {problem}")
        print(f"[ResultTypes] {'FAIL' if listProblem else 'OK'} ({len(listProblem)}건)")
        return ExitCode.Violation if listProblem else ExitCode.Passed
    if args.ratchet or args.accept is not None:
        listFile = listSourceFiles(projectRoot)
        listExisting = {path.relative_to(projectRoot).as_posix() for path in listFile}
        baselinePath = projectRoot / kBaselinePath
        baseline = readBaseline(projectRoot) if baselinePath.is_file() else {}
        if args.accept is not None:
            listAccept = [Path(path).resolve() for path in args.accept]
            actual = countFiles(projectRoot, [path for path in listAccept if path.is_file()])
            for path in listAccept:
                relative = path.relative_to(projectRoot).as_posix()
                if relative.startswith(kStrictPrefix):
                    print(f"[ResultTypes] {relative}: Core 는 기준선이 없다(0 이어야 한다) — 받지 않는다")
                    return ExitCode.Violation
                print(f"[ResultTypes] accept {relative}: {baseline.get(relative, {})} -> {actual.get(relative, {})}")
                baseline[relative] = actual.get(relative, {})
        else:
            baseline = lowerBaseline(baseline, countFiles(projectRoot, listFile), listExisting)
        writeBaseline(projectRoot, baseline)
        print(f"[ResultTypes] baseline -> {kBaselinePath}")
        return ExitCode.Passed

    printSummary(countFiles(projectRoot, listSourceFiles(projectRoot)))
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
