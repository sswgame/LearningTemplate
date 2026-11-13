"""이름 · 경로 바꾸기 도구(`move-source` · `rename-symbol` · `Scripts/dev/rename_identifier.py`)가 함께 쓰는 예외 규칙.

문서도 코드와 같이 바꾼다 — 문서를 건너뛰면 개명마다 규칙 문서(`docs/agents/`)의 이름 · 경로가 낡는다.
옛 이름을 일부러 적은 줄("쓰지 않는 것" 예, 이력)만 그 줄에 표지 `keep-old-name` 를 적어 남긴다(Markdown 은 `<!-- keep-old-name -->`).
"""

from __future__ import annotations

from typing import Callable

KEEP_OLD_NAME_MARKER = "keep-old-name"


def rewrite_unmarked_lines(text: str, rewrite: Callable[[str], str]) -> str:
    """@p text 의 줄 가운데 표지가 없는 줄에만 @p rewrite 를 적용한다(표지가 없는 글은 한 번에 바꾼다)."""
    if KEEP_OLD_NAME_MARKER not in text:
        return rewrite(text)
    return "".join(line if KEEP_OLD_NAME_MARKER in line else rewrite(line) for line in text.splitlines(keepends=True))


def check_rewrite_unmarked_lines() -> None:
    """표지 줄은 지키고 나머지 줄은 바꾸는지 본다."""
    rename = lambda part: part.replace("OldName", "NewName")  # noqa: E731
    text = "use `OldName`\nnot `OldName` <!-- keep-old-name -->\n`OldName` again\n"
    assert rewrite_unmarked_lines(text, rename) == "use `NewName`\nnot `OldName` <!-- keep-old-name -->\n`NewName` again\n"
    assert rewrite_unmarked_lines("`OldName`", rename) == "`NewName`"
