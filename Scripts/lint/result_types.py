"""
Scripts/lint/result_types.py

결과 형식 래칫. 값 또는 실패 이유는 `sw::expected<T, E>` 한 벌(`Core/Foundation/Expected.h`, "없음이 정상" 은 `sw::optional<T>`)로 돌려준다 —
규칙은 `docs/agents/CppStyle.md Errors`. 이 검사는 옛 모양 넷을 센다:

- `std-optional`: `std::optional` · `std::nullopt` 직접 사용(주석 · 문자열 · 전처리 줄은 세지 않는다).
- `out-error`: `string& outError` · `string& outReason`(`sw::string&` · `outErrorText` 처럼 이름이 이어져도) 출력 인자 모양.
- `out-error-pointer`: 같은 결함의 포인터 철자 `string* pOutError` · `pOutReason` · `pOutMessage`(널이면 이유를 버린다 — 2026-11-09, 참조형만 세던 사이 남은 자리).
- `out-text-on-failure`: 이름은 달라도(`outReply` · `outUnknown` · `outFailureText` …) 실패 갈래에서만 쓰는 글 출력 인자 — 함수 본문의 모든 쓰기 바로 뒤가
  `return false` · `nullptr` · `nullopt` · `unexpected` 이고 그 인자를 다른 함수에 넘기지 않는 정의만 센다(넘기면 성공 쪽 쓰기를 볼 수 없어 세지 않는다).

왜 린트인가. 형식(`sw::expected`)은 새 함수가 옛 모양을 쓰지 못하게 막지 못한다 — `std::optional` 도 `string&` 도 언어가 준다. 그래서 모든 파일이
0 이어야 하고, 예외는 `Config/Rules/Lint/ResultTypes.json` 의 `_exemptions`(`경로|종류` → 까닭)에만 둔다. 예전에는 Core 밖 파일마다 종류별 수를
기준선에 두는 래칫이었다 — 옮김이 끝나 기준선이 비었으므로(2026-11-21) 기준선 · `--ratchet` · `--accept` 를 지우고 Core 와 같은 규칙 하나로 둔다.

    py -3 -m Scripts lint result-types [--self-check]
"""

from __future__ import annotations

import re
from typing import Mapping

from lint.cpp_code import BODY_OPEN, find_param_list_end, is_source_file, match_brace, strip_code
from lint.lint_rule import LintRule, SourceFile
from lint.lint_scope import LintScope
from lint.rule_tables import ExceptionTable


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
    "std-optional": "`sw::optional<T>`(없음이 정상) 이나 `sw::expected<T, E>`(이유가 둘 이상)로 — docs/agents/CppStyle.md Errors",
    "out-error": "`bool f( …, string& outError )` 대신 `sw::expected<T>` · `sw::expected<void>` 를 돌려준다 — docs/agents/CppStyle.md Errors",
    "out-error-pointer": "`bool f( …, string* pOutError = nullptr )` 도 같은 결함이다 — `sw::expected<T>` 를 돌려주고 부르는 쪽이 버릴지 정한다(docs/agents/CppStyle.md Errors)",
    "out-text-on-failure": "실패 때만 쓰는 글 출력 인자는 이유다 — `sw::expected<T>`(또는 `expected<T, DomainError>`)의 오류로 돌려준다(docs/agents/CppStyle.md Errors)",
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


def count_kinds(code: str) -> dict[str, int]:
    """주석 · 문자열을 지운 코드에서 종류별 수입니다(0 인 종류는 적지 않는다)."""
    counts = {kind: len(pattern.findall(code)) for kind, pattern in PATTERN.items()}
    counts["out-text-on-failure"] = count_failure_only_text(code)
    return {kind: count for kind, count in counts.items() if count > 0}


def find_problems(found: Mapping[str, Mapping[str, int]], table: ExceptionTable) -> list[str]:
    """어느 파일이나 0 이어야 한다(예외 `경로|종류` 말고)."""
    list_problem = [f"{path}: {kind} {count} — 0 이어야 한다: {ADVICE[kind]}" for path, counts in found.items() for kind, count in counts.items()
                    if not table.allows("_exemptions", f"{path}|{kind}")]
    return list_problem + table.list_problems(whole_repo=False)   # 예외의 낡은 줄은 세지 않는다


