"""
Scripts/lint/acronyms.py

약어 표기 검사 — 식별자 · 파일 · 폴더 이름의 낱말이 약어 목록(`Config/Rules/Lint/NamingWords.json` 의 `_acronyms`)의 약어를 첫 글자만 대문자로 쓰면(`Json` · `Id` · `FourCc`) 막는다.
규칙은 `docs/agents/NamingCpp.md` Acronyms: 약어는 어디서나 모두 대문자, 소문자로 시작하는 첫 낱말일 때만 모두 소문자(`jsonText`). `gv_` 이름 · 주석 · 문자열은 보지 않는다.

**왜 린트인가.** 약어 일괄 변경(1dafbae0c9) 뒤 도구를 지워 새 어긋남(`FourCcUtil` · `HpackDecoder` · `kDxgiFormat…`)이 다시 쌓였다(FolderMoves 11 절) — 이름은 형식이 막지 못한다.
바깥 이름(서드파티 · OS API 를 그대로 쓰는 자리)과 데이터 철자(직렬화 키)는 `_acronymKeep` 에 이름 하나씩 까닭과 함께 둔다.

규칙 id 는 따로 없다 — `naming-words` 가 이 판정을 부른다.
"""

from __future__ import annotations

import re

# 낱말: 약어 복수(`PSOs` · `IDs`)를 먼저, 그다음 대문자 덩어리 · 첫 대문자 낱말 · 소문자 낱말
WORD = re.compile(r"[A-Z]{2,}s(?![a-z])|[A-Z]+(?![a-z])|[A-Z][a-z0-9]*|[a-z0-9]+")


def find_bad_words(name: str, acronyms: set[str]) -> list[str]:
    """@p name 안에서 약어를 첫 글자만 대문자로 쓴 낱말들."""
    if name.startswith("gv_") or re.search(r"[a-z]", name) is None:
        return []
    list_bad = []
    for part in name.split("_"):
        for word in WORD.findall(part):
            letters = re.sub(r"\d+$", "", word)
            if len(letters) >= 2 and letters[0].isupper() and letters[1:].islower() and letters.upper() in acronyms:
                list_bad.append(word)
    return list_bad
