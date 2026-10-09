#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckDocPaths.py

문서(`*.md`)가 가리키는 저장소 안의 자리가 **실제로 있는지** 검사합니다.

문서는 코드와 같이 빌드되지 않아, 파일을 옮기거나 지워도 문서의 경로는 아무도 모르게 낡습니다. 이 게이트는
문서가 하는 말 가운데 기계가 확인할 수 있는 것 — 경로 — 만 봅니다. 함수 · 클래스 이름은 보지 않습니다(설명 속
예시 이름 · 다른 엔진 이름과 가를 수 없다).

검사 규칙:

1. 상대 링크 `[글](경로#앵커)` — 문서 폴더 기준으로 그 파일 · 폴더가 있는가. 앵커가 있으면 대상 `.md` 에 그 제목이 있는가
   (GitHub 제목 슬러그). `http:` · `https:` · `mailto:` 는 보지 않는다.
2. 백틱 안의 저장소 경로 — 첫 조각이 저장소 최상위 폴더(`Source/` · `Test/` · `Scripts/` …)인 경로, 또는 첫 조각이 그 문서
   폴더의 하위 폴더인 경로. 확장자를 뗀 줄기(`Graphics/RHI/IRenderSurface`)는 같은 이름의 파일이 하나라도 있으면 맞다.
3. 코드 블록 안의 `#include "Engine/…"` 와 `py -3 Scripts/….py`.
4. 모든 `README.md`(최상위 제외)와 `docs/*.md` 가 문서 지도(`docs/02_DocumentMap.md`)에서 링크되는가 — 지도에 없는 문서는
   아무도 찾지 못한다.

`.gitignore` 가 무시하는 자리(내려받은 도구 · 생성 파일 — `Config/Environment/toolchain_config.json`)와 설정 목록
(`Scripts/common/ConfigCatalog.py`)이 `bOptional` 로 적은 설정 파일은 없어도 맞다.
`<` · `*` · `{` · `…` · `$` 가 든 것은 모양이지 경로가 아니라 보지 않는다.
`docs/06_Backlog.md` 와 `docs/plans/` 의 계획 문서는 규칙 1 만 본다 — 할 일 목록과 계획은 아직 없는 파일 · 옛 경로를 적는 자리다.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import unicodedata
from pathlib import Path
from urllib.parse import unquote

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import kNotOurDirNames, runProcess  # noqa: E402
from common.ConfigCatalog import kListConfigFile  # noqa: E402
from LintGate import GateResult, LintGate  # noqa: E402

#: 경로로 읽는 최상위 폴더.
_kRootFolderName: frozenset[str] = frozenset(
    {"Source", "Test", "Tools", "Scripts", "cmake", "Config", "Resource", "docs", "ThirdParty", ".github"}
)

#: `#include "X/…"` 에서 Source 아래로 푸는 첫 조각.
_kSourceFolderName: frozenset[str] = frozenset({"Core", "Engine", "Editor", "GameFramework", "Games", "RuntimeAPI", "App", "ModuleHost", "Server"})

#: 줄기만 적은 경로(`Graphics/RHI/IRenderSurface`)를 풀 때 붙여 보는 확장자.
_kStemExtension: tuple[str, ...] = (".h", ".cpp", ".xxx", ".inl", ".py", ".md", ".cmake", ".json", ".xml", ".hlsl", ".hlsli")

#: 규칙 1 만 보는 문서.
_kLinkOnlyDocument: frozenset[str] = frozenset({"docs/06_Backlog.md"})

#: 규칙 1 만 보는 폴더 — 다음 세션 계획(`docs/plans/`)은 만들 파일을 적는다.
_kLinkOnlyFolder: tuple[str, ...] = ("docs/plans/",)

#: 없어도 맞는 설정 파일 — 설정 목록(`ConfigCatalog`)이 `bOptional` 로 적은 줄(기본값과 다른 값이 있을 때만 만드는 파일 등).
#: 목록이 정본이라 여기에 따로 적지 않는다.
_kOptionalConfigPath: frozenset[str] = frozenset(
    entry.pathPattern for entry in kListConfigFile if entry.bOptional and "*" not in entry.pathPattern
)

