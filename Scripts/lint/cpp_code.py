"""
Scripts/lint/cpp_code.py

C++ 글을 보는 규칙이 함께 쓰는 것 — 주석 · 문자열 · 전처리 줄 지우기(자리를 지킨다), 중괄호 · 인자 목록의 짝, 줄 번호.
"""

from __future__ import annotations

import re

STRIP_START = re.compile(r'//|/\*|R"|["\']')   # 지울 것이 시작할 수 있는 자리 — 그 사이 글자는 그대로 둔다
RAW_STRING_HEAD = re.compile(r'R"([^(]*)\(')
PREPROCESSOR_LINE = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*$", re.M)


def blank_text(text: str) -> str:
    """줄 바꿈만 남기고 같은 길이의 공백으로."""
    return "\n".join(" " * len(part) for part in text.split("\n"))


def strip_comments_and_strings(text: str) -> str:
    """
    주석 · 문자열 · 문자 상수를 같은 길이의 공백으로 바꿉니다(줄 번호를 지킨다).
    지울 것이 시작할 수 있는 자리(`STRIP_START`)로 건너뛴다 — 글자마다 파이썬으로 돌던 것보다 약 5 배 빠르다.
    """
    list_piece: list[str] = []
    copied = 0
    size = len(text)

    def blank(start: int, end: int) -> None:
        nonlocal copied
        end = min(end, size)
        list_piece.append(text[copied:start])
        list_piece.append(blank_text(text[start:end]))
        copied = max(end, start)

    index = 0
    while True:
        match = STRIP_START.search(text, index)
        if match is None:
            break
        index = match.start()
        char = text[index]
        if text.startswith("//", index):
            end = text.find("\n", index)
            end = size if end < 0 else end
            blank(index, end)
            index = end
        elif text.startswith("/*", index):
            end = text.find("*/", index + 2)
            end = size if end < 0 else end + 2
            blank(index, end)
            index = end
        elif char == "R" and text.startswith('R"', index) and (index == 0 or not (text[index - 1].isalnum() or text[index - 1] == "_")):
            head = RAW_STRING_HEAD.match(text, index)
            if head is None:
                index += 1
                continue
            end = text.find(")" + head.group(1) + '"', index)
            end = size if end < 0 else end + len(head.group(1)) + 2
            blank(index + 1, end)
            index = end
        elif char == '"' or (char == "'" and not (index > 0 and (text[index - 1].isalnum() or text[index - 1] == "_"))):
            end = index + 1
            while end < size and text[end] != char and text[end] != "\n":
                end += 2 if text[end] == "\\" else 1
            blank(index + 1, end)
            index = end + 1
        else:
            index += 1
    list_piece.append(text[copied:])
    return "".join(list_piece)


def blank_preprocessor(text: str) -> str:
    """전처리 줄(이어 쓴 줄 포함)을 같은 길이의 공백으로 바꿉니다."""
    return PREPROCESSOR_LINE.sub(lambda m: blank_text(m.group(0)), text)


def strip_code(text: str) -> str:
    """주석 · 문자열 · 문자 상수 · 전처리 줄을 같은 길이의 공백으로 바꿉니다."""
    return blank_preprocessor(strip_comments_and_strings(text))


def match_brace(text: str, open_index: int) -> int:
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(text) - 1


BODY_OPEN = re.compile(r"\s*(?:const\s*|noexcept\s*|override\s*|final\s*|->\s*[\w:<>]+\s*)*\{")  # 인자 목록 `)` 뒤의 함수 본문 시작


def find_param_list_end(code: str, index: int) -> int:
    """인자 목록 안의 @p index 에서 그 목록을 닫는 `)` 위치입니다(없으면 글 끝)."""
    depth = 0
    for position in range(index, len(code)):
        if code[position] == "(":
            depth += 1
        elif code[position] == ")":
            if depth == 0:
                return position
            depth -= 1
    return len(code)


def get_line_of(text: str, index: int) -> int:
    return text.count("\n", 0, index) + 1


SKIP_FOLDERS = ("ThirdParty", "Generated", ".gen.")


def is_our_cpp(relative: str) -> bool:
    return relative.startswith("Source/") and relative.endswith((".h", ".cpp")) and not any(skip in relative for skip in SKIP_FOLDERS)


SOURCE_EXTENSIONS = (".h", ".cpp", ".inl")


def is_source_file(relative: str) -> bool:
    return relative.startswith("Source/") and relative.endswith(SOURCE_EXTENSIONS) and "/ThirdParty/" not in relative
