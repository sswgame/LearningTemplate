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
5. 규칙 문서(`CLAUDE.md` · `AGENTS.md` · `docs/NN_*.md` · `docs/agents/*.md` · `docs/plans/` 의 `NEXT.md` · `Roadmap.md` ·
   `WorkerRules.md`)는 맨 위 15 줄 안에 `> 질문:` · `> 독자:` · `> 결론:` 세 줄이 있는가 — 문서 하나가 어떤 질문에 답하는지
   첫 화면에서 보이게 한다(`docs/10_WritingDocs.md` 0절).
6. 규칙 문서(`docs/09_Decisions.md` 는 기록이라 뺀다) 둘이 한글 문장을 그대로 복사해 겹치는가 — 한글이 70 % 넘는 24 글자
   조각(공백 · 부호를 뺀 글)이 12 개 넘게 겹치면 같은 규칙이 두 문서에 문장으로 있는 것이다. 한 사실은 한 문서에만 쓰고
   다른 문서는 링크한다. 말만 바꾼 중복은 기계가 못 잡는다.

규칙 5 · 6 은 문서 구조를 형식으로 막을 방법이 없어(문서는 자유 글이다) 게이트로 둔다. 규칙 6 은 커밋 훅에서도 규칙 문서 전체를
디스크에서 읽어 견준다.

`.gitignore` 가 무시하는 자리(내려받은 도구 · 생성 파일 — `Config/Environment/toolchain_config.json`)와 설정 목록
(`Scripts/common/ConfigCatalog.py`)이 `bOptional` 로 적은 설정 파일은 없어도 맞다.
`<` · `*` · `{` · `…` · `$` 가 든 것은 모양이지 경로가 아니라 보지 않는다.
`docs/plans/` 의 계획 문서는 규칙 1 만 본다 — 계획은 아직 없는 파일 · 옛 경로를 적는 자리다.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path
from urllib.parse import unquote

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.Constants import kNotOurDirNames  # noqa: E402
from common.Process import runProcess  # noqa: E402
from common.ConfigCatalog import kListConfigFile  # noqa: E402
from common.MarkdownAnchor import collectAnchors  # noqa: E402
from lint.LintGate import GateError, GateResult, LintGate  # noqa: E402

#: 경로로 읽는 최상위 폴더.
_kRootFolderName: frozenset[str] = frozenset(
    {"Source", "Test", "Tools", "Scripts", "cmake", "Config", "Resource", "docs", "ThirdParty", ".github"}
)

#: `#include "X/…"` 에서 Source 아래로 푸는 첫 조각.
_kSourceFolderName: frozenset[str] = frozenset({"Core", "Engine", "Editor", "GameFramework", "Games", "RuntimeAPI", "App", "ModuleHost", "Server"})

#: 줄기만 적은 경로(`Graphics/RHI/IRenderSurface`)를 풀 때 붙여 보는 확장자.
_kStemExtension: tuple[str, ...] = (".h", ".cpp", ".xxx", ".inl", ".py", ".md", ".cmake", ".json", ".xml", ".hlsl", ".hlsli")

#: 규칙 1 만 보는 폴더 — 계획 문서(`docs/plans/`)는 만들 파일을 적는다.
_kLinkOnlyFolder: tuple[str, ...] = ("docs/plans/",)

#: 없어도 맞는 설정 파일 — 설정 목록(`ConfigCatalog`)이 `bOptional` 로 적은 줄(기본값과 다른 값이 있을 때만 만드는 파일 등).
#: 목록이 원본이라 여기에 따로 적지 않는다.
_kOptionalConfigPath: frozenset[str] = frozenset(
    entry.pathPattern for entry in kListConfigFile if entry.bOptional and "*" not in entry.pathPattern
)

#: 모든 README 와 `docs/*.md` 를 링크하는 문서 지도(규칙 4). 없으면 규칙 4 를 건너뛴다(자가 검사 조각의 작은 저장소).
_kDocumentMapPath = "docs/02_DocumentMap.md"

#: 규칙 5 · 6 의 대상 — 규칙 문서. 정규식은 저장소 기준 상대 경로에 맞춘다.
_kRuleDocRe = re.compile(
    r"^(CLAUDE\.md|AGENTS\.md|docs/\d\d_[^/]+\.md|docs/agents/[^/]+\.md|docs/plans/(NEXT|Roadmap|WorkerRules)\.md)$"
)
#: 규칙 6 에서 뺄 기록 문서(같은 문장을 되풀이해도 기록이다).
_kDuplicateExempt: frozenset[str] = frozenset({"docs/09_Decisions.md"})
_kHeaderRequired: tuple[str, ...] = ("> 질문:", "> 독자:", "> 결론:")
_kHeaderScanLines = 15
_kShingleLength = 24
_kShingleHangulRatio = 0.7
_kDuplicateLimit = 12   # 겹치는 조각이 이 수를 넘으면 위반(24 글자 조각 12 개 = 이어진 약 35 글자)

