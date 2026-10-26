#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
시나리오 파일의 고정 프레임 대기를 정적으로 세어 가장 긴 시나리오와 조건 대기로 바꿀 후보를 찾는다 — App 을 띄우지 않는다.

    py -3 -m Scripts scenario-waits                      # 대기 프레임 합계 상위 20 개
    py -3 -m Scripts scenario-waits --top 40 --gaps 5    # 상위 40 개, 시나리오마다 가장 긴 고정 대기 5 곳
    py -3 -m Scripts scenario-waits --candidates         # 조건 대기(WaitUntil)로 바꿀 후보: 긴 고정 대기 뒤에 상태를 단언하는 곳
    py -3 -m Scripts scenario-waits --json out.json      # 시나리오마다 값을 JSON 으로 남긴다(샤드 비용표 제안의 입력)
    py -3 -m Scripts scenario-waits --measured-dir build/Ninja-Release/Bin/Saved/Automation   # 보고의 timeline 으로 어림을 견준다

프레임 셈은 C++ `AutomationScenario::readAtFrame` 와 같다: `<At>` 은 `frame`(절대) · `time`(초 → `fixedDelta` 로 반올림) · `after`(앞 `<At>` 에서) · `afterSeconds` 중 하나이고,
고정 대기란 이어진 두 `<At>` 의 프레임 차이다. `<WaitUntil>` 이 붙잡는 프레임은 시나리오 프레임에 세지 않으므로 파일만으로는 알 수 없고 그 개수만 센다(보고의 `timeline.heldFrames` 가 잰 값이다).
어림 시간 = 마지막 `<At>` 프레임 × `--frame-seconds`(기본 `kDefaultFrameSeconds` — 이 값은 Release 한 판의 프레임 비용 어림이라 기계마다 다르다. 순위 매기기용이고 케이스 비용으로 쓰려면 `--measured-dir` 로 보정한다).
종료 코드: 0 목록을 냈다, 2 읽을 수 없다(시나리오 폴더 없음 · XML 오류 · 측정 폴더 없음).
"""

from __future__ import annotations

import argparse
import json
import sys
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from common.Paths import getProjectRoot  # noqa: E402
from common.ScenarioFile import listScenarioIds  # noqa: E402

#: 시나리오 루트에 `fixedDelta` 가 없을 때의 값(C++ `AutomationScenarioInternal::kDefaultFixedDelta`).
kDefaultFixedDelta = 1.0 / 60.0
#: 프레임 하나의 벽시계 어림(초). 순위용이다 — 사진 · 에디터 UI 단계가 든 프레임은 더 비싸다. 측정(`--measured-dir`)이 정하는 값이 아니다.
kDefaultFrameSeconds = 0.012
#: 이보다 긴 프레임 차이를 "긴 고정 대기" 로 센다.
kLongGapFrames = 30
#: 상태 단언이라 앞 대기를 조건 대기로 바꿀 수 있는 단계(`Expect` 류는 탐침을 읽는다).
kProbeAssertionSteps: frozenset[str] = frozenset({"Expect", "ExpectVisited", "ExpectVariable", "EditorExpectObject"})
#: 시계를 붙잡는 단계 — 앞 대기가 이미 조건 대기다.
kHoldingSteps: frozenset[str] = frozenset({"WaitUntil", "Screenshot", "CaptureEditorUI", "CaptureWindow"})
#: 시나리오 파일 루트 아래에서 `<At>` 이 아닌 자식(선언).
kNonAtChildren: frozenset[str] = frozenset({"Launch", "Scratch", "Load", "Backend", "Engine"})


@dataclass(frozen=True)
class FixedGap:
    """이어진 두 `<At>` 사이의 고정 대기 하나."""

    startFrame: int
    frames: int
    endSteps: tuple[str, ...]       # 대기가 끝나고 처음 도는 단계 이름
    bConditionNext: bool            # 그 단계가 이미 조건을 기다리는 단계다(WaitUntil 등)
    bProbeAssertNext: bool          # 그 단계가 탐침을 읽는 단언이다 — 조건 대기로 바꿀 수 있다
    beforeSteps: tuple[str, ...]    # 대기 앞에서 마지막으로 도는 단계 이름 — 무엇을 기다리는지 짐작하는 단서


@dataclass(frozen=True)
class ScenarioWaitCost:
    """시나리오 하나의 정적 대기 값."""

    resourceId: str
    fixedDelta: float
    timeoutFrames: int
    lastFrame: int
    stepCount: int
    waitUntilCount: int
    screenshotCount: int
    gapFrames: int                  # 긴 고정 대기의 프레임 합
    listGap: tuple[FixedGap, ...]   # 긴 고정 대기, 긴 순

    def estimateSeconds(self, frameSeconds: float) -> float:
        return self.lastFrame * frameSeconds


def readFloatInternal(text: str | None, defaultValue: float) -> float:
    return defaultValue if text is None else float(text)


def computeAtFrame(atElement: ElementTree.Element, previousFrame: int, fixedDelta: float) -> int:
    """`<At>` 의 시점 프레임. 정확히 하나의 속성이 아니면 `ValueError`(C++ 이 읽기 오류로 서는 모양)."""
    mapAttribute = dict(atElement.attrib)
    listKey = [key for key in mapAttribute if key in ("frame", "time", "after", "afterSeconds")]
    if len(listKey) != 1 or len(mapAttribute) != 1:
        raise ValueError(f"<At> 는 frame · time · after · afterSeconds 중 정확히 하나만 받는다: {mapAttribute}")
    key = listKey[0]
    value = mapAttribute[key]
    if key == "frame":
        return int(value)
    if key == "after":
        return previousFrame + int(value)
    frames = int(float(value) / fixedDelta + 0.5)
    return frames if key == "time" else previousFrame + frames


def analyzeScenario(resourceId: str, path: Path) -> ScenarioWaitCost:
    """시나리오 파일 하나의 대기를 센다. 읽을 수 없으면 `ValueError`."""
    try:
        root = ElementTree.parse(path).getroot()
    except (OSError, ElementTree.ParseError) as error:
        raise ValueError(f"{resourceId}: XML 을 읽지 못했다 — {error}") from error
    fixedDelta = readFloatInternal(root.get("fixedDelta"), kDefaultFixedDelta)
    timeoutFrames = int(root.get("timeoutFrames", "3600"))
    listGap: list[FixedGap] = []
    previousFrame = 0
    previousSteps: tuple[str, ...] = ()
    stepCount = 0
    waitUntilCount = 0
    screenshotCount = 0
    for child in root:
        if child.tag in kNonAtChildren or child.tag != "At":
            continue
        try:
            frame = computeAtFrame(child, previousFrame, fixedDelta)
        except ValueError as error:
            raise ValueError(f"{resourceId}: {error}") from error
        listName = tuple(step.tag for step in child)
        stepCount += len(listName)
        waitUntilCount += listName.count("WaitUntil")
        screenshotCount += sum(1 for name in listName if name in ("Screenshot", "CaptureEditorUI", "CaptureWindow"))
        gap = frame - previousFrame
        if gap >= kLongGapFrames and listName:
            bCondition = bool(listName) and listName[0] in kHoldingSteps
            bProbe = bool(listName) and listName[0] in kProbeAssertionSteps
            listGap.append(FixedGap(previousFrame, gap, listName[:3], bCondition, bProbe, previousSteps[-2:]))
        previousFrame = frame   # C++ `parseAtNode` 도 거꾸로 적힌 `<At>` 의 프레임을 그대로 앞 프레임으로 삼는다
        if listName:
            previousSteps = listName
    gapFrames = sum(gap.frames for gap in listGap)
    listGap.sort(key=lambda gap: gap.frames, reverse=True)
    return ScenarioWaitCost(resourceId, fixedDelta, timeoutFrames, previousFrame, stepCount, waitUntilCount, screenshotCount, gapFrames, tuple(listGap))


def collectCosts(resourceRoot: Path) -> list[ScenarioWaitCost]:
    """리소스 루트 아래 모든 시나리오(`listScenarioIds` — `AppScenarioTest` 가 찾는 자리와 같다)의 값."""
    return [analyzeScenario(resourceId, resourceRoot / resourceId) for resourceId in listScenarioIds(resourceRoot)]


def readMeasuredMilliseconds(reportFolder: Path) -> dict[str, dict[str, float]]:
    """보고 JSON 들에서 시나리오 이름 → {wallMilliseconds, heldFrames, idleWaitMilliseconds …}. 같은 이름의 여러 백엔드는 가장 큰 값을 둔다."""
    mapResult: dict[str, dict[str, float]] = {}
    for reportPath in sorted(reportFolder.rglob("*.json")):
        try:
            data = json.loads(reportPath.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError):
            continue
        if not isinstance(data, dict) or "wallMilliseconds" not in data or "name" not in data:
            continue
        timeline = data.get("timeline", {})
        entry = {"wallMilliseconds": float(data["wallMilliseconds"]), "frames": float(data.get("frames", 0))}
        for key in ("scheduledWaitMilliseconds", "heldMilliseconds", "heldFrames", "scheduledWaitFrames", "startMilliseconds"):
            entry[key] = float(timeline.get(key, 0)) if isinstance(timeline, dict) else 0.0
        previous = mapResult.get(data["name"])
        if previous is None or entry["wallMilliseconds"] > previous["wallMilliseconds"]:
            mapResult[data["name"]] = entry
    return mapResult


def formatTable(listCost: list[ScenarioWaitCost], top: int, gapCount: int, frameSeconds: float) -> str:
    """상위 `top` 개 표. 프레임 합계는 마지막 `<At>` 프레임이다."""
    lines = [f"{'순위':>4} {'마지막 프레임':>12} {'어림 초':>8} {'긴 고정 대기':>12} {'WaitUntil':>9} {'사진':>5} {'단계':>5}  시나리오"]
    for rank, cost in enumerate(sorted(listCost, key=lambda item: item.lastFrame, reverse=True)[:top], start=1):
        lines.append(f"{rank:>4} {cost.lastFrame:>12} {cost.estimateSeconds(frameSeconds):>8.1f} {cost.gapFrames:>12} {cost.waitUntilCount:>9} {cost.screenshotCount:>5} {cost.stepCount:>5}  {cost.resourceId}")
        for gap in cost.listGap[:gapCount]:
            marker = "조건" if gap.bConditionNext else ("후보" if gap.bProbeAssertNext else "")
            lines.append(f"{'':>4}   +{gap.frames:>5} 프레임 @{gap.startFrame:<6} {' '.join(gap.beforeSteps) or '(시작)'} → {' '.join(gap.endSteps)} {marker}")
    return "\n".join(lines)


def formatCandidates(listCost: list[ScenarioWaitCost], minFrames: int) -> str:
    """탐침 단언 앞의 긴 고정 대기 — 프레임 많은 순. WaitUntil 로 바꾸면 상태에 닿는 순간에 다음으로 간다."""
    listRow = [(gap.frames, cost.resourceId, gap) for cost in listCost for gap in cost.listGap if gap.bProbeAssertNext and gap.frames >= minFrames]
    listRow.sort(key=lambda row: row[0], reverse=True)
    lines = [f"조건 대기 후보 {len(listRow)} 곳(고정 대기 {minFrames} 프레임 이상, 바로 뒤가 탐침 단언):"]
    for frames, resourceId, gap in listRow:
        lines.append(f"  +{frames:>5} @{gap.startFrame:<6} {resourceId}  ({' '.join(gap.beforeSteps) or '시작'} → {' '.join(gap.endSteps)})")
    return "\n".join(lines)


def formatMeasured(listCost: list[ScenarioWaitCost], mapMeasured: dict[str, dict[str, float]], resourceRoot: Path) -> str:
    """어림 프레임과 잰 벽시계를 견준다 — 이름은 루트 `name`."""
    lines = [f"{'잰 초':>7} {'잰 프레임':>9} {'붙잡은':>7} {'정한 대기':>9} {'어림 프레임':>11}  시나리오"]
    listRow: list[tuple[float, str]] = []
    for cost in listCost:
        name = ElementTree.parse(resourceRoot / cost.resourceId).getroot().get("name", "")
        measured = mapMeasured.get(name)
        if measured is None:
            continue
        listRow.append((measured["wallMilliseconds"], f"{measured['wallMilliseconds'] / 1000.0:>7.1f} {int(measured['frames']):>9} {int(measured['heldFrames']):>7} "
                                                        f"{measured['scheduledWaitMilliseconds'] / 1000.0:>9.1f} {cost.lastFrame:>11}  {cost.resourceId}"))
    lines.extend(text for _, text in sorted(listRow, reverse=True))
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="시나리오 파일의 고정 프레임 대기를 정적으로 세어 가장 긴 시나리오와 조건 대기 후보를 찾는다.")
    parser.add_argument("--top", type=int, default=20, help="표에 보일 시나리오 수(기본 20)")
    parser.add_argument("--gaps", type=int, default=0, help="시나리오마다 보일 가장 긴 고정 대기 수(기본 0)")
    parser.add_argument("--candidates", action="store_true", help="조건 대기로 바꿀 후보(긴 고정 대기 뒤에 탐침 단언)를 보인다")
    parser.add_argument("--min-frames", type=int, default=kLongGapFrames, help="후보로 셀 고정 대기의 최소 프레임")
    parser.add_argument("--frame-seconds", type=float, default=kDefaultFrameSeconds, help="프레임 하나의 벽시계 어림(초) — 순위용")
    parser.add_argument("--json", type=Path, default=None, help="시나리오마다 값을 이 파일에 JSON 으로 쓴다")
    parser.add_argument("--measured-dir", type=Path, default=None, help="보고 JSON 폴더 — 있으면 어림 프레임과 잰 시간을 견준 표를 더한다")
    args = parser.parse_args(argv)
    resourceRoot = getProjectRoot() / "Resource"
    try:
        listCost = collectCosts(resourceRoot)
    except ValueError as error:
        print(f"[scenario-waits] {error}", file=sys.stderr)
        return ExitCode.CheckInvalid
    if not listCost:
        print(f"[scenario-waits] {resourceRoot} 아래에 시나리오가 없다", file=sys.stderr)
        return ExitCode.CheckInvalid
    print(formatTable(listCost, args.top, args.gaps, args.frame_seconds))
    if args.candidates:
        print()
        print(formatCandidates(listCost, args.min_frames))
    if args.measured_dir is not None:
        if not args.measured_dir.is_dir():
            print(f"[scenario-waits] 측정 폴더가 없다: {args.measured_dir}", file=sys.stderr)
            return ExitCode.CheckInvalid
        print()
        print(formatMeasured(listCost, readMeasuredMilliseconds(args.measured_dir), resourceRoot))
    if args.json is not None:
        listRow = [{"resourceId": cost.resourceId, "lastFrame": cost.lastFrame, "gapFrames": cost.gapFrames, "waitUntilCount": cost.waitUntilCount,
                    "screenshotCount": cost.screenshotCount, "stepCount": cost.stepCount, "fixedDelta": cost.fixedDelta, "timeoutFrames": cost.timeoutFrames}
                   for cost in listCost]
        args.json.write_text(json.dumps(listRow, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
