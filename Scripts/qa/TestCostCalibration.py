#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
샤드 비용표(`Test/TestPolicy.json` 의 `caseCosts` · `hostCaseCosts`)를 실제 케이스 시간으로 보정한다 — 실행하지 않고 ctest 로그를 읽는다.

    py -3 -m Scripts test-costs --log build/Ninja-Release/Testing/Temporary/LastTest.log              # 제안 표만 낸다
    py -3 -m Scripts test-costs --log <로그> [<로그> …] --min-ratio 1.5 --min-seconds 3 --top 20       # 문턱 · 동적 이름 후보 수
    py -3 -m Scripts test-costs --log <로그> --apply                                                   # 정책 표에 있는 이름의 값만 고친다

입력은 `ctest -L hostgpu`(또는 `-L nogpu`)를 돌린 뒤 빌드 폴더에 남는 `Testing/Temporary/LastTest.log` 다. 이 로그에는 테스트(샤드)마다 `Test time = N sec` 과 그 출력이 있고,
출력 안에 케이스마다 `[       OK ] Suite.Case (N ms)` 줄이 있다(`ConsoleTestReporter`). 이 도구는 두 가지를 읽는다.

1. **케이스 시간:** 같은 이름이 여러 줄이면 가장 큰 값(비용표는 가장 나쁜 때에 맞춘다). 정책 표(`caseCosts` · `hostCaseCosts`)의 이름마다 잰 값과 견줘
   비율이 `--min-ratio` 밖이고 차이가 `--min-seconds` 이상이면 고칠 후보로 낸다. 표에 못 적는 동적 이름(`Suite.Case.<백엔드>` 등 — `SW_TEST_CASE_PER_PARAMETER` ·
   `SW_TEST_CASE_GENERATOR`)은 `--top` 개까지 "잰 시간" 으로 따로 보인다 — 그 값은 매크로 인자나 `addCase` 의 예상 초로 고친다.
2. **샤드 겹침:** 이름이 `_Shard<k>` 로 끝나는 테스트들의 시간 합과 가장 긴 하나를 견준다. 합 ÷ 가장 긴 것이 샤드가 실제로 겹친 정도(샤드 수에 가까울수록 좋다)이고,
   `hostResourceLock` 이 없어진 뒤 샤드 둘이 겹쳐 시간이 줄었는지를 이 수로 본다.

`--apply` 는 정책 파일에서 그 실행 파일의 표 안의 숫자만 정수 초로 바꾼다(주석 · 다른 줄 · 줄끝은 그대로). 표에 없는 이름은 더하지 않는다.
종료 코드: 0 표를 냈다, 2 읽을 수 없다(로그 없음 · 케이스 줄이 하나도 없음 · 정책 읽기 오류).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402

kPolicyRelPath = "Test/TestPolicy.json"
#: 비용표를 담는 정책 칸 둘.
kCostTableKeys: tuple[str, ...] = ("caseCosts", "hostCaseCosts")
#: 잰 값이 표 값의 이 배수 안이면 그대로 둔다(케이스 시간은 기계 부하에 따라 흔들린다).
kDefaultMinRatio = 1.5
#: 표 값과 잰 값의 차이가 이보다 작으면 비율이 커도 둔다(수 초짜리의 흔들림).
kDefaultMinSeconds = 3.0
kDefaultTop = 20

_kTestHeaderRe = re.compile(r"^\d+/\d+ Testing: (\S+)\s*$")
_kTestTimeRe = re.compile(r"^Test time =\s*([\d.]+) sec\s*$")
_kCaseLineRe = re.compile(r"\[\s*(?:OK|FAILED)\s*\]\s+(\S+) \(([\d.]+) ms\)")
_kShardNameRe = re.compile(r"^(?P<group>.+?)_Shard(?P<index>\d+)(?:of\d+)?$")


@dataclass(frozen=True)
class LogReading:
    """로그 읽은 결과 — 케이스 이름 → 가장 큰 초, 테스트(샤드) 이름 → 초."""

    mapCaseSeconds: dict[str, float]
    mapTestSeconds: dict[str, float]


@dataclass(frozen=True)
class CostProposal:
    """정책 표의 한 줄에 대한 제안 — 실행 파일 · 이름 · 표 값 · 잰 값(없으면 None)."""

    executable: str
    caseName: str
    tableSeconds: float
    measuredSeconds: float | None

    @property
    def newSeconds(self) -> int | None:
        return None if self.measuredSeconds is None else max(1, round(self.measuredSeconds))


@dataclass(frozen=True)
class ShardOverlap:
    """샤드 묶음 하나의 겹침 — 시간 합 · 가장 긴 것 · 합 ÷ 가장 긴 것."""

    group: str
    listShard: tuple[tuple[str, float], ...]

    @property
    def sumSeconds(self) -> float:
        return sum(seconds for _, seconds in self.listShard)

    @property
    def longestSeconds(self) -> float:
        return max(seconds for _, seconds in self.listShard)

    @property
    def overlap(self) -> float:
        return self.sumSeconds / self.longestSeconds if self.longestSeconds > 0 else 0.0


