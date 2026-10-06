#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
문서(`*.md`)마다 **사람이 읽기 어렵게 만드는 문장 모양**을 세어 보고한다.

[왜 필요한가]
문서는 컴파일되지 않아서, 읽기 어려워지는 과정을 아무도 보지 못한다. 한 줄에 사실 하나를 더할 때마다 `·` 로 명사를 이어 붙이고,
괄호 안에 괄호를 넣고, 표 칸에 문장을 밀어 넣다 보면 어느새 아무도 읽지 않는 문서가 된다.
이 보고서는 그 모양을 숫자로 보여 준다. 기준은 `docs/10_WritingDocs.md` 의 문장 규칙이다.

[무엇을 세나] — 코드 블록과 인라인 코드(백틱) 안은 세지 않는다.
- 나열: 한 문장에 ` · ` 가 둘 이상(명사 셋 이상을 가운뎃점으로 이은 것). 셋 이상이면 목록으로 쓴다.
- 괄호 중첩: 괄호 안의 괄호.
- 긴 문장: 문장 하나가 `--max-sentence` 자(기본 120)를 넘는 것.
- 긴 표 칸: 표 칸 하나가 `--max-cell` 자(기본 60)를 넘는 것. 표 칸에는 문장이 아니라 짧은 값을 둔다.
- 대시 이음: 한 줄에 ` — ` 가 둘 이상.
- 폴더 트리: `├─` · `└─` 가 든 코드 블록. 파일 목록은 코드가 원본이다.
- 조어: 용어 대조표(`kListCoinedTerm`)에 있는 저장소 조어가 나온 횟수.

[게이트가 아니다]
`Run*` 은 보고하고 `Check*` 이 막는다 — 종료 코드는 늘 0 이다. 문장이 좋은지는 사람이 판단한다. 다시 쓰기 전과 후를 비교할 때 쓴다.

사용법:
  py -3 Scripts/lint/report/RunDocStyle.py                          # 모든 문서, 점수 순
  py -3 Scripts/lint/report/RunDocStyle.py --files Source/Engine/Object/README.md --detail
  py -3 Scripts/lint/report/RunDocStyle.py --terms                  # 조어가 많이 나오는 문서와 낱말
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintReport

from common import collectRepositoryFiles, kNotOurDirNames  # noqa: E402
from LintReport import LintReport, ReportContext  # noqa: E402

kDefaultMaxSentenceLength = 120
kDefaultMaxCellLength = 60

#: 세지 않는 문서 — 생성 문서(코드에서 만든다)와 영어 규칙 문서, 할 일 목록, 출처 표기, 그리고 나쁜 예를 일부러 싣는 지침 문서.
_kSkippedPathPrefix: tuple[str, ...] = ("docs/Config/", "AGENTS.md", "CLAUDE.md", "docs/06_Backlog.md", "docs/10_WritingDocs.md")
_kSkippedFileName: frozenset[str] = frozenset({"credits.md"})

#: 저장소 조어 — 용어 대조표의 왼쪽 열. (정규식, 쓸 말) 쌍. 쓸 말은 보고에만 쓴다.
kListCoinedTerm: tuple[tuple[str, str], ...] = (
    (r"판(?=[ 을이은의·,)])", "버전"),
    (r"갈래", "분기 · 변형"),
    (r"굽(?:는|고|은|기|다)", "베이크 · 쿠킹"),
    (r"짓(?:는|고|기|다)|지은", "빌드 · 생성"),
    (r"얹(?:는|고|은|다)", "적용 · 추가"),
    (r"끝점", "엔드포인트"),
    (r"등록부", "레지스트리"),
    (r"(?<![가-힣])칸(?![가-힣])", "슬롯 · 필드"),
    (r"묶음", "배치 · 번들 · 그룹"),
    (r"거둔|걷(?:는|어|고|은)", "수집 · 정리"),
    (r"(?<![가-힣])넷(?![가-힣])", "네 개"),
    (r"정본", "기준 · 원본"),
    (r"도장", "스탬프"),
    (r"창구", "API · 진입점"),
    (r"통로", "경로 · 채널"),
    (r"몫", "할당량 · 담당"),
    (r"문턱", "임계값"),
    (r"이름표", "레이블 · 이름"),
    (r"견주|견줘", "비교"),
    (r"되살", "복원"),
    (r"(?<![가-힣])꼴(?![가-힣])", "형태 · 레이아웃"),
)

