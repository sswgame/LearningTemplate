"""
C++ · HLSL 소스 글을 읽는 도구입니다(게이트 공용) — 주석 · 리터럴 가리기, 오프셋 → 줄 번호, include 줄 훑기, include 경로를 저장소 파일로 풀기.

지운 자리는 **같은 길이의 공백**으로 채우고 줄바꿈은 남긴다 — 줄 번호와 오프셋이 원문과 맞아야 위반을 원문 줄로 알리고, 중괄호를 세어 본문을 자를 수 있다.
한 번의 정규식 훑기로 주석과 리터럴을 함께 가린다. 그래서 문자열 안의 `//` 를 주석으로, 주석 안의 `"` 를 문자열로 잘못 읽지 않는다.
"""
from __future__ import annotations

import os
import posixpath
import re
from pathlib import Path
from typing import Iterator

#: 주석 · 문자열 · 문자 리터럴. 앞의 두 갈래가 주석이다.
_kCommentOrLiteralRe = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|'(?:\\.|[^'\\\n])*'", re.DOTALL)

#: 줄 머리의 `#include <…>` · `#include "…"` — 무리 1 이 경로다. 줄 하나에 `match` 한다.
kIncludeLineRe = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
#: 따옴표 include 만(`#include "…"`). 꺾쇠는 시스템 · 서드파티 헤더라 저장소 파일로 풀지 않는 게이트가 쓴다.
kQuotedIncludeLineRe = re.compile(r'^\s*#\s*include\s*"([^"]+)"')


def blankText(text: str) -> str:
    """줄바꿈을 남기고 나머지를 공백으로 바꿉니다. 주석 · 리터럴마다 불리므로 정규식 대신 줄 단위로 길이만 센다."""
    return "\n".join(" " * len(part) for part in text.split("\n"))


def blankMatch(match: re.Match) -> str:
    """`re.sub` 의 바꿀 함수 — 맞은 글을 같은 길이의 공백으로 바꿉니다(줄바꿈은 남긴다)."""
    return blankText(match.group(0))


def blankCommentsAndLiterals(text: str) -> str:
    """주석 · 문자열 · 문자 리터럴을 같은 길이의 공백으로 바꿉니다(줄바꿈은 남긴다)."""
    return _kCommentOrLiteralRe.sub(blankMatch, text)


def blankComments(text: str) -> str:
    """주석만 같은 길이의 공백으로 바꿉니다. 문자열 · 문자 리터럴은 그대로 둡니다(그 안의 `//` · `/*` 는 주석이 아니다)."""
    def replace(match: re.Match) -> str:
        found = match.group(0)
        return blankText(found) if found.startswith(("//", "/*")) else found

    return _kCommentOrLiteralRe.sub(replace, text)


def lineOf(text: str, offset: int) -> int:
    """글 안의 오프셋 → 1 부터 세는 줄 번호입니다."""
    return text.count("\n", 0, offset) + 1


def iterIncludes(text: str, *, bQuotedOnly: bool = False) -> Iterator[tuple[int, str]]:
    """
    줄 머리의 include 를 (줄 번호, 적힌 경로) 로 차례로 내줍니다. 경로는 적힌 그대로다 — 비교하려면 `normalizePath` 를 거친다.

    게이트마다 include 정규식을 따로 두면 공백 · 꺾쇠 허용이 갈려 같은 줄을 한 게이트는 보고 다른 게이트는 못 본다.
    `#` 가 없는 줄에는 정규식을 돌리지 않는다.
    """
    pattern = kQuotedIncludeLineRe if bQuotedOnly else kIncludeLineRe
    for lineNumber, line in enumerate(text.splitlines(), start=1):
        if "#" not in line:
            continue
        match = pattern.match(line)
        if match is not None:
            yield lineNumber, match.group(1)


def firstFolderAfter(path: str, prefix: str) -> str:
    """
    `prefix` 뒤의 첫 폴더 이름입니다 — 폴더가 곧 층인 게이트(Core · Core/Network · Engine)가 "이 파일은 어느 층인가" 를 묻는다.
    `prefix` 바로 아래 파일이면 빈 이름. `path` 가 `prefix` 로 시작하는지는 부르는 쪽이 본다.
    """
    listPart = path[len(prefix):].split("/")
    return listPart[0] if len(listPart) > 1 else ""


class IncludeResolver:
    """
    따옴표 include 를 저장소 파일(저장소 기준 POSIX 경로)로 풉니다 — 먼저 include 한 파일의 폴더 기준, 없으면 `searchRoot` 기준(컴파일러의 `-I Source`).

    include 마다 파일 시스템에 묻지 않는다(`is_file` · `resolve` 를 include 수만큼 부르면 트리 전체가 십수 초다). `searchRoot` 아래 파일을
    한 번 걸어 표로 들고, 그 밖으로 나가는 경로(`../` 로 저장소 위로)만 파일 시스템에 묻는다. Windows 에서는 대소문자를 가리지 않고 찾아
    실제 철자를 돌려준다 — 파일 시스템이 그렇게 답한다.
    """

    def __init__(self, repositoryRoot: Path, searchRoot: str = "Source") -> None:
        self._repositoryRoot = repositoryRoot.resolve()
        self._searchRoot = searchRoot.rstrip("/")
        self._bCaseInsensitive = os.name == "nt"
        self._mapFile: dict[str, str] = {}
        for directory, _, listFileName in os.walk(self._repositoryRoot / self._searchRoot):
            relativeDir = Path(directory).relative_to(self._repositoryRoot).as_posix()
            for fileName in listFileName:
                relative = f"{relativeDir}/{fileName}"
                self._mapFile[self.keyOfInternal(relative)] = relative

    def keyOfInternal(self, relative: str) -> str:
        return relative.lower() if self._bCaseInsensitive else relative

    def findFile(self, relative: str) -> str | None:
        """저장소 기준 경로(`..` 가 섞여도 된다) → 실제 파일의 저장소 기준 경로입니다. 없으면 None."""
        normalized = posixpath.normpath(relative)
        if normalized.startswith(self._searchRoot + "/"):
            return self._mapFile.get(self.keyOfInternal(normalized))
        candidate = self._repositoryRoot / relative
        if candidate.is_file() is False:
            return None
        try:
            return candidate.resolve().relative_to(self._repositoryRoot).as_posix()
        except ValueError:
            return None

    def resolveInclude(self, includingRelative: str, includePath: str) -> str | None:
        """`includingRelative` 파일 안의 `#include "includePath"` 가 가리키는 저장소 파일입니다. 못 찾으면 None."""
        found = self.findFile(f"{posixpath.dirname(includingRelative)}/{includePath}")
        if found is None:
            found = self.findFile(f"{self._searchRoot}/{includePath}")
        return found


__all__ = ["IncludeResolver", "blankComments", "blankCommentsAndLiterals", "blankMatch", "blankText", "firstFolderAfter", "iterIncludes",
           "kIncludeLineRe", "kQuotedIncludeLineRe", "lineOf"]