def readLogs(listLogText: list[str]) -> LogReading:
    """ctest 로그 글들에서 케이스 시간과 테스트 시간을 읽는다(같은 이름은 가장 큰 값)."""
    mapCase: dict[str, float] = {}
    mapTest: dict[str, float] = {}
    for logText in listLogText:
        currentTest = ""
        for line in logText.splitlines():
            headerMatch = _kTestHeaderRe.match(line)
            if headerMatch is not None:
                currentTest = headerMatch.group(1)
                continue
            timeMatch = _kTestTimeRe.match(line)
            if timeMatch is not None and currentTest:
                mapTest[currentTest] = max(mapTest.get(currentTest, 0.0), float(timeMatch.group(1)))
                continue
            for name, milliseconds in _kCaseLineRe.findall(line):
                mapCase[name] = max(mapCase.get(name, 0.0), float(milliseconds) / 1000.0)
    return LogReading(mapCase, mapTest)


def readPolicyCosts(policy: dict) -> dict[str, dict[str, float]]:
    """실행 파일 이름 → (케이스 이름 → 표 값). 표가 둘 다 없는 실행 파일은 뺀다. 숫자가 아닌 값은 `ValueError`."""
    mapExecutable: dict[str, dict[str, float]] = {}
    for executable, entry in policy.get("executables", {}).items():
        mapCost: dict[str, float] = {}
        for key in kCostTableKeys:
            for name, value in entry.get(key, {}).items():
                if isinstance(value, bool) or not isinstance(value, (int, float)):
                    raise ValueError(f"{kPolicyRelPath}: {executable}.{key}.{name} 이 숫자가 아니다: {value!r}")
                mapCost[name] = float(value)
        if mapCost:
            mapExecutable[executable] = mapCost
    return mapExecutable


def proposeCosts(mapPolicyCost: dict[str, dict[str, float]], reading: LogReading, minRatio: float, minSeconds: float
                 ) -> tuple[list[CostProposal], list[CostProposal]]:
    """(고칠 제안, 로그에 없는 표 항목). 표에 있는 이름만 본다."""
    listChange: list[CostProposal] = []
    listMissing: list[CostProposal] = []
    for executable, mapCost in mapPolicyCost.items():
        for caseName, tableSeconds in mapCost.items():
            measured = reading.mapCaseSeconds.get(caseName)
            proposal = CostProposal(executable, caseName, tableSeconds, measured)
            if measured is None:
                listMissing.append(proposal)
                continue
            low, high = sorted((tableSeconds, measured))
            bFar = high / max(low, 1e-9) >= minRatio and high - low >= minSeconds
            if bFar and proposal.newSeconds != round(tableSeconds):
                listChange.append(proposal)
    return listChange, listMissing


def findShardOverlaps(mapTestSeconds: dict[str, float]) -> list[ShardOverlap]:
    """이름이 `<묶음>_Shard<k>`(뒤에 `of<n>` 허용)인 테스트들을 묶음별로 모은다. 샤드가 둘 이상인 묶음만."""
    mapGroup: dict[str, list[tuple[str, float]]] = {}
    for testName, seconds in mapTestSeconds.items():
        match = _kShardNameRe.match(testName)
        if match is not None:
            mapGroup.setdefault(match.group("group"), []).append((testName, seconds))
    return [ShardOverlap(group, tuple(sorted(listShard))) for group, listShard in sorted(mapGroup.items()) if len(listShard) >= 2]


def findUnlistedSlowCases(reading: LogReading, setTableName: set[str], top: int) -> list[tuple[str, float]]:
    """정책 표에 없는 케이스를 느린 순으로 `top` 개(동적 이름 포함) — 매크로 인자의 예상 초를 고칠 후보."""
    listCase = [(name, seconds) for name, seconds in reading.mapCaseSeconds.items() if name not in setTableName]
    return sorted(listCase, key=lambda pair: (-pair[1], pair[0]))[:top]


def applyProposals(policyText: str, listChange: list[CostProposal]) -> tuple[str, int]:
    """정책 글에서 제안한 이름의 숫자만 바꾼다. 실행 파일의 객체 범위 안에서 `"<이름>": <숫자>` 줄을 찾는다. (새 글, 바꾼 개수)."""
    newText = policyText
    changedCount = 0
    for proposal in listChange:
        span = findObjectSpanInternal(newText, proposal.executable)
        if span is None or proposal.newSeconds is None:
            continue
        start, end = span
        pattern = re.compile(rf'("{re.escape(proposal.caseName)}"\s*:\s*)(\d+(?:\.\d+)?)')
        body, count = pattern.subn(lambda match: f"{match.group(1)}{proposal.newSeconds}", newText[start:end], count=1)
        if count:
            newText = newText[:start] + body + newText[end:]
            changedCount += count
    return newText, changedCount


