"""이름 · 경로 바꾸기 도구(`move-source` · `rename-symbol`)가 함께 쓰는 예외 규칙.

문서도 코드와 같이 바꾼다 — 문서를 건너뛰면 개명마다 규칙 문서(`docs/agents/`)의 이름 · 경로가 낡는다.
옛 이름을 일부러 적은 줄("쓰지 않는 것" 예, 이력)만 그 줄에 표지 `keep-old-name` 를 적어 남긴다(Markdown 은 `<!-- keep-old-name -->`).
린트 예외 표(`Config/Rules/Lint/*.json`)도 지금 이름을 가리키니 함께 고친다 — 옛 이름을 일부러 적는 두 곳만 지킨다(`rewriteRenameTarget`).
"""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Callable, Mapping

from common.git import run_git_or_raise
from lint.rule_tables import get_table_path

KEEP_OLD_NAME_MARKER = "keep-old-name"
BANNED_NAMES_PATH = get_table_path("naming-words", "BannedNames")     # 이름 이력 — `_bannedNames` 의 키가 옛 이름이다
RENAME_PAIRS_FOLDER = "Config/Rules/Renames/"   # 병합 뒤 다시 돌릴 쌍 표(`rename-symbol --pairs <표> --reapply`) — 옛 이름이 열쇠다

TEXT_SUFFIX = {".h", ".hpp", ".inl", ".cpp", ".c", ".cmake", ".txt", ".md", ".json", ".in", ".py", ".toml", ".xxx", ".natvis", ".hlsl", ".hlsli", ".xml", ".po", ".tpl",
               ".manifest", ".sh", ".js"}   # .js — `Tools/` 의 우리 VS Code 확장(launch-args · comment-fold)
CPP_SUFFIX = {".h", ".hpp", ".inl", ".cpp"}
SCAN_ROOT = ["Source", "Content", "Source/Programs/ReflectionParser", "Source/Programs/OnlineLoadBot", "cmake", "Scripts", "Tools", "docs", "Config", "IntegrationTests", "ThirdParty",
             "CMakeLists.txt", "CMakePresets.json", "AGENTS.md", "CLAUDE.md", ".claude/skills", ".claude/hooks"]
SKIP_SCAN_PART = "/vcpkg-port/"   # ThirdParty 는 우리 감싸기 CMake 만 — vcpkg 포트는 남의 코드다
#: 소스 이름 바꾸기에서 문자열을 건너뛰는 C 계열 형식(`rename_symbol`) — 파일 줄기 바꾸기도 이 형식은 짧은 이름 규칙을 쓰지 않는다.
C_LIKE_SUFFIX = CPP_SUFFIX | {".c", ".hlsl", ".hlsli", ".xxx"}


def list_tracked_text(root: Path, suffixes: set[str] = TEXT_SUFFIX) -> list[Path]:
    """`SCAN_ROOT` 아래 추적 중인 글 파일(@p suffixes 형식)."""
    return [root / line for line in run_git_or_raise(["ls-files", "--", *SCAN_ROOT], cwd=root).splitlines()
            if Path(line).suffix.lower() in suffixes and SKIP_SCAN_PART not in line and (root / line).is_file()]


def read_text(path: Path) -> str:
    return path.read_bytes().decode("utf-8", "surrogateescape")


def write_text(path: Path, text: str) -> None:
    path.write_bytes(text.encode("utf-8", "surrogateescape"))


def dump_naming_table(data: dict) -> str:
    return json.dumps(data, ensure_ascii=False, indent=2) + "\n"


def is_rename_table(relative_path: str) -> bool:
    """옛 이름을 일부러 적는 표(이름 이력 · 쌍 표)면 true — `rewrite_rename_target` 이 따로 다룬다."""
    return relative_path == BANNED_NAMES_PATH or relative_path.startswith(RENAME_PAIRS_FOLDER)


def rewrite_rename_target(relative_path: str, text: str, rewrite: Callable[[str], str]) -> str:
    """
    이름 바꾸기 도구가 파일 하나(@p relative_path — 저장소 기준 posix)를 고친다. 예전에 두 도구가 따로 짰을 때 한 도구는 린트 표를 통째로 건너뛰고
    한 도구는 금지 이름 표의 키까지 바꿔, 개명마다 예외 표가 낡거나 금지 이름이 지워졌다 — 그래서 이 함수 하나로 둔다.
    """
    if relative_path.startswith(RENAME_PAIRS_FOLDER):
        return text
    if relative_path == BANNED_NAMES_PATH:
        data = json.loads(text)
        data["_bannedNames"] = {old: rewrite_unmarked_lines(advice, rewrite) for old, advice in data["_bannedNames"].items()}   # 키(옛 이름)는 지키고 안내의 새 이름은 따라간다
        return dump_naming_table(data)
    return rewrite_unmarked_lines(text, rewrite)


def rewrite_unmarked_lines(text: str, rewrite: Callable[[str], str]) -> str:
    """@p text 의 줄 가운데 표지가 없는 줄에만 @p rewrite 를 적용한다(표지가 없는 글은 한 번에 바꾼다)."""
    if KEEP_OLD_NAME_MARKER not in text:
        return rewrite(text)
    return "".join(line if KEEP_OLD_NAME_MARKER in line else rewrite(line) for line in text.splitlines(keepends=True))


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
    `makeXMLAttributeName`(ReflectionParser/ReflectionPipeline.cpp)과 같은 규칙이다. 규칙을 바꾸면 셋을 함께 고친다."""
    if not member.startswith("_"):
        return member
    name = member[1:]
    if len(name) > 1 and name[0] == "b" and "A" <= name[1] <= "Z":
        return name[1].lower() + name[2:]
    return name


def collect_reflected_properties(map_header: Mapping[str, str]) -> dict[str, list[tuple[str, str]]]:
    """헤더(경로 → 글)에서 `PROPERTY(...)` 가 붙은 칸을 모은다: 리플렉션 이름 → [(경로, 직렬화 이름)].
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
