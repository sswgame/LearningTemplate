#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckBacklog.py

영역별 백로그(`docs/backlog/*.md`)의 항목 번호와 상태 태그, 그리고 저장소 곳곳에서 백로그를 가리키는 글이 맞는지 검사합니다.

코드 주석과 문서는 백로그 항목을 번호(`C-28`)로 가리키고, 번호는 다시 매기지 않습니다. 같은 번호가 두 파일에 생기면 그 번호를 쓴 글이
어느 항목을 뜻하는지 알 수 없게 됩니다. 항목을 다른 영역 파일로 옮기면 `docs/backlog/<영역>.md` 의 `ID` 처럼 파일을 함께 적은 글은 낡습니다.
목록 명령(`py -3 -m Scripts backlog`)도 상태 태그로 고르므로 태그가 틀리면 그 항목은 목록에서 빠집니다.

검사 규칙:

1. 항목 첫 줄은 ``- `ID` [상태] **제목.**`` 이고 상태는 `common.Backlog.kListBacklogStatus` 가운데 하나다.
2. 항목 번호는 모든 백로그 파일을 통틀어 하나뿐이다.
3. 저장소의 글(문서 · 코드 주석 · 스크립트)에 적힌 `docs/backlog/<이름>.md` 와 상대 링크 `backlog/<이름>.md` 는 있는 파일을 가리키고,
   `#앵커` 가 있으면 그 파일에 그 제목이 있다. 바로 뒤에 백틱 번호(`` `C-28` ``)가 오면 그 항목이 그 파일에 있다.