_kFenceRe = re.compile(r"^\s*(```|~~~)")
_kInlineCodeRe = re.compile(r"`[^`\n]*`")
_kLinkTargetRe = re.compile(r"\]\([^)]*\)")
_kSentenceEndRe = re.compile(r"(?<=[다요까])\.(?=\s|$)|(?<=[.!?])\s+(?=[A-Z가-힣])")
_kTreeRe = re.compile(r"[├└]─")


@dataclass
class DocStyleStats:
    """문서 하나의 셈."""

    path: str
    lineCount: int = 0
    dotChainCount: int = 0
    nestedParenCount: int = 0
    longSentenceCount: int = 0
    longCellCount: int = 0
    dashChainCount: int = 0
    treeBlockCount: int = 0
    coinedTermCount: int = 0
    listDetail: list[str] = field(default_factory=list)
    mapCoinedTerm: Counter[str] = field(default_factory=Counter)

    def getScore(self) -> int:
        """정렬용 — 줄 수로 나누지 않는다. 큰 문서가 위에 오는 것이 맞다(고칠 양)."""
        return (self.dotChainCount * 2 + self.nestedParenCount * 2 + self.longSentenceCount + self.longCellCount
                + self.dashChainCount + self.treeBlockCount * 5)


def stripCodeInternal(line: str) -> str:
    """인라인 코드와 링크 대상을 지운다 — 그 안의 `·` · 괄호는 문장이 아니다."""
    return _kLinkTargetRe.sub("]", _kInlineCodeRe.sub("X", line))


def countNestedParensInternal(text: str) -> int:
    depth = 0
    nestedCount = 0
    for character in text:
        if character in "(（":
            depth += 1
            if depth == 2:
                nestedCount += 1
        elif character in ")）" and depth > 0:
            depth -= 1
    return nestedCount


def measureDocument(repositoryRoot: Path, filePath: Path, maxSentence: int, maxCell: int) -> DocStyleStats:
    relativePath = filePath.relative_to(repositoryRoot).as_posix()
    stats = DocStyleStats(path=relativePath)
    listLine = filePath.read_text(encoding="utf-8", errors="replace").splitlines()
    stats.lineCount = len(listLine)

    bInFence = False
    bTreeInFence = False
    listProse: list[tuple[int, str]] = []
    for lineNumber, line in enumerate(listLine, start=1):
        if _kFenceRe.match(line):
            if bInFence and bTreeInFence:
                stats.treeBlockCount += 1
            bInFence = not bInFence
            bTreeInFence = False
            continue
        if bInFence:
            bTreeInFence = bTreeInFence or bool(_kTreeRe.search(line))
            continue
        prose = stripCodeInternal(line)
        if prose.lstrip().startswith("|"):
            for cell in prose.strip().strip("|").split("|"):
                if len(cell.strip()) > maxCell:
                    stats.longCellCount += 1
                    stats.listDetail.append(f"{lineNumber}: 긴 표 칸({len(cell.strip())}자)")
        listProse.append((lineNumber, prose))

        if prose.count(" · ") >= 2:
            stats.dotChainCount += 1
            stats.listDetail.append(f"{lineNumber}: 나열 ` · ` {prose.count(' · ')}개")
        nested = countNestedParensInternal(prose)
        if nested > 0:
            stats.nestedParenCount += nested
            stats.listDetail.append(f"{lineNumber}: 괄호 중첩")
        if prose.count(" — ") >= 2:
            stats.dashChainCount += 1
            stats.listDetail.append(f"{lineNumber}: 대시 이음 {prose.count(' — ')}개")
        for pattern, _ in kListCoinedTerm:
            for match in re.finditer(pattern, prose):
                stats.coinedTermCount += 1
                stats.mapCoinedTerm[match.group(0)] += 1

    # 문장은 줄을 넘는다 — 문단(빈 줄 · 목록 · 표 · 제목 경계)을 이어 붙여 나눈다.
    listParagraph: list[tuple[int, str]] = []
    currentStart = 0
    currentText = ""
    for lineNumber, prose in listProse:
        stripped = prose.strip()
        bBoundary = stripped == "" or stripped.startswith(("#", "|", "- ", "* ", ">")) or re.match(r"^\d+\.\s", stripped)
        if bBoundary and currentText:
            listParagraph.append((currentStart, currentText))
            currentText = ""
        if stripped == "" or stripped.startswith(("#", "|")):
            continue
        if not currentText:
            currentStart = lineNumber
        currentText += " " + stripped.lstrip("-*> ").strip()
    if currentText:
        listParagraph.append((currentStart, currentText))
    for startLine, paragraph in listParagraph:
        for sentence in _kSentenceEndRe.split(paragraph):
            if len(sentence.strip()) > maxSentence:
                stats.longSentenceCount += 1
                stats.listDetail.append(f"{startLine}~: 긴 문장({len(sentence.strip())}자)")
    return stats


