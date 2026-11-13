"""이름 · 경로 바꾸기 도구(`move-source` · `rename-symbol` · `Scripts/dev/RenameIdentifier.py`)가 함께 쓰는 예외 규칙.

문서도 코드와 같이 바꾼다 — 문서를 건너뛰면 개명마다 규칙 문서(`docs/agents/`)의 이름 · 경로가 낡는다.
옛 이름을 일부러 적은 줄("쓰지 않는 것" 예, 이력)만 그 줄에 표지 `keep-old-name` 를 적어 남긴다(Markdown 은 `<!-- keep-old-name -->`).
린트 예외 표(`Config/Rules/Lint/*.json`)도 지금 이름을 가리키니 함께 고친다 — 옛 이름을 일부러 적는 두 곳만 지킨다(`rewriteRenameTarget`).
"""

from __future__ import annotations

import json
from typing import Callable

kKeepOldNameMarker = "keep-old-name"
kNamingWordsPath = "Config/Rules/Lint/NamingWords.json"     # `_bannedNames` 의 키가 옛 이름이다
kNamingBaselinePath = "Config/Rules/Lint/NamingBaseline.json"   # 남은 위반 이름 — `--lower-baseline` 만 줄인다(새 이름이 또 위반이면 린트가 지게)


def dumpNamingTable(data: dict) -> str:
    return json.dumps(data, ensure_ascii=False, indent=2) + "\n"


def rewriteRenameTarget(relativePath: str, text: str, rewrite: Callable[[str], str]) -> str:
    """
    이름 바꾸기 도구가 파일 하나(@p relativePath — 저장소 기준 posix)를 고친다. 두 도구(`rename-symbol` · `RenameIdentifier.py`)가 이 함수 하나를 쓴다 —
    한 도구는 린트 표를 통째로 건너뛰고 한 도구는 금지 이름 표의 키까지 바꿔, 개명마다 예외 표가 낡거나 금지 이름이 지워졌다.
    """
    if relativePath == kNamingBaselinePath:
        return text
    if relativePath == kNamingWordsPath:
        data = json.loads(text)
        mapBanned = data.get("_bannedNames", {})
        data["_bannedNames"] = {}
        rewritten = json.loads(rewriteUnmarkedLines(dumpNamingTable(data), rewrite))
        rewritten["_bannedNames"] = {old: rewriteUnmarkedLines(advice, rewrite) for old, advice in mapBanned.items()}   # 키(옛 이름)는 지키고 안내의 새 이름은 따라간다
        return dumpNamingTable(rewritten)
    return rewriteUnmarkedLines(text, rewrite)


def rewriteUnmarkedLines(text: str, rewrite: Callable[[str], str]) -> str:
    """@p text 의 줄 가운데 표지가 없는 줄에만 @p rewrite 를 적용한다(표지가 없는 글은 한 번에 바꾼다)."""
    if kKeepOldNameMarker not in text:
        return rewrite(text)
    return "".join(line if kKeepOldNameMarker in line else rewrite(line) for line in text.splitlines(keepends=True))


def checkRewriteUnmarkedLines() -> None:
    """표지 줄은 지키고 나머지 줄은 바꾸는지 본다."""
    rename = lambda part: part.replace("OldName", "NewName")  # noqa: E731
    text = "use `OldName`\nnot `OldName` <!-- keep-old-name -->\n`OldName` again\n"
    assert rewriteUnmarkedLines(text, rename) == "use `NewName`\nnot `OldName` <!-- keep-old-name -->\n`NewName` again\n"
    assert rewriteUnmarkedLines("`OldName`", rename) == "`NewName`"
    table = dumpNamingTable({"_bannedNames": {"OldName": "NewName — x"}, "_keep": {"OldName": "y"}})
    assert json.loads(rewriteRenameTarget(kNamingWordsPath, table, rename)) == {"_bannedNames": {"OldName": "NewName — x"}, "_keep": {"NewName": "y"}}
    assert rewriteRenameTarget(kNamingBaselinePath, "OldName", rename) == "OldName"
    assert rewriteRenameTarget("Config/Rules/Lint/ThreadOwners.json", "OldName", rename) == "NewName"