"""

from __future__ import annotations

import argparse
import re
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kNotOurDirNames  # noqa: E402
from common.Backlog import kBacklogRelDir, readBacklog  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

#: 백로그를 가리킬 수 있는 글 파일.
_kReferenceSuffix: tuple[str, ...] = (".md", ".h", ".cpp", ".inl", ".py", ".cmake", ".txt", ".json", ".hlsl", ".hlsli", ".yml")

#: `backlog/<이름>.md` + 앵커 + 바로 뒤의 백틱 번호(링크 닫는 괄호 · 백틱 · 조사 "의" 를 건너뛴다).
_kReferenceRe = re.compile(r"backlog/([A-Za-z0-9_\-]+\.md)(?:#([^)\s`]+))?(?:[)`]*\s*(?:의\s*)?`([A-Z]\d*-\d+)`)?")
#: 자가 테스트 조각(가짜 `backlog/a.md`)을 글로 싣는 폴더 — 가리키는 글로 보지 않는다.
_kFixtureRelDir: tuple[str, ...] = ("Scripts/lint/", "Test/PythonTest/")
_kHeadingRe = re.compile(r"^#{1,6}\s+(.*?)\s*#*\s*$")


def slugifyHeadingInternal(heading: str) -> str:
    """GitHub 의 제목 앵커 — 소문자, 글자 · 숫자 · `_` · `-` 만 남기고 공백은 `-`(`CheckDocPaths` 와 같은 규칙)."""
    text = re.sub(r"[`*~]", "", heading.strip().lower())
    return "".join("-" if ch == " " else ch for ch in text if ch in " -_" or unicodedata.category(ch)[0] in "LN")


class CheckBacklogGate(LintGate):
    """백로그 항목 번호가 하나뿐이고 상태 태그가 허용값이며, 백로그를 가리키는 글이 있는 파일 · 제목 · 항목을 가리키는지."""

    description = "영역별 백로그(docs/backlog)의 항목 번호 · 상태 태그와 백로그를 가리키는 글을 검사"
    buildComment = "Checking backlog item ids, status tags and references to backlog files..."
    timeoutSeconds = 30
    preCommitPattern = ("docs/backlog/*", "*.md")
    preCommitFileArgument = ""
    violationHeader = "백로그 위반"
    hint = ("  상태 태그와 항목 모양은 docs/backlog/README.md 를 봅니다. 번호를 옮겨 쓰지 말고 새 번호는 같은 문자의 가장 큰 번호 다음을 씁니다.\n"
            "  항목을 다른 파일로 옮겼으면 그 파일을 적은 글도 고칩니다(`py -3 -m Scripts backlog --id <번호>` 가 지금 파일을 알려 준다).")
    selfTestCases = [
        {
            "name": "두 파일에 같은 번호",
            "expectedViolation": "docs/backlog/b.md:3: `C-1` 번호가 docs/backlog/a.md:3 에도 있습니다",
            "files": {"docs/backlog/a.md": "# A\n\n- `C-1` [정리] **하나.**\n",
                      "docs/backlog/b.md": "# B\n\n- `C-1` [기능] **둘.**\n"},
        },
        {
            "name": "허용되지 않은 상태 태그",
            "expectedViolation": "docs/backlog/a.md:3: `C-1` 상태 '완료' 는 허용값(결함 · 검증대기 · 정리 · 기능 · 관찰 · 결정대기 · 조건부 · 보류)이 아닙니다",
            "files": {"docs/backlog/a.md": "# A\n\n- `C-1` [완료] **하나.**\n"},
        },
        {
            "name": "상태 태그가 없는 항목",
            "expectedViolation": "docs/backlog/a.md:3: `C-1` 첫 줄에 [상태] 태그가 없습니다",
            "files": {"docs/backlog/a.md": "# A\n\n- `C-1` **하나.**\n"},
        },
        {
            "name": "없는 백로그 파일을 가리키는 주석",
            "expectedViolation": "Source/Engine/X.h:1: 'backlog/gone.md' 가 없습니다",
            "files": {"docs/backlog/a.md": "# A\n\n- `C-1` [정리] **하나.**\n",
                      "Source/Engine/X.h": "// 남은 일은 docs/backlog/gone.md 에 있다\n"},
        },
        {
            "name": "다른 파일로 옮긴 항목을 옛 파일로 가리키는 글",
            "expectedViolation": "docs/Guide.md:3: `D3-1` 는 backlog/a.md 이 아니라 backlog/b.md 에 있습니다",
            "files": {"docs/backlog/a.md": "# A\n\n- `C-1` [정리] **하나.**\n",
                      "docs/backlog/b.md": "# B\n\n- `D3-1` [기능] **둘.**\n",
                      "docs/Guide.md": "# 안내\n\n남은 일은 [백로그](backlog/a.md) `D3-1` 입니다.\n"},
        },
    ]

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listItem, listViolation = readBacklog(repositoryRoot)

        mapFirst: dict[str, str] = {}
        for item in listItem:
            where = f"{kBacklogRelDir}/{item.area}.md:{item.lineNumber}"
            if item.itemId in mapFirst:
                listViolation.append(f"{where}: `{item.itemId}` 번호가 {mapFirst[item.itemId]} 에도 있습니다")
            else:
                mapFirst[item.itemId] = where

        backlogDir = repositoryRoot / kBacklogRelDir
        mapAreaById = {item.itemId: f"{item.area}.md" for item in listItem}
        mapAnchor: dict[str, set[str]] = {}
        for path in backlogDir.glob("*.md"):
            listHeading = [match.group(1) for line in path.read_text(encoding="utf-8").splitlines() if (match := _kHeadingRe.match(line))]
            mapAnchor[path.name] = {slugifyHeadingInternal(heading) for heading in listHeading}

        listPath = self.selectTargetFiles(repositoryRoot, None, suffixes=_kReferenceSuffix, excludedDirNames=kNotOurDirNames | {"ThirdParty"})
        listPath = [path for path in listPath if not path.relative_to(repositoryRoot).as_posix().startswith(_kFixtureRelDir)]
        listReferencing = self.readFiles(listPath, mustContain="backlog/")
        countReference = 0
        for path, text in listReferencing:
            relPath = path.relative_to(repositoryRoot).as_posix()
            for lineNumber, line in enumerate(text.splitlines(), start=1):
                for match in _kReferenceRe.finditer(line):
                    fileName, anchor, itemId = match.groups()
                    countReference += 1
                    where = f"{relPath}:{lineNumber}"
                    if fileName not in mapAnchor:
                        listViolation.append(f"{where}: 'backlog/{fileName}' 가 없습니다 — 있는 파일: {' · '.join(sorted(mapAnchor))}")
                        continue
                    if anchor and anchor.lower() not in mapAnchor[fileName]:
                        listViolation.append(f"{where}: 'backlog/{fileName}#{anchor}' 의 제목이 그 파일에 없습니다")
                    if itemId and mapAreaById.get(itemId, fileName) != fileName:
                        listViolation.append(f"{where}: `{itemId}` 는 backlog/{fileName} 이 아니라 backlog/{mapAreaById[itemId]} 에 있습니다")

        return GateResult(listViolation=listViolation,
                          summary=f"항목 {len(listItem)}개, 백로그를 가리키는 글 {countReference}곳({len(listReferencing)} 파일)")


main = CheckBacklogGate.run


if __name__ == "__main__":
    sys.exit(main())