#: 모든 README 와 `docs/*.md` 를 링크하는 문서 지도(규칙 4). 없으면 규칙 4 를 건너뛴다(자가 검사 조각의 작은 저장소).
_kDocumentMapPath = "docs/02_DocumentMap.md"

_kLinkRe = re.compile(r"\]\(([^)\s]+)\)")
_kSpanRe = re.compile(r"`([^`\n]+)`")
_kPathTokenRe = re.compile(r"[A-Za-z0-9_.\-/]+")
_kShapeCharRe = re.compile(r"[<>*{}…$%|]|\.\.\.")
_kIncludeRe = re.compile(r'#include\s+"([^"]+)"')
_kPythonScriptRe = re.compile(r"py -3 (?:-m )?(Scripts/[A-Za-z0-9_./\-]+\.py)")
_kHeadingRe = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")


def slugifyHeadingInternal(heading: str) -> str:
    """GitHub 의 제목 앵커 — 소문자, 글자 · 숫자 · `_` · `-` 만 남기고 공백은 `-`."""
    text = re.sub(r"[`*~]", "", heading.strip().lower())
    listChar: list[str] = []
    for ch in text:
        if ch == " ":
            listChar.append("-")
        elif ch in "-_" or unicodedata.category(ch)[0] in "LN":
            listChar.append(ch)
    return "".join(listChar)


def collectAnchorInternal(text: str) -> set[str]:
    """문서의 제목 앵커 전부(같은 제목은 `-1` · `-2` …)."""
    setAnchor: set[str] = set()
    mapCount: dict[str, int] = {}
    bInFence = False
    for line in text.splitlines():
        if line.lstrip().startswith("```"):
            bInFence = not bInFence
            continue
        if bInFence:
            continue
        match = _kHeadingRe.match(line)
        if match is None:
            continue
        slug = slugifyHeadingInternal(match.group(2))
        count = mapCount.get(slug, 0)
        mapCount[slug] = count + 1
        setAnchor.add(slug if count == 0 else f"{slug}-{count}")
    return setAnchor


