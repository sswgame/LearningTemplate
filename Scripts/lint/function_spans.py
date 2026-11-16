#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/function_spans.py

C++ 글에서 함수 정의와 람다의 본문 범위를 찾아 이름을 붙이는 공용 도우미다. 예외 표를 `경로|함수` 열쇠로 두는 규칙(common-shapes 의 `assetEntrance`,
frame-intern)이 모두 이것으로 "이 자리를 담은 함수" 를 정한다 — 규칙마다 정의 줄 패턴을 따로 두다 보니 한쪽만 고친 근사가 갈렸다.

- 함수: 반환 형식 · 이름 · 인자 · `{` 꼴의 정의(`형식::함수` 는 적힌 그대로). 제어문(`else if` 등)은 함수가 아니다. 생성자(반환 형식 없음)는 보지 않는다.
- 람다: `[캡처]( 인자 ) {` · `[캡처] {` 를 담은 함수(또는 람다) 이름 + `#lambda<순번>`(담은 것 안에서 나온 차례, 1 부터) — 예외 함수 안에 새 람다가 생기면
  열쇠가 달라져 걸린다.
- 근사다: 매크로가 만든 함수 · 여러 줄에 걸친 반환 형식은 이름을 얻지 못한다(그 자리는 `?`).

    py -3 Scripts/lint/function_spans.py --self-check
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from typing import Sequence

FUNCTION_HEAD = re.compile(
    r"^[ \t]*(?:template\s*<[^>]*>\s*)?(?:\[\[nodiscard\]\]\s*)?(?:[\w:<>,]+[ \t*&]+)+([\w:~]+)[ \t]*"
    r"\((?:[^;{}()]|\([^;{}()]*\)|\{[^;{}()]*\})*\)\s*(?:const\s*)?(?:(?:override|final|noexcept)\s*)*(?:->\s*[\w:<>*&]+\s*)?\{",
    re.M,
)
LAMBDA_HEAD = re.compile(r"(?<![\w\]\)>])\[[^\[\]\n;]*\]\s*(?:\((?:[^;{}()]|\([^;{}()]*\))*\)\s*)?(?:mutable\s*)?(?:->\s*[\w:<>*&]+\s*)?\{")
CONTROL_WORDS = frozenset({"if", "for", "while", "switch", "catch", "return", "else", "do", "sizeof", "decltype"})
UNKNOWN = "?"


@dataclass(frozen=True)
class FunctionSpan:
    name: str
    open_index: int   # 여는 `{`
    close_index: int  # 닫는 `}`(없으면 글 끝)


def find_closing_brace(code: str, open_index: int) -> int:
    depth = 0
    for index in range(open_index, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(code)


def find_function_spans(code: str) -> list[FunctionSpan]:
    """주석을 지운 @p code 의 함수 · 람다 본문 범위를 여는 자리 순서로 돌려준다."""
    list_head: list[tuple[int, str | None]] = []   # (여는 자리, 함수 이름 — 람다면 None)
    for match in FUNCTION_HEAD.finditer(code):
        if match.group(1) not in CONTROL_WORDS:
            list_head.append((match.end() - 1, match.group(1)))
    for match in LAMBDA_HEAD.finditer(code):
        list_head.append((match.end() - 1, None))
    list_head.sort()
    list_span: list[FunctionSpan] = []
    list_open: list[FunctionSpan] = []          # 지금 자리를 담은 범위들(바깥부터)
    lambda_count: dict[int, int] = {}           # 담은 범위의 여는 자리 → 그 안 람다 수
    for open_index, name in list_head:
        while list_open and list_open[-1].close_index < open_index:
            list_open.pop()
        if name is None:
            parent = list_open[-1] if list_open else None
            parent_key = parent.open_index if parent is not None else -1
            lambda_count[parent_key] = lambda_count.get(parent_key, 0) + 1
            name = f"{parent.name if parent is not None else UNKNOWN}#lambda{lambda_count[parent_key]}"
        span = FunctionSpan(name, open_index, find_closing_brace(code, open_index))
        list_span.append(span)
        list_open.append(span)
    return list_span


def run_self_check() -> None:
    code = (
        "bool A::loadFromAsset( const AssetID& id )\n{\n    if ( x )\n    {\n        call();\n    }\n"
        "    each( []( int v ) { use( v ); } );\n    each( [&] { other(); } );\n}\n\n"
        "static EditorMarkHit evaluate( const State& s, const float2& a = float2{ 0.5f, 0.5f } )\n{\n    run( [this]() mutable {\n        inner( [] { deep(); } );\n    } );\n}\n"
        "int32 arr[2]{ 1, 2 };\n"
    )
    list_span = find_function_spans(code)
    names = [span.name for span in list_span]
    assert names == ["A::loadFromAsset", "A::loadFromAsset#lambda1", "A::loadFromAsset#lambda2", "evaluate", "evaluate#lambda1", "evaluate#lambda1#lambda1"], names   # 제어문 블록 · 배열 초기화는 함수 · 람다가 아니다


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="함수 · 람다 본문 범위 도우미의 자체 검사")
    parser.add_argument("--self-check", action="store_true")
    parser.parse_args(argv)
    run_self_check()
    print("[FunctionSpans] self-check OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
