#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/result_types.py

결과 형식 래칫. 값 또는 실패 이유는 `sw::expected<T, E>` 한 벌(`Core/Foundation/Expected.h`, "없음이 정상" 은 `sw::optional<T>`)로 돌려준다 —
규칙은 `docs/agents/Errors.md`. 이 검사는 옛 모양 넷을 센다:

- `std-optional`: `std::optional` · `std::nullopt` 직접 사용(주석 · 문자열 · 전처리 줄은 세지 않는다).
- `out-error`: `string& outError` · `string& outReason`(`sw::string&` · `outErrorText` 처럼 이름이 이어져도) 출력 인자 모양.
- `out-error-pointer`: 같은 결함의 포인터 철자 `string* pOutError` · `pOutReason` · `pOutMessage`(널이면 이유를 버린다 — 2026-11-09, 참조형만 세던 사이 남은 자리).
- `out-text-on-failure`: 이름은 달라도(`outReply` · `outUnknown` · `outFailureText` …) 실패 갈래에서만 쓰는 글 출력 인자 — 함수 본문의 모든 쓰기 바로 뒤가
  `return false` · `nullptr` · `nullopt` · `unexpected` 이고 그 인자를 다른 함수에 넘기지 않는 정의만 센다(넘기면 성공 쪽 쓰기를 볼 수 없어 세지 않는다).

**왜 린트인가.** 형식(`sw::expected`)은 새 함수가 옛 모양을 쓰지 못하게 막지 못한다 — `std::optional` 도 `string&` 도 언어가 준다. 그래서 모든 파일이
**0** 이어야 하고, 예외는 `Config/Rules/Lint/ResultTypes.json` 의 `_exemptions`(`경로|종류` → 까닭)에만 둔다. 예전에는 Core 밖 파일마다 종류별 수를
기준선에 두는 래칫이었다 — 옮김이 끝나 기준선이 비었으므로(2026-11-21) 기준선 · `--ratchet` · `--accept` 를 지우고 Core 와 같은 규칙 하나로 둔다.

    py -3 -m Scripts lint result-types                    # 폴더별 남은 수
    py -3 -m Scripts lint result-types --check [--all]    # 검사(커밋 훅은 staged 파일)
    py -3 -m Scripts lint result-types --self-check