class CheckDocPathsGate(LintGate):
    """문서가 없는 파일 · 폴더 · 제목을 가리키고 있지 않은지."""

    description = "문서(.md)의 상대 링크 · 저장소 경로가 실재하는지 검사"
    buildComment = "Checking that paths and links in the docs still exist..."
    timeoutSeconds = 30
    preCommitPattern = ("*.md",)
    preCommitFileArgument = "--files"
    violationHeader = "문서가 없는 자리를 가리킵니다"
    hint = ("  파일을 옮겼으면 문서의 경로를 새 자리로, 지웠으면 그 문장을 지우세요. 아직 없는 파일을 적는 것은 백로그와 계획 문서(docs/plans/)의 일입니다.\n"
            "  파일을 옮기거나 지운 커밋은 문서를 건드리지 않아 훅이 이 게이트를 돌리지 않습니다 — `ctest -L lint` 가 전체를 봅니다.")
    selfTestCases = [
        {
            "name": "없는 파일로 가는 상대 링크",
            "files": {"docs/Guide.md": "# 안내\n\n[엔진](../Source/Engine/README.md)\n"},
        },
        {
            "name": "없는 제목으로 가는 앵커",
            "files": {
                "docs/Guide.md": "# 안내\n\n[절](Other.md#없는-절)\n",
                "docs/Other.md": "# 다른 문서\n\n## 있는 절\n",
            },
        },
        {
            "name": "백틱 안의 없는 저장소 경로",
            "files": {"README.md": "# 저장소\n\n정본은 `Source/Engine/Missing/Thing.h` 다.\n",
                      "Source/Engine/Real.h": "#pragma once\n"},
        },
        {
            "name": "문서 폴더 기준의 없는 하위 경로",
            "files": {"Source/Engine/README.md": "# 엔진\n\n`Common/Gone` 을 본다.\n",
                      "Source/Engine/Common/Here.h": "#pragma once\n"},
        },
        {
            "name": "문서 지도에 없는 README",
            "files": {"docs/02_DocumentMap.md": "# 문서 지도\n\n- [엔진](../Source/Engine/README.md)\n",
                      "Source/Engine/README.md": "# 엔진\n",
                      "Source/Core/README.md": "# 코어\n"},
        },
        {
            "name": "코드 블록 안의 없는 include",
            "files": {"README.md": "# 저장소\n\n```cpp\n#include \"Engine/Nowhere/Gone.h\"\n```\n",
                      "Source/Engine/Real.h": "#pragma once\n"},
        },
    ]

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        self.addFilesArgument(parser, "검사할 문서 (생략 시 저장소의 .md 전부)")

    def scan(self, repositoryRoot: Path, args: argparse.Namespace) -> GateResult:
        listDocument = self.selectTargetFiles(repositoryRoot, args.files, suffixes=(".md",),
                                              excludedDirNames=kNotOurDirNames | {"ThirdParty"})
        mapAnchor: dict[Path, set[str]] = {}
        mapChildDir: dict[Path, set[str]] = {}
        listPending: list[tuple[str, str, Path]] = []   # (위치, 적힌 글, 풀어 본 경로) — 없어서 무시 여부를 물을 것
        listViolation: list[str] = []

        def existsInternal(path: Path, bAllowStem: bool) -> bool:
            if path.exists():
                return True
            if bAllowStem and path.suffix == "":
                return any(path.with_name(path.name + extension).exists() for extension in _kStemExtension)
            return False

        def anchorsOfInternal(path: Path) -> set[str]:
            if path not in mapAnchor:
                mapAnchor[path] = collectAnchorInternal(path.read_text(encoding="utf-8", errors="replace"))
            return mapAnchor[path]

        for documentPath in listDocument:
            relDocument = documentPath.relative_to(repositoryRoot).as_posix()
            documentDir = documentPath.parent
            bLinkOnly = relDocument in _kLinkOnlyDocument or relDocument.startswith(_kLinkOnlyFolder)
            listBaseDir = [documentDir, *[parent for parent in documentDir.parents if repositoryRoot in (parent, *parent.parents) and parent != repositoryRoot]]
            for baseDir in listBaseDir:
                if baseDir not in mapChildDir:
                    mapChildDir[baseDir] = {child.name for child in baseDir.iterdir() if child.is_dir()}
            bInFence = False
            text = documentPath.read_text(encoding="utf-8", errors="replace")

            for lineNumber, line in enumerate(text.splitlines(), start=1):
                where = f"{relDocument}:{lineNumber}"
                if line.lstrip().startswith("```"):
                    bInFence = not bInFence
                    continue

                if bInFence:
                    if bLinkOnly:
                        continue
                    if match := _kIncludeRe.search(line):
                        include = match.group(1)
                        if include.split("/", 1)[0] in _kSourceFolderName and _kShapeCharRe.search(include) is None:
                            target = repositoryRoot / "Source" / include
                            if existsInternal(target, False) is False:
                                listPending.append((where, include, target))
                    if match := _kPythonScriptRe.search(line):
                        target = repositoryRoot / match.group(1)
                        if existsInternal(target, False) is False:
                            listPending.append((where, match.group(1), target))
                    continue

                # 규칙 1 — 상대 링크.
                for match in _kLinkRe.finditer(line):
                    linkTarget = match.group(1)
                    if re.match(r"^(https?|mailto):", linkTarget):
                        continue
                    pathPart, _, anchor = linkTarget.partition("#")
                    target = documentPath if pathPart == "" else Path(os.path.normpath(documentDir / unquote(pathPart)))
                    if pathPart and existsInternal(target, False) is False:
                        listPending.append((where, linkTarget, target))
                        continue
                    if anchor and target.suffix.lower() == ".md" and target.is_file():
                        if unquote(anchor).lower() not in anchorsOfInternal(target):
                            listViolation.append(f"{where} 링크 '{linkTarget}' 의 제목 '#{anchor}' 이 {target.name} 에 없습니다")

                if bLinkOnly:
                    continue

                # 규칙 2 — 백틱 안의 경로.
                for span in _kSpanRe.findall(line):
                    token = span.strip().rstrip("/")
                    token = re.sub(r":\d+(-\d+)?$", "", token)
                    if "/" not in token or "/." in token or _kPathTokenRe.fullmatch(token) is None or _kShapeCharRe.search(token):
                        continue
                    firstPart = token.split("/", 1)[0]
                    # 모듈 문서는 자기 모듈 기준 경로를 쓴다(`Source/Engine/Animation/README.md` 의 `Resource/AnimationAssetCache` 는
                    # `Source/Engine/Resource/…`). 문서 폴더와 그 위 폴더들 가운데 첫 조각을 하위 폴더로 가진 곳, 그리고 저장소 최상위 —
                    # 어느 하나에 있으면 맞다.
                    listTarget = [baseDir / token for baseDir in listBaseDir if firstPart in mapChildDir[baseDir]]
                    if firstPart in _kRootFolderName:
                        listTarget.append(repositoryRoot / token)
                    if listTarget and all(existsInternal(target, True) is False for target in listTarget):
                        listPending.append((where, token, listTarget[-1]))

        # 규칙 4 — 문서 지도에 없는 README · docs 문서.
        mapPath = repositoryRoot / _kDocumentMapPath
        if mapPath.is_file():
            mapText = mapPath.read_text(encoding="utf-8", errors="replace")
            setLinked = {Path(os.path.normpath(mapPath.parent / unquote(target.partition("#")[0])))
                         for target in _kLinkRe.findall(mapText) if re.match(r"^(https?|mailto):", target) is None}
            for documentPath in listDocument:
                relDocument = documentPath.relative_to(repositoryRoot).as_posix()
                bListed = documentPath.name == "README.md" or (relDocument.startswith("docs/") and relDocument.count("/") == 1)
                if bListed is False or relDocument == "README.md":
                    continue
                if Path(os.path.normpath(documentPath)) not in setLinked:
                    listViolation.append(f"{relDocument} 가 문서 지도({_kDocumentMapPath})에 없습니다 — 목록에 한 줄을 더하세요")

        # 없는 것 가운데 `.gitignore` 가 무시하는 자리(내려받은 도구 · 생성 파일)는 맞다.
        setIgnored: set[str] = set()
        if listPending:
            listRelative = []
            for _, _, target in listPending:
                # resolve() 는 쓰지 않는다 — 워크트리의 정션(`Tools/`)을 따라가 저장소 밖 경로가 된다.
                relative = Path(os.path.relpath(target, repositoryRoot)).as_posix()
                listRelative.append("" if relative.startswith("..") else relative)
            # `-z` 로 NUL 로 가른다 — 줄바꿈으로 가르면 Windows 글 모드의 표준 입력이 줄 끝을 CRLF 로 바꿔 경로에 `\r` 이 붙는다.
            completed = runProcess(["git", "-C", str(repositoryRoot), "check-ignore", "--no-index", "--stdin", "-z"],
                                   stdinText="\0".join(query for path in listRelative if path for query in (path, path + "/_")) + "\0")
            setIgnored = {path for path in completed.stdout.split("\0") if path}
            for (where, written, _), relative in zip(listPending, listRelative):
                if relative and (relative in setIgnored or relative + "/_" in setIgnored):   # `/_` — 아직 없는 폴더도 `Layouts/` 꼴 규칙에 걸리게
                    continue
                if relative in _kOptionalConfigPath:
                    continue
                listViolation.append(f"{where} '{written}' 가 없습니다")

        return GateResult(listViolation=listViolation, summary=f"{len(listDocument)} documents")


main = CheckDocPathsGate.run


if __name__ == "__main__":
    sys.exit(main())
