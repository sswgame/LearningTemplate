"""
키트 뼈대(new-kit · new-online-kit)의 C++ 이 지금 저장소의 API 와 맞는지 빌드 없이 본다.

뼈대는 글이라 엔진 형식의 이름 · 자리가 바뀌어도 아무 빌드도 깨지지 않는다(키트 이름 상수가 다른 헤더로 옮겨진 뒤 new-online-kit 이 조용히 낡았다).
파일마다 include 를 저장소에서 끝까지 따라가, 쓴 이름(대문자로 시작하는 이름 · 부르는 함수)이 그 헤더들이나 뼈대 자신에 있는지 대조한다.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.paths import get_project_root  # noqa: E402

PCH = "Engine/pch.h"   # GameFramework 모듈이 쓰는 pch
CODE_SUFFIXES = (".h", ".cpp")
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)
NOISE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|^\s*#\s*include[^\n]*', re.S | re.M)
USED_NAME = re.compile(r"(?<![\w$])([A-Z]\w+)|(?<![\w$])([a-z]\w*)\s*\(")
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
KEYWORDS = frozenset({"if", "for", "while", "switch", "return", "sizeof", "catch", "static_assert", "decltype", "alignof"})


def find_included_file(include: str, include_path: str, map_file: dict[str, str]) -> str | None:
    """include 의 글. 뼈대 안의 파일(`include_path` 머리)이면 뼈대에서, 아니면 저장소 `Source/` 에서 읽는다. 없으면 None."""
    if include.startswith(include_path + "/"):
        return map_file.get(include[len(include_path) + 1:])
    path = get_project_root() / "Source" / include
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else None


def find_problems(map_file: dict[str, str], include_path: str, tag: str, set_own: set[str]) -> list[str]:
    """
    `map_file` 은 키트 폴더 기준 경로 → 글, `include_path` 는 키트 폴더의 include 머리(`GameFramework/Kits/<묶음>/<이름>`), `tag` 는 키트 이름(그 이름이 든 것은 뼈대 자신의 것),
    `set_own` 은 키트 이름이 들지 않은 뼈대 자신의 이름이다.
    """
    list_problem: list[str] = []
    for relative, text in map_file.items():
        if not relative.endswith(CODE_SUFFIXES):
            continue
        set_known: set[str] = set()
        set_seen: set[str] = set()
        list_pending = INCLUDE.findall(text)
        for include in list_pending:
            if find_included_file(PCH if include == "pch.h" else include, include_path, map_file) is None:
                list_problem.append(f"{relative}: include '{include}' 가 없다")
        while list_pending:
            include = list_pending.pop()
            include = PCH if include == "pch.h" else include
            if include in set_seen:
                continue
            set_seen.add(include)
            included = find_included_file(include, include_path, map_file)
            if included is None:
                continue   # 생성 헤더 · 외부 헤더 — 뼈대가 직접 적은 것은 위에서 봤다
            set_known.update(IDENTIFIER.findall(included))
            list_pending.extend(INCLUDE.findall(included))
        for match in USED_NAME.finditer(NOISE.sub(" ", text)):
            name = match.group(1) or match.group(2)
            if name not in KEYWORDS | set_own and tag not in name and tag[0].lower() + tag[1:] not in name and name not in set_known:
                list_problem.append(f"{relative}: '{name}' 이 include 한 어느 헤더에도 없다")
    return sorted(set(list_problem))
