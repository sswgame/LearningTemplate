#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
어림값 문턱(`<ExpectImage provisional="…">`)을 잰 값(`measured="날짜 백엔드:값 …"`)으로 승격하는 도구 — 숫자는 보고의 `metrics[]` 에서만 가져오고 이 도구가 정하지 않는다.

    py -3 -m Scripts scenario-thresholds --list                                                  # 어림값 단언 목록(측정 전에도 본다)
    py -3 -m Scripts scenario-thresholds --reports build/Ninja-Release/Bin/Saved/Automation      # 제안 표만 낸다(아무것도 쓰지 않는다)
    py -3 -m Scripts scenario-thresholds --reports <폴더> [<폴더> …] --backends dx12 vk --apply   # 승격할 수 있는 단언만 시나리오 파일에 쓴다

입력 형식: 시나리오를 백엔드마다 돌려 얻은 보고 JSON(`<줄기>_<백엔드>.json`, 체인 단계는 `<줄기>_<백엔드>.<n>.json`)이다. 보고의 `metrics[]` 줄은 `metric(region) 파일 = 값` 이고
(`AutomationRunner::runExpectImage` 가 단언을 돌 때마다 적는다) 줄 순서가 단계 순서다. 단언과의 짝짓기는 (시나리오 `name`, `metric`, `region`(없으면 `0,0,1,1`), `file`) 같은 줄을
n 번째 줄 ↔ n 번째 `<ExpectImage>` 로 맺는다 — 줄 수와 단언 수가 다르면(앞 단계가 지거나 단언이 건너뛰였다) 추측하지 않고 건너뛴다. 종료 코드 0 · 14 · 15 로 끝까지 돈 보고만 쓴다.

문턱 규칙(`docs/plans/GpuVerification.md` 3절): `atLeast` 는 잰 값의 70 % 아래, `atMost` 는 잰 값의 1.5 배 위(0 이 기준이면 0.001), 부호 있는 값은 0 과 잰 값 사이 절반쯤.
잰 값 + 여유(`kMarginLeast` · `kMarginMost`)를 지키는 현재 문턱은 그대로 두고, 지키지 못하면 규칙 값을 두 유효숫자로 안전한 쪽으로 올림 · 내림해 제안한다.
`equals` · `near` 문턱은 규칙이 없어 사람이 정한다. 백엔드 사이 값이 `kDefectSpread` 보다 벌어지면 결함 의심이라 `measured` 에 넣지 않는다(고친 뒤 다시 잰다).
종료 코드: 0 표를 냈다(제안이 있어도), 2 읽을 수 없다(보고 없음 · 짝이 하나도 안 맞음).
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.AppRun import kBackendSwitch  # noqa: E402
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.ScenarioFile import listScenarioIds, readRootName  # noqa: E402
from common.ScenarioOutcome import kExitCodeHeaderRelPath, readOutcomeExitCodes  # noqa: E402

#: 단언이 영역을 적지 않았을 때 실행기가 지표 줄에 쓰는 글(`AutomationRunner::runExpectImage`).
kDefaultRegionText = "0,0,1,1"
#: `atLeast` 문턱이 잰 값 아래로 비워 둘 비율, `atMost` 문턱이 잰 값 위로 비워 둘 비율(문서의 70 % · 1.5 배).
kMarginLeast = 0.3
kMarginMost = 0.5
#: 기준값이 0 일 때의 문턱 절댓값.
kZeroBaseThreshold = 0.001
#: 백엔드 사이 허용 차이 — 이보다 벌어지면 결함 의심이다(문서: 백엔드 사이 차이는 1e-3 아래).
kDefectSpread = 1.0e-3
#: 모든 백엔드 값이 이 안에서 같으면 `all:` 하나로 적는다.
kSameSpread = 5.0e-6
#: 문턱 비교 속성 가운데 규칙이 있는 둘.
kRuledComparisons: tuple[str, ...] = ("atLeast", "atMost")
#: 문턱 비교 속성 전부(`AutomationRunner::parseNumberComparison`).
kComparisons: tuple[str, ...] = ("atLeast", "atMost", "near", "equals")
_kMetricLineRe = re.compile(r"^(\w+)\(([^)]*)\) (\S+) = (-?[0-9.]+(?:[eE][-+]?[0-9]+)?)$")
_kReportNameRe = re.compile(r"_(" + "|".join(kBackendSwitch) + r")(?:\.\d+)?\.json$")
_kCommentRe = re.compile(r"<!--.*?-->", re.S)
_kExpectImageRe = re.compile(r"<ExpectImage\b[^>]*>", re.S)