def collectDocuments(repositoryRoot: Path, listFile: list[str]) -> list[Path]:
    """`listFile`(저장소 상대 경로)를 주면 그것만, 아니면 세는 문서 전부를 돌려줍니다. 빌드 트리 · 외부 코드로는 내려가지 않습니다."""
    if listFile:
        return [(repositoryRoot / name).resolve() for name in listFile]
    listPath: list[Path] = []
    for filePath in collectRepositoryFiles(repositoryRoot, suffixes=(".md",), excludedDirNames=kNotOurDirNames | {"ThirdParty"}):
        relativePath = filePath.relative_to(repositoryRoot).as_posix()
        if relativePath.startswith(_kSkippedPathPrefix) or filePath.name in _kSkippedFileName:
            continue
        listPath.append(filePath)
    return listPath


class RunDocStyleReport(LintReport):
    description = "문서의 읽기 어려운 문장 모양(나열 · 괄호 중첩 · 긴 문장 · 긴 표 칸 · 폴더 트리 · 조어) 보고"

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--files", nargs="*", default=[], help="저장소 상대 경로. 주지 않으면 모든 문서")
        parser.add_argument("--detail", action="store_true", help="문서마다 걸린 줄을 보인다")
        parser.add_argument("--terms", action="store_true", help="조어가 나온 횟수를 낱말별로 보인다")
        parser.add_argument("--max-sentence", type=int, default=kDefaultMaxSentenceLength, help="문장 길이 상한(자)")
        parser.add_argument("--max-cell", type=int, default=kDefaultMaxCellLength, help="표 칸 길이 상한(자)")

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        repositoryRoot = context.repositoryRoot
        listStats = [measureDocument(repositoryRoot, filePath, args.max_sentence, args.max_cell)
                     for filePath in collectDocuments(repositoryRoot, args.files)]
        listStats.sort(key=lambda stats: stats.getScore(), reverse=True)

        print(f"[RunDocStyle] 문서 {len(listStats)}개 — 점수 = 나열×2 + 괄호중첩×2 + 긴문장 + 긴칸 + 대시이음 + 트리×5")
        print(f"  {'점수':>5} {'줄':>5} {'나열':>4} {'괄호':>4} {'긴문장':>5} {'긴칸':>4} {'대시':>4} {'트리':>4} {'조어':>4}  문서")
        for stats in listStats:
            print(f"  {stats.getScore():5d} {stats.lineCount:5d} {stats.dotChainCount:4d} {stats.nestedParenCount:4d} "
                  f"{stats.longSentenceCount:5d} {stats.longCellCount:4d} {stats.dashChainCount:4d} {stats.treeBlockCount:4d} "
                  f"{stats.coinedTermCount:4d}  {stats.path}")
            if args.detail:
                for detail in stats.listDetail:
                    print(f"        {detail}")
        if args.terms:
            totalTerm: Counter[str] = Counter()
            for stats in listStats:
                totalTerm.update(stats.mapCoinedTerm)
            print("[RunDocStyle] 조어 — 낱말별 횟수")
            for term, count in totalTerm.most_common():
                print(f"  {count:5d}  {term}")
        return 0


main = RunDocStyleReport.run

if __name__ == "__main__":
    sys.exit(main())
