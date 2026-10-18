#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Scripts/lint/gate/CheckDataDrivenLiterals.py

튜닝값 · 기본값이 코드로 다시 들어오지 못하게 막는다(결정 기록 5-4 "유지보수성을 위한 데이터화", docs/agents/Constants.md Constants 절).

규칙 둘:
  - `AttributeFallback`: 데이터 속성을 읽으며 리터럴 기본값을 채우거나(`getAttributeFloat( "speed", 3.0f )`) 범위를 조용히 자르는 줄
    (`MathUtil::max( 0, node.getAttributeInt( … ) )`). 스키마(리플렉션 Def 의 초기값 · `Required` · `Min`/`Max`)가 정하고
    `GameDataXML::readFields` 가 로드 시점에 검증한다. 대상은 Source 전체.
  - `TuningConstexpr`: 게임 · 키트 코드의 실수 `constexpr` 상수(`static constexpr float32 kSpeed = 3.5f;`) — 게임플레이 튜닝값은
    PROPERTY 나 키트 설정 · 카탈로그 칸이다. 대상은 `Source/Games` · `Source/GameFramework/Kits`. 불변식(수학 상수 · 형식 상한 등)이면
    예외 표에 이유와 함께 적는다.

커밋 훅은 추가한 줄만, 전체 훑기는 규칙마다 `rules/CheckDataDrivenLiterals.toml` 의 `[baseline]` 을 넘을 때만 실패한다 — 두 모드 · 병합 커밋 ·
`--whole-files` · `--list` 는 기반(`LintGate.BaselineSourceGate`)이 정한다. 예외 키는 `<규칙>:<경로>:<이름>`(`TuningConstexpr` 은 상수 이름,
`AttributeFallback` 은 속성 이름).

  python Scripts/lint/gate/CheckDataDrivenLiterals.py [--root <repo>] [--files a.cpp] [--whole-files] [--baseline N] [--list]
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintGate

from common import blankComments  # noqa: E402
from LintGate import BaselineSourceGate, CountedFinding  # noqa: E402
from SourceScan import SourceFile  # noqa: E402

_kSuffixes = (".h", ".hpp", ".inl", ".cpp")
_kScanRoots = ("Source",)
_kTuningRoots = ("Source/Games/", "Source/GameFramework/Kits/")
_kLiteral = r'(?:-?\d+(?:\.\d*)?(?:[eE][-+]?\d+)?[fFuU]?|true|false|"[^"]*")'
_kFallbackRe = re.compile(r'getAttribute(?:Float|Int|Int64|Uint|Bool|Text)?\(\s*"(?P<name>[^"]+)"\s*,\s*' + _kLiteral + r'\s*\)')
_kClampRe = re.compile(r'MathUtil::(?:max|min|clamp|saturate)\s*\((?P<body>[^;]*?)getAttribute\w*\(\s*"(?P<name>[^"]+)"')
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
    bTuningScope = relative.startswith(_kTuningRoots)
    for index, line in enumerate(listLine):
        for match in _kFallbackRe.finditer(line):
            listFinding.append(Finding("AttributeFallback", relative, index + 1, match.group("name"), listOriginal[index].strip()))
        for match in _kClampRe.finditer(line):
            listFinding.append(Finding("AttributeFallback", relative, index + 1, match.group("name"), listOriginal[index].strip()))
        if bTuningScope:
            for match in _kTuningRe.finditer(line):
                listFinding.append(Finding("TuningConstexpr", relative, index + 1, match.group("name"), listOriginal[index].strip()))
    return listFinding


class CheckDataDrivenLiteralsGate(BaselineSourceGate):
    """`selfTestCases` 는 이 린트가 **반드시 잡아야 하는** 조각이다."""

    description = "데이터 속성의 리터럴 기본값 · 조용한 범위 자르기와 게임 · 키트의 실수 튜닝 상수를 막는다(훅은 추가한 줄만, 전체는 기준선 초과만)"
    buildComment = "Checking that tuning values and attribute defaults stay in data..."
    timeoutSeconds = 120
    preCommitPattern = tuple(f"{root}/*" for root in _kScanRoots)
    sourceScanRoots = _kScanRoots
    sourceSuffixes = _kSuffixes
    violationHeader = "데이터로 옮길 리터럴"
    baselineRules = ("AttributeFallback", "TuningConstexpr")
    countedNoun = "literals"
    baselineExceededHint = "새 줄을 데이터로 옮기거나 불변식이면 예외 표에 이유와 함께 적는다"
    hint = ("  속성 기본값 · 필수 · 범위는 리플렉션 Def 의 초기값 · PROPERTY( Required, Min, Max ) 로 적고 GameDataXML::readFields 로 읽는다.\n"
            "  게임플레이 튜닝값은 컴포넌트 PROPERTY(기본값은 생성자) 나 키트 설정 · 카탈로그 칸이다. 불변식이면 rules 의 [exemption] 에\n"
            "  \"<규칙>:<경로>:<이름>\" = \"이유\" 로 적는다.")
    selfTestCases = [
        {"name": "속성 리터럴 기본값", "expectedViolation": "Source/Probe/A.cpp:3: AttributeFallback 'speed'", "files": {"Source/Probe/A.cpp": "void f( const XMLNode& node )\n{\n    float s = node.getAttributeFloat( \"speed\", 3.0f );\n}\n"},
         "args": ["--files", "Source/Probe/A.cpp", "--whole-files"]},
        {"name": "속성 조용한 자르기", "expectedViolation": "Source/Probe/B.cpp:3: AttributeFallback 'capacity'", "files": {"Source/Probe/B.cpp": "void f( const XMLNode& node )\n{\n    int c = MathUtil::max( 1, node.getAttributeInt( \"capacity\", c ) );\n}\n"},
         "args": ["--files", "Source/Probe/B.cpp", "--whole-files"]},
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
