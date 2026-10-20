#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckDataDrivenLiterals.py

튜닝값 · 기본값이 코드로 다시 들어오지 못하게 막는다(결정 기록 5-4 "유지보수성을 위한 데이터화", docs/agents/Constants.md Constants 절).

규칙 하나:
  - `TuningConstexpr`: 게임 · 키트 코드의 실수 `constexpr` 상수(`static constexpr float32 kSpeed = 3.5f;`) — 게임플레이 튜닝값은
    PROPERTY 나 키트 설정 · 카탈로그 칸이다. 대상은 `Source/Games` · `Source/GameFramework/Kits`. 불변식(수학 상수 · 형식 상한 등)이면
    예외 표에 이유와 함께 적는다.

속성 리터럴 기본값(`getAttributeFloat( "speed", 3.0f )`)을 막던 `AttributeFallback` 은 지웠다(데이터 주도 계획 D9). 폴백 인자를 받는 읽기
함수(XML `getAttribute*` · `getChild*`, JSON `asInt` 류, `KeyValueFile::get*`)를 지워 그 모양은 컴파일 오류가 됐다 — 형식으로 막힌 틈이다.

커밋 훅은 추가한 줄만, 전체 훑기는 규칙마다 `rules/CheckDataDrivenLiterals.toml` 의 `[baseline]` 을 넘을 때만 실패한다 — 두 모드 · 병합 커밋 ·
`--whole-files` · `--list` 는 기반(`LintGate.BaselineSourceGate`)이 정한다. 예외 키는 `<규칙>:<경로>:<상수 이름>` 이다.

  python Scripts/lint/gate/CheckDataDrivenLiterals.py [--root <repo>] [--files a.cpp] [--whole-files] [--baseline N] [--list]
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts

from common.CodeText import blankComments  # noqa: E402
from lint.LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from lint.SourceScan import SourceFile  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".cpp")
_kScanRoots = ("Source",)
_kTuningRoots = ("Source/Games/", "Source/GameFramework/Kits/")
_kTuningRe = re.compile(r'\bconstexpr\s+(?:float32|float64|float|double)\s+(?P<name>k\w+)\s*(?:=|\{)\s*-?\d')


@dataclass
class Finding:
    """위반 하나."""

    rule: str
    relative: str
    line: int
    name: str
    text: str


def findInText(relative: str, text: str, masked: str | None = None) -> list[Finding]:
    """파일 하나의 위반 후보를 찾습니다(주석은 가린 글에서 본다 — `masked` 는 이미 만든 `blankComments(text)`)."""
    masked = blankComments(text) if masked is None else masked
    if len(masked) != len(text):
        return []
    listFinding: list[Finding] = []
    listLine = masked.split("\n")
    listOriginal = text.split("\n")
    if not relative.startswith(_kTuningRoots):
        return listFinding
    for index, line in enumerate(listLine):
        for match in _kTuningRe.finditer(line):
            listFinding.append(Finding("TuningConstexpr", relative, index + 1, match.group("name"), listOriginal[index].strip()))
    return listFinding


class CheckDataDrivenLiteralsGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "게임 · 키트의 실수 튜닝 상수를 막는다(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that tuning values and attribute defaults stay in data..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoots)
    sourceScanRoots = _kScanRoots
    sourceSuffixes = _kSuffixes
    violationHeader = "데이터로 옮길 리터럴"
    baselineRules = ("TuningConstexpr",)
    countedNoun = "literals"
    baselineExceededHint = "새 줄을 데이터로 옮기거나 불변식이면 예외 표에 이유와 함께 적는다"
    hint = ("  게임플레이 튜닝값은 컴포넌트 PROPERTY(기본값은 생성자) 나 키트 설정 · 카탈로그 칸이다. 불변식이면 rules 의 [exemption] 에\n"
            "  \"<규칙>:<경로>:<이름>\" = \"이유\" 로 적는다.")
    selfTestCases = [
        {"name": "게임 실수 튜닝 상수", "expectedViolation": "Source/Games/Probe/C.cpp:3: TuningConstexpr 'kWalkSpeed'", "files": {"Source/Games/Probe/C.cpp": "struct X\n{\n    static constexpr float32 kWalkSpeed = 3.5f;\n};\n"},
         "args": ["--files", "Source/Games/Probe/C.cpp", "--whole-files"]},
    ]

    def findCounted(self, source: SourceFile) -> list[CountedFinding] | None:
        listCounted: list[CountedFinding] = []
        for finding in findInText(source.relative, source.text, source.noComment):
            key = f"{finding.rule}:{finding.relative}:{finding.name}"
            if key in self.mapExemption:
                self.useExemption(key)
                continue
            listCounted.append(CountedFinding(finding.rule, finding.relative, finding.line, finding.line, f"{finding.rule} '{finding.name}'", finding.text))
        return listCounted


main = CheckDataDrivenLiteralsGate.run

if __name__ == "__main__":
    sys.exit(main())