_kFenceBlockRe = re.compile(r"```.*?```", re.S)
_kNormalizeRe = re.compile(r"[\s`*|\-\[\]()·,.:;]+")
_kHangulRe = re.compile(r"[가-힣]")

_kLinkRe = re.compile(r"\]\(([^)\s]+)\)")
_kSpanRe = re.compile(r"`([^`\n]+)`")
_kPathTokenRe = re.compile(r"[A-Za-z0-9_.\-/]+")
_kShapeCharRe = re.compile(r"[<>*{}…$%|]|\.\.\.")
_kIncludeRe = re.compile(r'#include\s+"([^"]+)"')
_kPythonScriptRe = re.compile(r"py -3 (?:-m )?(Scripts/[A-Za-z0-9_./\-]+\.py)")


def collectHangulShingles(text: str) -> set[str]:
    """코드 블록과 공백 · 부호를 뺀 글에서 한글이 70 % 넘는 24 글자 조각을 모은다(명령 · 식별자 목록의 우연한 겹침을 거른다)."""
    plain = _kNormalizeRe.sub("", _kFenceBlockRe.sub("", text))
    setShingle: set[str] = set()
    for index in range(0, len(plain) - _kShingleLength + 1):
        shingle = plain[index:index + _kShingleLength]
        if len(_kHangulRe.findall(shingle)) >= _kShingleLength * _kShingleHangulRatio:
            setShingle.add(shingle)
    return setShingle