@dataclass
class ImageStep:
    """시나리오 파일의 `<ExpectImage>` 하나."""

    resourceId: str
    scenarioName: str
    orderInFile: int                 # 파일 안 `<ExpectImage>` 순서(주석 제외)
    line: int
    metric: str
    region: str
    file: str
    comparison: str                  # atLeast · atMost · near · equals
    threshold: float
    bProvisional: bool
    startOffset: int                 # 파일 글 안의 시작 위치(`--apply` 가 바꿀 자리)
    endOffset: int

    @property
    def key(self) -> tuple[str, str, str]:
        return (self.metric, self.region, self.file)


@dataclass
class Measurement:
    """보고 하나(시나리오 × 백엔드)의 지표 줄들 — 키마다 나온 순서대로 값."""

    scenarioName: str
    backend: str
    startedAt: str
    mapValue: dict[tuple[str, str, str], list[float]] = field(default_factory=dict)


@dataclass
class Proposal:
    """단언 하나의 제안."""

    step: ImageStep
    mapBackendValue: dict[str, float]
    status: str                      # 아래 kStatus* 중 하나
    measuredText: str = ""
    newThreshold: float | None = None
    note: str = ""


kStatusPromote = "승격"
kStatusSkip = "건너뜀"
kStatusDefect = "결함 의심"
kStatusManual = "사람이 정함"


def roundSignificant(value: float, digits: int, bUp: bool) -> float:
    """`value` 를 `digits` 유효숫자로 올림(`bUp`) 또는 내림한다 — 문턱이 잰 값에서 멀어지는 쪽으로 안전하게 맞추는 데 쓴다."""
    if value == 0.0:
        return 0.0
    exponent = math.floor(math.log10(abs(value))) - digits + 1
    scale = 10.0 ** exponent
    scaled = value / scale
    rounded = math.ceil(scaled - 1e-9) if bUp else math.floor(scaled + 1e-9)
    return round(rounded * scale, max(0, -exponent))


def proposeThreshold(comparison: str, measuredMin: float, measuredMax: float) -> float:
    """문서 규칙으로 계산한 문턱. `atLeast` 는 가장 작은 잰 값에서, `atMost` 는 가장 큰 잰 값에서 여유를 둔다."""
    if comparison == "atLeast":
        if measuredMin == 0.0:
            return -kZeroBaseThreshold
        return roundSignificant(measuredMin - kMarginLeast * abs(measuredMin), 2, bUp=False)
    if measuredMax == 0.0:
        return kZeroBaseThreshold
    return roundSignificant(measuredMax + kMarginMost * abs(measuredMax), 2, bUp=True)


def holdsMargin(comparison: str, threshold: float, measuredMin: float, measuredMax: float) -> bool:
    """현재 문턱이 규칙의 여유를 지키는지(더 느슨해도 지키는 것이다)."""
    rule = proposeThreshold(comparison, measuredMin, measuredMax)
    return threshold <= rule if comparison == "atLeast" else threshold >= rule


def formatNumber(value: float) -> str:
    """보고와 시나리오에 쓰는 수 글(여섯 유효숫자, 끝의 0 정리)."""
    return f"{value:.6g}"


def parseSteps(resourceRoot: Path, resourceId: str) -> list[ImageStep]:
    """시나리오 파일 하나의 `<ExpectImage>` 들. 주석 안의 것은 건너뛴다. 읽을 수 없으면 `ValueError`."""
    text = (resourceRoot / resourceId).read_text(encoding="utf-8", newline="")
    masked = _kCommentRe.sub(lambda match: re.sub(r"[^\n]", " ", match.group(0)), text)
    scenarioName = readRootName(resourceRoot / resourceId)
    listStep: list[ImageStep] = []
    for order, match in enumerate(_kExpectImageRe.finditer(masked)):
        snippet = match.group(0)
        try:
            element = ElementTree.fromstring(snippet if snippet.endswith("/>") else snippet[:-1] + "/>")
        except ElementTree.ParseError as error:
            raise ValueError(f"{resourceId}: <ExpectImage> #{order} 를 읽지 못했다 — {error}") from error
        listComparison = [name for name in kComparisons if name in element.attrib]
        if len(listComparison) != 1:
            continue
        comparison = listComparison[0]
        listStep.append(ImageStep(resourceId, scenarioName, order, text.count("\n", 0, match.start()) + 1, element.get("metric", ""), element.get("region", kDefaultRegionText),
                                  element.get("file", ""), comparison, float(element.get(comparison, "nan")), "provisional" in element.attrib, match.start(), match.end()))
    return listStep


def collectSteps(resourceRoot: Path) -> list[ImageStep]:
    return [step for resourceId in listScenarioIds(resourceRoot) for step in parseSteps(resourceRoot, resourceId)]


