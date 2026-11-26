"""이름 · 경로 바꾸기 도구(`move-source` · `rename-symbol`)가 함께 쓰는 예외 규칙.

문서도 코드와 같이 바꾼다 — 문서를 건너뛰면 개명마다 규칙 문서(`docs/agents/`)의 이름 · 경로가 낡는다.
옛 이름을 일부러 적은 줄("쓰지 않는 것" 예, 이력)만 그 줄에 표지 `keep-old-name` 를 적어 남긴다(Markdown 은 `<!-- keep-old-name -->`).
린트 예외 표(`Config/Rules/Lint/*.json`)도 지금 이름을 가리키니 함께 고친다 — 옛 이름을 일부러 적는 두 곳만 지킨다(`rewrite_rename_target`).
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Callable

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