class CheckDocPathsGate(LintGate):
    """문서가 없는 파일 · 폴더 · 제목을 가리키고 있지 않은지."""

    description = "문서(.md)의 상대 링크 · 저장소 경로가 실재하는지 검사"
    buildComment = "Checking that paths and links in the docs still exist..."
    timeoutSeconds = 30
    preCommitPattern = ("*.md",)
    preCommitFileArgument = "--files"
    violationHeader = "문서가 없는 자리를 가리킵니다"
    hint = ("  파일을 옮겼으면 문서의 경로를 새 자리로, 지웠으면 그 문장을 지우세요. 아직 없는 파일을 적는 것은 계획 문서(docs/plans/)의 일입니다.\n"
            "  파일을 옮기거나 지운 커밋은 문서를 건드리지 않아 훅이 이 게이트를 돌리지 않습니다 — `ctest -L lint` 가 전체를 봅니다.")
    # 없는 자리를 `git check-ignore` 로 묻는 게이트라 조각마다 임시 트리를 git 저장소로 만든다(`git`).
    selfTestCases = [
        {
            "name": "없는 파일로 가는 상대 링크",
            "expectedViolation": "docs/Guide.md:3 '../Source/Engine/README.md' 가 없습니다",
            "git": True,
            "files": {"docs/Guide.md": "# 안내\n\n[엔진](../Source/Engine/README.md)\n"},
        },
        {
            "name": "없는 제목으로 가는 앵커",
            "expectedViolation": "docs/Guide.md:3 링크 'Other.md#없는-절' 의 제목 '#없는-절' 이 Other.md 에 없습니다",
            "git": True,
            "files": {
                "docs/Guide.md": "# 안내\n\n[절](Other.md#없는-절)\n",
                "docs/Other.md": "# 다른 문서\n\n## 있는 절\n",
            },
        },
        {
            "name": "백틱 안의 없는 저장소 경로",
            "expectedViolation": "README.md:3 'Source/Engine/Missing/Thing.h' 가 없습니다",
            "git": True,
            "files": {"README.md": "# 저장소\n\n정본은 `Source/Engine/Missing/Thing.h` 다.\n",
                      "Source/Engine/Real.h": "#pragma once\n"},
        },
        {
            "name": "문서 폴더 기준의 없는 하위 경로",
            "expectedViolation": "Source/Engine/README.md:3 'Common/Gone' 가 없습니다",
            "git": True,
            "files": {"Source/Engine/README.md": "# 엔진\n\n`Common/Gone` 을 본다.\n",
                      "Source/Engine/Common/Here.h": "#pragma once\n"},
        },
        {
            "name": "문서 지도에 없는 README",
            "expectedViolation": "Source/Core/README.md 가 문서 지도(docs/02_DocumentMap.md)에 없습니다",
            "git": True,
            "files": {"docs/02_DocumentMap.md": "# 문서 지도\n\n- [엔진](../Source/Engine/README.md)\n",
                      "Source/Engine/README.md": "# 엔진\n",
                      "Source/Core/README.md": "# 코어\n"},
        },
        {
            "name": "규칙 문서 맨 위에 질문 · 독자 · 결론이 없음",
            "expectedViolation": "CLAUDE.md 맨 위 15 줄에 > 질문: · > 독자: · > 결론: 줄이 없습니다",
            "git": True,
            "files": {"CLAUDE.md": "# 안내\n\n본문만 있다.\n"},
        },
        {
            "name": "두 규칙 문서에 같은 한글 문장 복사",
            "expectedViolation": "docs/11_One.md 와 docs/12_Two.md 에 같은 한글 문장이 겹칩니다",
            "git": True,
            "files": {
                "docs/11_One.md": "# 하나\n\n> 질문: a\n> 독자: b\n> 결론: c\n\n작업자는 자기 워크트리 안에서만 일하고 빌드와 테스트와 병합과 푸시를 하지 않으며 코드를 짜고 린트한 뒤 커밋하고 멈춘다.\n",
                "docs/12_Two.md": "# 둘\n\n> 질문: a\n> 독자: b\n> 결론: c\n\n작업자는 자기 워크트리 안에서만 일하고 빌드와 테스트와 병합과 푸시를 하지 않으며 코드를 짜고 린트한 뒤 커밋하고 멈춘다.\n",
            },
        },
        {
            "name": "코드 블록 안의 없는 include",
            "expectedViolation": "README.md:4 'Engine/Nowhere/Gone.h' 가 없습니다",
            "git": True,
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
                mapAnchor[path] = collectAnchors(path.read_text(encoding="utf-8", errors="replace"))
            return mapAnchor[path]

        for documentPath in listDocument:
            relDocument = documentPath.relative_to(repositoryRoot).as_posix()
            documentDir = documentPath.parent
            bLinkOnly = relDocument.startswith(_kLinkOnlyFolder)
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

        # 규칙 5 — 규칙 문서 맨 위의 질문 · 독자 · 결론.
        listRuleDoc = [documentPath for documentPath in listDocument
                       if _kRuleDocRe.match(documentPath.relative_to(repositoryRoot).as_posix())]
        for documentPath in listRuleDoc:
            relDocument = documentPath.relative_to(repositoryRoot).as_posix()
            head = "\n".join(documentPath.read_text(encoding="utf-8", errors="replace").splitlines()[:_kHeaderScanLines])
            listMissing = [marker for marker in _kHeaderRequired if marker not in head]
            if listMissing:
                listViolation.append(f"{relDocument} 맨 위 {_kHeaderScanLines} 줄에 {' · '.join(listMissing)} 줄이 없습니다 — 문서가 답하는 질문 · 독자 · 결론을 세 줄로 적으세요")

        # 규칙 6 — 규칙 문서끼리 한글 문장 복사. 커밋 훅(--files)에서도 비교 상대를 디스크에서 모두 읽는다.
        if listRuleDoc:
            mapShingle: dict[str, set[str]] = {}
            for candidate in sorted(repositoryRoot.glob("*.md")) + sorted((repositoryRoot / "docs").rglob("*.md")):
                relCandidate = candidate.relative_to(repositoryRoot).as_posix()
                if _kRuleDocRe.match(relCandidate) is None or relCandidate in _kDuplicateExempt:
                    continue
                mapShingle[relCandidate] = collectHangulShingles(candidate.read_text(encoding="utf-8", errors="replace"))
            mapPairCount: dict[tuple[str, str], int] = {}
            mapOwner: dict[str, str] = {}
            for rel, setShingle in mapShingle.items():
                mapCount: dict[str, int] = {}
                for shingle in setShingle:
                    other = mapOwner.get(shingle)
                    if other is not None:
                        mapCount[other] = mapCount.get(other, 0) + 1
                    mapOwner[shingle] = rel
                for other, count in mapCount.items():
                    mapPairCount[(other, rel)] = count
            setChecked = {documentPath.relative_to(repositoryRoot).as_posix() for documentPath in listRuleDoc}
            for (first, second), count in sorted(mapPairCount.items()):
                if count > _kDuplicateLimit and (first in setChecked or second in setChecked):
                    listViolation.append(f"{first} 와 {second} 에 같은 한글 문장이 겹칩니다(조각 {count} 개) — 한 문서에만 쓰고 다른 쪽은 링크 한 줄로 줄이세요")

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
            if completed.returnCode > 1:   # 0 = 무시되는 것 있음, 1 = 없음, 그 밖은 git 오류 — 오류를 "무시 없음" 으로 읽지 않는다
                raise GateError(f"git check-ignore 가 실패했습니다: {completed.stderr.strip()}")
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