def readMeasurements(listFolder: list[Path]) -> list[Measurement]:
    """보고 폴더들에서 끝까지 돈 보고의 지표 줄을 읽는다. 시나리오 보고가 아닌 JSON 은 건너뛴다."""
    outcomeCode = readOutcomeExitCodes(getProjectRoot() / kExitCodeHeaderRelPath)
    setRanToEnd = {outcomeCode["Passed"], outcomeCode["Unconfirmed"], outcomeCode["UnhandledLog"]}
    listMeasurement: list[Measurement] = []
    for folder in listFolder:
        for reportPath in sorted(folder.rglob("*.json")):
            nameMatch = _kReportNameRe.search(reportPath.name)
            if nameMatch is None:
                continue
            try:
                data = json.loads(reportPath.read_text(encoding="utf-8-sig"))
            except (OSError, ValueError):
                continue
            if not isinstance(data, dict) or data.get("exitCode") not in setRanToEnd or not isinstance(data.get("metrics"), list):
                continue
            measurement = Measurement(str(data.get("name", "")), nameMatch.group(1), str(data.get("startedAt", "")))
            for line in data["metrics"]:
                lineMatch = _kMetricLineRe.match(str(line))
                if lineMatch is not None:
                    metric, region, fileName, value = lineMatch.groups()
                    measurement.mapValue.setdefault((metric, region, fileName), []).append(float(value))
            listMeasurement.append(measurement)
    return listMeasurement


def buildProposals(listStep: list[ImageStep], listMeasurement: list[Measurement], listBackend: list[str]) -> list[Proposal]:
    """어림값 단언마다 제안 하나. 지표 줄과 단언의 짝이 맞는 백엔드 값만 쓴다."""
    listProposal: list[Proposal] = []
    for step in (item for item in listStep if item.bProvisional):
        listSameKey = [item for item in listStep if item.scenarioName == step.scenarioName and item.key == step.key]
        nth = listSameKey.index(step)
        mapValue: dict[str, float] = {}
        listSkipNote: list[str] = []
        for measurement in listMeasurement:
            if measurement.scenarioName != step.scenarioName or measurement.backend not in listBackend:
                continue
            listLine = measurement.mapValue.get(step.key, [])
            if len(listLine) != len(listSameKey):
                listSkipNote.append(f"{measurement.backend}: 지표 줄 {len(listLine)} 개 ↔ 단언 {len(listSameKey)} 개")
                continue
            mapValue[measurement.backend] = listLine[nth]
        listProposal.append(makeProposal(step, mapValue, listBackend, listSkipNote, listMeasurement))
    return listProposal


def makeProposal(step: ImageStep, mapValue: dict[str, float], listBackend: list[str], listSkipNote: list[str], listMeasurement: list[Measurement]) -> Proposal:
    listMissing = [backend for backend in listBackend if backend not in mapValue]
    if listMissing:
        note = "잰 백엔드 부족: " + " ".join(listMissing) + (" (" + "; ".join(listSkipNote) + ")" if listSkipNote else "")
        return Proposal(step, mapValue, kStatusSkip, note=note)
    listValue = list(mapValue.values())
    spread = max(listValue) - min(listValue)
    if spread > kDefectSpread:
        return Proposal(step, mapValue, kStatusDefect, note=f"백엔드 사이 차이 {spread:.4g} > {kDefectSpread} — 고친 뒤 다시 잰다")
    if step.comparison not in kRuledComparisons:
        return Proposal(step, mapValue, kStatusManual, note=f"{step.comparison} 문턱은 규칙이 없다")
    startedAt = next((item.startedAt for item in listMeasurement if item.scenarioName == step.scenarioName and item.startedAt), "")
    date = startedAt[:10] if len(startedAt) >= 10 else ""
    if not date:
        return Proposal(step, mapValue, kStatusSkip, note="보고에 startedAt 이 없어 날짜를 정할 수 없다")
    if spread <= kSameSpread:
        measuredText = f"{date} all:{formatNumber(sum(listValue) / len(listValue))}"
    else:
        measuredText = date + " " + " ".join(f"{backend}:{formatNumber(mapValue[backend])}" for backend in listBackend)
    newThreshold = None
    note = "문턱 유지"
    if holdsMargin(step.comparison, step.threshold, min(listValue), max(listValue)) is False:
        newThreshold = proposeThreshold(step.comparison, min(listValue), max(listValue))
        note = f"문턱 {formatNumber(step.threshold)} → {formatNumber(newThreshold)}"
    return Proposal(step, mapValue, kStatusPromote, measuredText, newThreshold, note)


