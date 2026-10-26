#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""린트 게이트가 파일을 직접 걷는(`rglob` · `glob`) 곳을 잡는다 — 파일 모으기는 `collectRepositoryFiles` 나 공유 훑기(`sourceScanRoots`)다.

게이트마다 제 걸음으로 걸으면 같은 파일을 게이트 수만큼 읽고, 빌드 산출물 · 외부 도구 폴더(`Tools/`)까지 내려가 느려지고, 게이트 둘이
서로 다른 파일 집합을 본다. 공유 훑기는 한 번 읽은 글을 나눠 쓰고 제외 규칙이 한 곳에 있다.
`Path.glob` 은 표준 함수라 형식으로 막을 수 없다 — 그래서 막지 못하는 틈에만 이 게이트를 둔다. 폴더 하나의 직속 파일을 이름으로 훑는
것처럼 공유 훑기가 맞지 않는 쓰임은 기준선에 들어 있으니, 새 쓰임이 정말 필요하면 이유를 적고 기준선을 올린다.

  python Scripts/lint/gate/CheckGateFileWalk.py [--root <repo>] [--files a.py] [--list]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from lint.LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from lint.SourceScan import SourceFile  # noqa: E402

_kScanRoots = ("Scripts/lint/gate",)

_kWalkRe = re.compile(r"\.(?:rglob|glob)\(")
#: `common.Search` 의 걷기를 게이트가 직접 부르는 곳 — 보조 걷기가 0건을 모아도 통과하는 길이다. `LintGate.walkRepository` · `walkSources` 는 `bMustFind` 를 꼭 받는다.
_kSharedWalkRe = re.compile(r"(?<![\w.])(?:collectRepositoryFiles|collectSourceFiles)\(")


class CheckGateFileWalkGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "린트 게이트가 파일을 rglob · glob 으로 직접 걷지 않고 collectRepositoryFiles · 공유 훑기를 쓰는지 검사(기준선 래칫)"
    buildComment = "Checking that lint gates collect files through the shared scan..."
    timeoutSeconds = 60
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoots)
    sourceScanRoots = _kScanRoots
    sourceSuffixes = (".py",)
    violationHeader = "게이트의 직접 파일 걷기"
    countedNoun = "direct file walks"
    baselineExceededHint = "collectRepositoryFiles 나 sourceScanRoots 로 옮긴다"
    hint = ("  게이트의 파일 모으기는 sourceScanRoots(공유 훑기) 또는 common.Search.collectRepositoryFiles 입니다.\n"
            "  직접 rglob 하면 같은 파일을 게이트마다 다시 읽고 Tools/ · build/ 로 내려갑니다. 전체를 보려면 --list.")
    selfTestCases = [
        {"name": "rglob 로 직접 걷는다", "expectedViolation": "1  Scripts/lint/gate/CheckProbe.py",
         "files": {"Scripts/lint/gate/CheckProbe.py": "def run( root ):\n    return list( root.rglob( \"*.cpp\" ) )\n"}, "args": ["--baseline", "0"]},
        {"name": "공용 걷기를 bMustFind 없이 직접 부른다", "expectedViolation": "1  Scripts/lint/gate/CheckProbe.py",
         "files": {"Scripts/lint/gate/CheckProbe.py": "def run( root ):\n    return collectRepositoryFiles( root, (\"Source\",), suffixes=(\".cpp\",) )\n"}, "args": ["--baseline", "0"]},
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        listFound: list[CountedFinding] = []
        if source.relative.endswith("/CheckGateFileWalk.py"):
            return listFound   # 자기 자신: 자체 시험 조각이 rglob 글을 담고 있다
        for lineIndex, line in enumerate(source.listLine, start=1):
            code = line.split("#", 1)[0]
            if _kWalkRe.search(code) is not None:
                listFound.append(CountedFinding("total", source.relative, lineIndex, lineIndex,
                                                "직접 파일 걷기 — LintGate.walkRepository 나 sourceScanRoots 로", line.strip()))
            elif _kSharedWalkRe.search(code) is not None and not code.lstrip().startswith(("from ", "import ")):
                listFound.append(CountedFinding("total", source.relative, lineIndex, lineIndex,
                                                "공용 걷기를 직접 부름 — LintGate.walkRepository · walkSources 로(bMustFind 를 적어야 0건 통과를 막는다)", line.strip()))
        return listFound


main = CheckGateFileWalkGate.run

if __name__ == "__main__":
    sys.exit(main())