def findObjectSpanInternal(text: str, key: str) -> tuple[int, int] | None:
    """`"<key>": {` 의 `{` 부터 짝 `}` 까지의 글 범위(문자열 안의 괄호는 세지 않는다). 없으면 None."""
    keyMatch = re.search(rf'"{re.escape(key)}"\s*:\s*\{{', text)
    if keyMatch is None:
        return None
    depth = 0
    index = keyMatch.end() - 1
    bInString = False
    while index < len(text):
        character = text[index]
        if bInString:
            if character == "\\":
                index += 1
            elif character == '"':
                bInString = False
        elif character == '"':
            bInString = True
        elif character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return keyMatch.end() - 1, index + 1
        index += 1
    return None


def formatReport(listChange: list[CostProposal], listMissing: list[CostProposal], listOverlap: list[ShardOverlap],
                 listUnlisted: list[tuple[str, float]], mapTestSeconds: dict[str, float]) -> str:
    listLine: list[str] = []
    listLine.append(f"## 표 값을 고칠 후보 {len(listChange)} 개")
    for proposal in listChange:
        listLine.append(f"  {proposal.executable}  {proposal.caseName}: 표 {proposal.tableSeconds:g} s → 잰 값 {proposal.measuredSeconds:.1f} s (제안 {proposal.newSeconds})")
    if listMissing:
        listLine.append(f"## 로그에 없는 표 항목 {len(listMissing)} 개 (이 로그가 그 케이스를 돌지 않았다 — 지우지 않는다)")
        listLine += [f"  {proposal.executable}  {proposal.caseName}: 표 {proposal.tableSeconds:g} s" for proposal in listMissing]
    listLine.append(f"## 표에 없는 느린 케이스 상위 {len(listUnlisted)} 개 (동적 이름은 매크로 인자 · addCase 의 예상 초로 고친다)")
    listLine += [f"  {name}: {seconds:.1f} s" for name, seconds in listUnlisted]
    listLine.append("## 샤드 겹침")
    if not listOverlap:
        listLine.append(f"  샤드 묶음(`<이름>_Shard<k>`)이 로그에 없다 — 테스트 {len(mapTestSeconds)} 개")
    for overlap in listOverlap:
        listLine.append(f"  {overlap.group}: 샤드 {len(overlap.listShard)} 개, 합 {overlap.sumSeconds:.0f} s, 가장 긴 {overlap.longestSeconds:.0f} s, 합 ÷ 가장 긴 = {overlap.overlap:.2f}"
                        f" (샤드 수에 가까울수록 겹침이 좋다)")
        listLine += [f"    {name}: {seconds:.0f} s" for name, seconds in overlap.listShard]
    return "\n".join(listLine)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="ctest 로그의 실제 케이스 시간으로 샤드 비용표(Test/TestPolicy.json)를 보정하는 제안을 낸다")
    parser.add_argument("--log", type=Path, nargs="+", required=True, help="ctest 의 LastTest.log (여러 개면 같은 이름은 가장 큰 값)")
    parser.add_argument("--policy", type=Path, default=None, help=f"정책 파일 (기본 저장소의 {kPolicyRelPath})")
    parser.add_argument("--min-ratio", type=float, default=kDefaultMinRatio, help="표 값과 잰 값의 비가 이 값 이상이어야 고칠 후보다")
    parser.add_argument("--min-seconds", type=float, default=kDefaultMinSeconds, help="표 값과 잰 값의 차이가 이 초 이상이어야 고칠 후보다")
    parser.add_argument("--top", type=int, default=kDefaultTop, help="표에 없는 느린 케이스를 몇 개까지 보일지")
    parser.add_argument("--apply", action="store_true", help="고칠 후보의 숫자를 정책 파일에 쓴다(표에 있는 이름만)")
    args = parser.parse_args(argv)

    policyPath = args.policy or getProjectRoot() / kPolicyRelPath
    try:
        listLogText = [path.read_text(encoding="utf-8", errors="replace") for path in args.log]
        policyText = policyPath.read_bytes().decode("utf-8")   # 줄끝(CRLF)을 보존해 읽는다 — 되쓸 때 그대로 나가야 한다
        mapPolicyCost = readPolicyCosts(json.loads(policyText))
    except (OSError, ValueError) as error:
        print(f"[test-costs] 읽을 수 없다 — {error}", file=sys.stderr)
        return ExitCode.CheckInvalid
    reading = readLogs(listLogText)
    if not reading.mapCaseSeconds:
        print("[test-costs] 로그에 케이스 줄(`[       OK ] Suite.Case (N ms)`)이 하나도 없다 — ctest 가 돈 로그(LastTest.log)가 맞는가", file=sys.stderr)
        return ExitCode.CheckInvalid

    listChange, listMissing = proposeCosts(mapPolicyCost, reading, args.min_ratio, args.min_seconds)
    setTableName = {name for mapCost in mapPolicyCost.values() for name in mapCost}
    print(formatReport(listChange, listMissing, findShardOverlaps(reading.mapTestSeconds), findUnlistedSlowCases(reading, setTableName, args.top), reading.mapTestSeconds))
    if args.apply and listChange:
        newText, changedCount = applyProposals(policyText, listChange)
        policyPath.write_bytes(newText.encode("utf-8"))
        print(f"[test-costs] {changedCount} 곳을 {policyPath} 에 썼다")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