def applyProposals(resourceRoot: Path, listProposal: list[Proposal]) -> int:
    """승격할 제안을 시나리오 파일에 쓴다(줄 끝 보존). 바꾼 단언 수를 돌려준다."""
    mapFile: dict[str, list[Proposal]] = {}
    for proposal in listProposal:
        if proposal.status == kStatusPromote:
            mapFile.setdefault(proposal.step.resourceId, []).append(proposal)
    changed = 0
    for resourceId, listFileProposal in mapFile.items():
        path = resourceRoot / resourceId
        text = path.read_text(encoding="utf-8", newline="")
        for proposal in sorted(listFileProposal, key=lambda item: item.step.startOffset, reverse=True):   # 뒤쪽부터 바꿔 앞쪽 위치가 밀리지 않게 한다
            step = proposal.step
            element = text[step.startOffset:step.endOffset]
            replaced = re.sub(r'\bprovisional="[^"]*"', f'measured="{proposal.measuredText}"', element, count=1)
            if proposal.newThreshold is not None:
                replaced = re.sub(rf'\b{step.comparison}="[^"]*"', f'{step.comparison}="{formatNumber(proposal.newThreshold)}"', replaced, count=1)
            text = text[:step.startOffset] + replaced + text[step.endOffset:]
            changed += 1
        path.write_text(text, encoding="utf-8", newline="")
    return changed


def formatProposals(listProposal: list[Proposal]) -> str:
    lines = [f"{'결과':<8} {'시나리오':<52} {'지표(영역) 파일':<60} {'비교':<16} 잰 값 / 제안"]
    for proposal in listProposal:
        step = proposal.step
        where = f"{step.resourceId}:{step.line}"
        what = f"{step.metric}({step.region}) {step.file}"
        valueText = " ".join(f"{backend}={formatNumber(value)}" for backend, value in proposal.mapBackendValue.items()) or "-"
        tail = f'measured="{proposal.measuredText}" · {proposal.note}' if proposal.status == kStatusPromote else proposal.note
        lines.append(f"{proposal.status:<8} {where:<52} {what:<60} {step.comparison}={formatNumber(step.threshold):<8} {valueText} / {tail}")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="어림값 문턱(provisional)을 보고의 잰 값(measured)으로 승격하는 제안을 만들고, 승격할 수 있는 것만 시나리오에 쓴다.")
    parser.add_argument("--reports", type=Path, nargs="*", default=[], help="시나리오 보고 JSON 이 있는 폴더들(`build/<프리셋>/Bin/Saved/Automation`)")
    parser.add_argument("--backends", nargs="+", default=list(kBackendSwitch), choices=list(kBackendSwitch), help="잰 값이 모두 있어야 하는 백엔드(기본 넷)")
    parser.add_argument("--list", action="store_true", help="어림값 단언 목록만 보인다(보고 없이)")
    parser.add_argument("--apply", action="store_true", help="승격할 수 있는 단언을 시나리오 파일에 쓴다(기본은 표만)")
    args = parser.parse_args(argv)
    resourceRoot = getProjectRoot() / "Resource"
    try:
        listStep = collectSteps(resourceRoot)
    except (OSError, ValueError) as error:
        print(f"[scenario-thresholds] {error}", file=sys.stderr)
        return ExitCode.CheckInvalid
    listProvisional = [step for step in listStep if step.bProvisional]
    if args.list or not args.reports:
        for step in listProvisional:
            print(f"{step.resourceId}:{step.line}  {step.metric}({step.region}) {step.file}  {step.comparison}={formatNumber(step.threshold)}")
        print(f"어림값 단언 {len(listProvisional)} 개 / 시나리오 {len({step.resourceId for step in listProvisional})} 개 (전체 ExpectImage {len(listStep)} 개)")
        return ExitCode.Passed
    for folder in args.reports:
        if not folder.is_dir():
            print(f"[scenario-thresholds] 보고 폴더가 없다: {folder}", file=sys.stderr)
            return ExitCode.CheckInvalid
    listMeasurement = readMeasurements(args.reports)
    if not listMeasurement:
        print("[scenario-thresholds] 끝까지 돈 시나리오 보고가 없다(종료 코드 0 · 14 · 15, `<줄기>_<백엔드>.json`)", file=sys.stderr)
        return ExitCode.CheckInvalid
    listProposal = buildProposals(listStep, listMeasurement, list(args.backends))
    print(formatProposals(listProposal))
    promotable = sum(1 for proposal in listProposal if proposal.status == kStatusPromote)
    print(f"\n어림값 {len(listProposal)} 개 중 승격 가능 {promotable} 개")
    if args.apply:
        print(f"시나리오 파일에 {applyProposals(resourceRoot, listProposal)} 곳을 썼다")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
