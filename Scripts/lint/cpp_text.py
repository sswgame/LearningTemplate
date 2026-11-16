#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/cpp_text.py

C++ 글을 훑는 린트가 함께 쓰는 도우미: 주석 · 글 리터럴을 빈칸으로 지우기, 호출의 최상위 인자 가르기.
"""

from __future__ import annotations

import re

COMMENT_OR_STRING = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", re.S)


def blank_comments_and_strings(text: str) -> str:
    """주석 · 글 리터럴을 같은 길이의 빈칸으로 바꾼다(줄 번호와 괄호 짝을 지킨다)."""
    return COMMENT_OR_STRING.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)


def split_arguments(code: str, start: int) -> list[str]:
    """`start` 가 여는 괄호 바로 뒤일 때 최상위 쉼표로 가른 인자들을 돌려준다."""
    list_argument: list[str] = []
    depth = 0
    begin = start
    cursor = start
    while cursor < len(code):
        char = code[cursor]
        if char in "([{":
            depth += 1
        elif char in ")]}":
            if depth == 0:
                list_argument.append(code[begin:cursor])
                return list_argument
            depth -= 1
        elif char == "," and depth == 0:
            list_argument.append(code[begin:cursor])
            begin = cursor + 1
        cursor += 1
    return list_argument