"""

from __future__ import annotations

import argparse
import collections
import re
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts

from common.exit_code import ExitCode  # noqa: E402
from lint.cpp_shape import find_param_list_end, BODY_OPEN, match_brace, strip_code  # noqa: E402
from lint.format import get_modified_cpp_files  # noqa: E402
from lint.lint_scope import LintScope, read_text  # noqa: E402
from lint.magic_literals import is_source_file, list_source_files  # noqa: E402
from lint.rule_tables import ExceptionTable  # noqa: E402


RULE_ID = "result-types"
KINDS = ("std-optional", "out-error", "out-error-pointer", "out-text-on-failure")
PATTERN = {
    "std-optional": re.compile(r"\bstd\s*::\s*(?:optional\b|nullopt\b|nullopt_t\b)"),
    "out-error": re.compile(r"\b(?:sw\s*::\s*)?string\s*&\s*out\w*(?:Error|Reason)\w*\b"),
    "out-error-pointer": re.compile(r"\b(?:sw\s*::\s*)?string\s*\*\s*pOut\w*(?:Error|Reason|Message)\w*\b"),
}
# out-text-on-failure: 이름이 이유를 말하지 않는 `string& outX` 정의(이유 이름은 위 out-error 가 센다)
OUT_TEXT_PARAM = re.compile(r"\b(?:sw\s*::\s*)?string\s*&\s*(out\w+)\s*(?=[,)])")
REASON_NAME = re.compile(r"Error|Reason|Message")
FAILURE_RETURN = re.compile(r"\s*return\s+(?:false\b|nullptr\b|(?:std\s*::\s*|sw\s*::\s*)?nullopt\b|(?:sw\s*::\s*)?unexpected\b)")
ADVICE = {
    "std-optional": "`sw::optional<T>`(없음이 정상) 이나 `sw::expected<T, E>`(이유가 둘 이상)로 — docs/agents/Errors.md",
    "out-error": "`bool f( …, string& outError )` 대신 `sw::expected<T>` · `sw::expected<void>` 를 돌려준다 — docs/agents/Errors.md",
    "out-error-pointer": "`bool f( …, string* pOutError = nullptr )` 도 같은 결함이다 — `sw::expected<T>` 를 돌려주고 부르는 쪽이 버릴지 정한다(docs/agents/Errors.md)",
    "out-text-on-failure": "실패 때만 쓰는 글 출력 인자는 이유다 — `sw::expected<T>`(또는 `expected<T, DomainError>`)의 오류로 돌려준다(docs/agents/Errors.md)",
}


def is_failure_only_write(body: str, write: re.Match[str]) -> bool:
    """쓰기 문장 바로 다음 문장이 실패 반환인지 봅니다."""
    semicolon = body.find(";", write.end())
    return semicolon >= 0 and FAILURE_RETURN.match(body, semicolon + 1) is not None


def count_failure_only_text(code: str) -> int:
    """`string& outX` 인자를 실패 갈래에서만 쓰는 함수 정의 수입니다(선언 · 쓰지 않는 정의 · 다른 함수에 넘기는 정의는 세지 않는다)."""
    count = 0
    for match in OUT_TEXT_PARAM.finditer(code):
        name = match.group(1)
        if REASON_NAME.search(name):
            continue
        opening = BODY_OPEN.match(code, find_param_list_end(code, match.end()) + 1)
        if opening is None:
            continue
        body = code[opening.end() - 1:match_brace(code, opening.end() - 1)]
        list_write = list(re.finditer(r"(?<![\w.>])" + name + r"\s*(?:=(?!=)|\+=|\.\s*(?:append|assign|push_back)\s*\()", body))
        if not list_write or re.search(r"[(,]\s*" + name + r"\s*[,)]", body):
            continue
        if all(is_failure_only_write(body, write) for write in list_write):
            count += 1
    return count


def count_text(text: str) -> dict[str, int]:
    """주석 · 문자열을 지운 코드에서 종류별 수입니다(0 인 종류는 적지 않는다)."""
    code = strip_code(text)
    counts = {kind: len(pattern.findall(code)) for kind, pattern in PATTERN.items()}
    counts["out-text-on-failure"] = count_failure_only_text(code)
    return {kind: count for kind, count in counts.items() if count > 0}


def count_files(project_root: Path, list_file: Iterable[Path]) -> dict[str, dict[str, int]]:
    result: dict[str, dict[str, int]] = {}
    for file in list_file:
        text = read_text(file, errors="ignore")
        if text is None:
            raise FileNotFoundError(file)   # scope 의 파일은 있다 — 예전 read_text 처럼 멈춘다
        counts = count_text(text)
        if counts:
            result[file.relative_to(project_root).as_posix()] = counts
    return result


def find_problems(actual: dict[str, dict[str, int]], list_path: Iterable[str], table: ExceptionTable) -> list[str]:
    """`list_path` 파일마다 0 이어야 한다(예외 `경로|종류` 말고)."""
    list_problem = [f"{path}: {kind} {count} — 0 이어야 한다: {ADVICE[kind]}" for path in sorted(set(list_path)) for kind, count in actual.get(path, {}).items()
                    if not table.allows("_exemptions", f"{path}|{kind}")]
    return list_problem + table.list_problems(whole_repo=False)   # 예외의 낡은 줄은 예전처럼 세지 않는다


def load_exemptions(project_root: Path) -> ExceptionTable:
    """예외 `경로|종류` → 까닭(`ResultTypes.json`)."""
    return ExceptionTable.load(project_root, RULE_ID, ("_exemptions",))


def check_result_types(project_root: Path, scope: LintScope) -> list[str]:
    """scope 의 C++ 파일 검사입니다."""
    list_relative = [relative for relative in scope.select() if is_source_file(relative)]
    if not list_relative:
        return []
    return find_problems(count_files(project_root, [project_root / relative for relative in list_relative]), list_relative, load_exemptions(project_root))


def print_summary(actual: dict[str, dict[str, int]]) -> None:
    count_by_folder: collections.Counter[tuple[str, str]] = collections.Counter()
    for path, counts in actual.items():
        folder = "/".join(path.split("/")[:2])
        for kind, count in counts.items():
            count_by_folder[(folder, kind)] += count
    list_folder = sorted({folder for folder, _ in count_by_folder})
    print(f"{'folder':<28}" + "".join(f"{kind:>14}" for kind in KINDS))
    for folder in list_folder:
        print(f"{folder:<28}" + "".join(f"{count_by_folder[(folder, kind)]:>14}" for kind in KINDS))
    print(f"{'total':<28}" + "".join(f"{sum(count for (_, k), count in count_by_folder.items() if k == kind):>14}" for kind in KINDS))


def run_self_check() -> int:
    failures = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal failures
        if not condition:
            print(f"[ResultTypes] self-check: {message}")
            failures += 1

    expect(count_text("std::optional<int32> find();") == {"std-optional": 1}, "std::optional is counted")
    expect(count_text("return std::nullopt;") == {"std-optional": 1}, "std::nullopt is counted")
    expect(count_text("sw::optional<int32> find(); // std::optional was here\nconst char* p = \"std::optional\";") == {}, "comments, strings and sw::optional are not counted")
    expect(count_text("#include <optional>\n") == {}, "the include line is not counted")
    expect(count_text("bool parse( string_view text, string& outError );") == {"out-error": 1}, "string& outError is counted")
    expect(count_text("bool gate( sw::string_view path, sw::string& outReason );") == {"out-error": 1}, "sw::string& outReason is counted")
    expect(count_text("bool take( StreamCloseReason& outReason );") == {}, "an enum out parameter is not counted")
    expect(count_text("void f( string& outText );") == {}, "other string outputs are not counted")
    expect(count_text("bool load( string_view text, string* pOutError = nullptr );") == {"out-error-pointer": 1}, "string* pOutError is counted")
    expect(count_text("bool decode( sw::string* pOutReason ); void g( string* pOutMessage );") == {"out-error-pointer": 2}, "pOutReason · pOutMessage are counted")
    expect(count_text("bool resolve( string* pOutAbsPath );") == {}, "a pointer to a value output is not counted")
    failure_only = "bool find( int32 id, string& outReply )\n{\n    if ( id < 0 )\n    {\n        outReply = \"bad id\";\n        return false;\n    }\n    return true;\n}\n"
    expect(count_text(failure_only) == {"out-text-on-failure": 1}, "a text output written only before `return false` is counted")
    expect(count_text("Item* pick( string& outUnknown ) { if ( a ) { outUnknown = name; return nullptr; } return p; }") == {"out-text-on-failure": 1}, "`return nullptr` is a failure return")
    both_sides = "bool run( string& outReply )\n{\n    if ( a )\n    {\n        outReply = \"no\";\n        return false;\n    }\n    outReply = \"done\";\n    return true;\n}\n"
    expect(count_text(both_sides) == {}, "a text output also written on success is not counted")
    expect(count_text("bool run( string& outReply ) { if ( a ) { outReply = \"no\"; return false; } return inner( outReply ); }") == {}, "a text output passed on is not counted")
    expect(count_text("bool run( string& outReply );") == {}, "a declaration is not counted")
    exemptions = ExceptionTable.make(RULE_ID, _exemptions={"Source/Core/Foundation/Expected.h|std-optional": "bridge"})
    expect(find_problems({"Source/Engine/A.cpp": {}}, ["Source/Engine/A.cpp"], exemptions) == [], "zero passes")
    expect(len(find_problems({"Source/Engine/A.cpp": {"std-optional": 1}}, ["Source/Engine/A.cpp"], exemptions)) == 1, "any count outside Core fails")
    expect(len(find_problems({"Source/Core/X.h": {"out-error-pointer": 2}}, ["Source/Core/X.h"], exemptions)) == 1, "any count in Core fails")
    expect(find_problems({"Source/Core/Foundation/Expected.h": {"std-optional": 4}}, ["Source/Core/Foundation/Expected.h"], exemptions) == [], "an exempt kind passes")
    expect(len(find_problems({"Source/Core/Foundation/Expected.h": {"out-error": 1}}, ["Source/Core/Foundation/Expected.h"], exemptions)) == 1, "an exemption covers only its kind")
    print(f"[ResultTypes] self-check {'OK' if failures == 0 else 'FAIL'}")
    return ExitCode.PASSED if failures == 0 else ExitCode.VIOLATION


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="결과 형식 검사: std::optional · string& outError · string* pOutError · 실패 때만 쓰는 글 출력 인자 모양이 어디에도 없게")
    parser.add_argument("--check", action="store_true", help="수정된 C++ 파일(작업 트리 대 HEAD)을 검사한다(--all 은 모든 파일)")
    parser.add_argument("--all", action="store_true", help="--check 와 함께: 모든 Source 파일")
    parser.add_argument("--self-check", action="store_true", help="세기 · 판정의 자체 검사")
    args = parser.parse_args(argv)
    sys.stdout.reconfigure(encoding="utf-8")
    if args.self_check:
        return run_self_check()

    project_root = Path(__file__).resolve().parents[2]
    if args.check:
        list_file = list_source_files(project_root) if args.all else get_modified_cpp_files(project_root)
        list_relative = [path.relative_to(project_root).as_posix() for path in list_file]
        list_source = [path for path, relative in zip(list_file, list_relative) if is_source_file(relative)]
        list_problem = find_problems(count_files(project_root, list_source), [relative for relative in list_relative if is_source_file(relative)], load_exemptions(project_root))
        for problem in list_problem:
            print(f"  - {problem}")
        print(f"[ResultTypes] {'FAIL' if list_problem else 'OK'} ({len(list_problem)}건)")
        return ExitCode.VIOLATION if list_problem else ExitCode.PASSED
    print_summary(count_files(project_root, list_source_files(project_root)))
    return ExitCode.PASSED


if __name__ == "__main__":
    sys.exit(main())