class ResultTypesRule(LintRule):
    id = "result-types"
    group = "모양"
    blocks = "std::optional · string& outError 모양(어디서나 0 — 예외는 까닭과 함께 표에)"
    why_not_type = "언어가 주는 모양이다"
    tables = ("",)

    def selects(self, path: str) -> bool:
        return is_source_file(path)

    def inspect(self, file: SourceFile) -> dict[str, int]:
        return count_kinds(file.code)

    def evaluate(self, scope: LintScope, found: Mapping[str, Mapping[str, int]]) -> list[str]:
        return find_problems(found, self.load_exceptions("_exemptions"))

    def self_check(self) -> None:
        def count_text(text: str) -> dict[str, int]:
            return count_kinds(strip_code(text))

        assert count_text("std::optional<int32> find();") == {"std-optional": 1}, "std::optional is counted"
        assert count_text("return std::nullopt;") == {"std-optional": 1}, "std::nullopt is counted"
        assert count_text("sw::optional<int32> find(); // std::optional was here\nconst char* p = \"std::optional\";") == {}, "comments, strings and sw::optional are not counted"
        assert count_text("#include <optional>\n") == {}, "the include line is not counted"
        assert count_text("bool parse( string_view text, string& outError );") == {"out-error": 1}, "string& outError is counted"
        assert count_text("bool gate( sw::string_view path, sw::string& outReason );") == {"out-error": 1}, "sw::string& outReason is counted"
        assert count_text("bool take( StreamCloseReason& outReason );") == {}, "an enum out parameter is not counted"
        assert count_text("void f( string& outText );") == {}, "other string outputs are not counted"
        assert count_text("bool load( string_view text, string* pOutError = nullptr );") == {"out-error-pointer": 1}, "string* pOutError is counted"
        assert count_text("bool decode( sw::string* pOutReason ); void g( string* pOutMessage );") == {"out-error-pointer": 2}, "pOutReason · pOutMessage are counted"
        assert count_text("bool resolve( string* pOutAbsPath );") == {}, "a pointer to a value output is not counted"
        failure_only = "bool find( int32 id, string& outReply )\n{\n    if ( id < 0 )\n    {\n        outReply = \"bad id\";\n        return false;\n    }\n    return true;\n}\n"
        assert count_text(failure_only) == {"out-text-on-failure": 1}, "a text output written only before `return false` is counted"
        assert count_text("Item* pick( string& outUnknown ) { if ( a ) { outUnknown = name; return nullptr; } return p; }") == {"out-text-on-failure": 1}, "`return nullptr` is a failure return"
        both_sides = "bool run( string& outReply )\n{\n    if ( a )\n    {\n        outReply = \"no\";\n        return false;\n    }\n    outReply = \"done\";\n    return true;\n}\n"
        assert count_text(both_sides) == {}, "a text output also written on success is not counted"
        assert count_text("bool run( string& outReply ) { if ( a ) { outReply = \"no\"; return false; } return inner( outReply ); }") == {}, "a text output passed on is not counted"
        assert count_text("bool run( string& outReply );") == {}, "a declaration is not counted"
        exemptions = ExceptionTable.make(self.id, _exemptions={"Source/Core/Foundation/Expected.h|std-optional": "bridge"})
        assert find_problems({"Source/Engine/A.cpp": {}}, exemptions) == [], "zero passes"
        assert len(find_problems({"Source/Engine/A.cpp": {"std-optional": 1}}, exemptions)) == 1, "any count outside Core fails"
        assert len(find_problems({"Source/Core/X.h": {"out-error-pointer": 2}}, exemptions)) == 1, "any count in Core fails"
        assert find_problems({"Source/Core/Foundation/Expected.h": {"std-optional": 4}}, exemptions) == [], "an exempt kind passes"
        assert len(find_problems({"Source/Core/Foundation/Expected.h": {"out-error": 1}}, exemptions)) == 1, "an exemption covers only its kind"
