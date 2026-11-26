"""이름 바꾸기 도구(`rename-symbol`)가 리플렉션 프로퍼티의 직렬화 이름을 데이터에서 함께 바꾸는 규칙."""

from __future__ import annotations

import re
from typing import Mapping


# ── 리플렉션 프로퍼티의 직렬화 이름 ──
# `PROPERTY(...)` 가 붙은 멤버 `_bRequiresForeground` 는 XML · JSON 에 `requiresForeground` 로 적힌다(`XMLFields::getAttributeName` · `JSONFields`).
# 멤버 이름만 바꾸면 데이터의 속성이 모르는 이름이 된다 — 개명 도구가 같은 규칙으로 직렬화 이름 쌍을 만들어 데이터에도 적용한다.
# (`XMLSerializer` 는 멤버 이름 `_foo` 를 글자 그대로 적으므로 낱말 치환이 이미 따라간다.)

COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
PROPERTY_OPEN = re.compile(r"^(?P<head>[^\n]*?)\bPROPERTY\s*\(", re.M)
FIELD_NAME = re.compile(r"\s*[\w:<>,\s*&()]+?[\s*&>](_[A-Za-z]\w*)\s*(?:[{=;\[:]|SW_)")   # `타입 _이름` 뒤에 초기값 · 배열 · 비트필드 · 스레드 주석 · 끝
EXPLICIT_NAME = re.compile(r'\bName\s*=\s*"([^"]+)"')


def make_serialized_name(member: str) -> str:
    """멤버 이름의 XML 속성 · JSON 키 이름 — `XMLFields::getAttributeName`(Source/Engine/Serialization/XML/XMLFields.cpp) ·
    `makeXMLAttributeName`(ReflectionParser/Run/ReflectionPipeline.cpp)과 같은 규칙이다. 규칙을 바꾸면 셋을 함께 고친다."""
    if not member.startswith("_"):
        return member
    name = member[1:]
    if len(name) > 1 and name[0] == "b" and "A" <= name[1] <= "Z":
        return name[1].lower() + name[2:]
    return name


def collect_reflected_properties(map_header: Mapping[str, str]) -> dict[str, list[tuple[str, str]]]:
    """리플렉션 입력(경로 → 글, 헤더 · .cpp)에서 `PROPERTY(...)` 가 붙은 칸을 모은다: 리플렉션 이름 → [(경로, 직렬화 이름)].
    리플렉션 이름은 `Name = "x"` 를 적었으면 그것(멤버 이름을 바꿔도 데이터 이름은 그대로다), 아니면 `_` 멤버 이름이다."""
    properties: dict[str, list[tuple[str, str]]] = {}
    for path, raw in map_header.items():
        if "PROPERTY" not in raw:
            continue
        text = COMMENT.sub(" ", raw)
        for match in PROPERTY_OPEN.finditer(text):
            if "#" in match.group("head"):   # 매크로 정의
                continue
            depth, end = 1, match.end()
            while depth and end < len(text):
                depth += {"(": 1, ")": -1}.get(text[end], 0)
                end += 1
            named = EXPLICIT_NAME.search(text, match.end(), end) or FIELD_NAME.match(text, end)
            if named is not None:
                properties.setdefault(named.group(1), []).append((path, make_serialized_name(named.group(1))))
    return properties


def is_xml_path(path: str) -> bool:
    return path.lower().endswith(".xml")


def make_serialized_pattern(name: str, xml: bool = False) -> re.Pattern[str]:
    """직렬화 이름이 적히는 꼴만 — 속성 · `이름=값` 토큰(`name=`), 요소(`<name>` · `</name>`), 따옴표로 감싼 키(`"name"`). 글 속 영어 낱말은 건드리지 않는다.
    @p xml 이면 `name = "x"` 처럼 `=` 둘레를 띄운 속성도 본다(XML 문법이 허용한다) — 코드의 `string name = "x"` 는 변수라 XML 파일에만 건다."""
    word = re.escape(name)
    spaced = rf'|(?<![\w.-]){word}(?=\s+=\s*["\'])' if xml else ""
    return re.compile(rf'(?<![\w.-]){word}(?==[^=])|(?<=<){word}(?=[\s/>])|(?<=</){word}(?=>)|(?<="){word}(?=\\?")' + spaced)


def plan_serialized_rename(properties: Mapping[str, list[tuple[str, str]]], map_text: Mapping[str, str], old: str, new: str) -> tuple[tuple[str, str] | None, list[str]]:
    """
    멤버 개명 @p old → @p new 가 데이터에서 함께 바꿀 직렬화 이름 쌍과, 바꿀 수 없는 까닭(있으면 도구가 멈춘다 — 폴백 없음).
    병합 뒤 다시 돌릴 때는 선언이 이미 새 이름이라 두 이름 모두로 찾는다. @p map_text 는 치환할 파일(경로 → 글)이다.
    """
    serialized_old, serialized_new = make_serialized_name(old), make_serialized_name(new)
    if (old not in properties and new not in properties) or serialized_old == serialized_new:
        return None, []
    list_use = [path for path, text in map_text.items() if serialized_old in text and make_serialized_pattern(serialized_old, is_xml_path(path)).search(text)]
    if not list_use:
        return None, []
    # 이름만으로는 어느 타입의 칸인지 모른다 — 같은 직렬화 이름을 쓰는 다른 멤버가 있으면 데이터의 그 이름이 누구 것인지 가릴 수 없다.
    list_other = [f"{member}({path})" for member, list_entry in properties.items() if member not in (old, new) for path, serialized in list_entry if serialized == serialized_old]
    where = ", ".join(list_use[:10]) + (" …" if len(list_use) > 10 else "")
    if list_other:
        return None, [f"직렬화 이름 '{serialized_old}' 를 다른 리플렉션 멤버도 쓴다: {', '.join(list_other[:10])} — 데이터({where})의 그 이름이 어느 것인지 가릴 수 없다"]
    if len(serialized_old) < 6 or re.search(r"[a-z0-9][A-Z]", serialized_old) is None:
        return None, [f"직렬화 이름 '{serialized_old}' 는 흔한 낱말이라 손으로 읽는 속성과 겹칠 수 있다 — 데이터({where})"]
    return (serialized_old, serialized_new), []


def replace_serialized_name(text: str, serialized_old: str, serialized_new: str, path: str = "") -> tuple[str, int]:
    """@p path 는 그 글의 파일(XML 이면 `=` 둘레를 띄운 속성도 바꾼다 — `make_serialized_pattern`)."""
    if serialized_old not in text:
        return text, 0
    return make_serialized_pattern(serialized_old, is_xml_path(path)).subn(lambda _: serialized_new, text)
