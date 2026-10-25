#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@file RunScenarioLogs.py
@brief 시나리오 보고(`-scenario-report` JSON)에서 로그 허용 목록(`Resource/engine/automation/allowedlogs.json`)을 지키는 두 가지 일을 합니다.

    py -3 -m Scripts scenario-logs --check                      # 래칫: 목록에 있으나 더 이상 나오지 않는 항목 · 처리하지 않은 로그(종료 코드 15) 보고가 있으면 실패
    py -3 -m Scripts scenario-logs --regen                      # 처리하지 않은 로그를 목록의 초안 항목(사유 · 계획 칸은 TODO)으로 더한다
    py -3 -m Scripts scenario-logs --check --reports build/Ninja-Shipping/Bin/Saved/Automation

규칙은 C++ 쪽(`AutomationLogPolicy`)이 가진다 — 항목은 id · 범위(시나리오 하나) · 사유 · 계획이 모두 채워져야 읽히고, 로그 id 가 없는 줄은 목록으로 가릴 수 없다.
이 도구는 그 규칙을 다시 구현하지 않고 보고만 읽는다. 보고는 시나리오 실행 한 번(백엔드 하나)마다 한 편이라, 같은 시나리오를 여러 백엔드로 돌린 보고를 함께 줘야 한다.

- `--check` 의 낡은 항목: 항목의 시나리오가 끝까지 돈 보고(종료 코드 0 · 14 · 15)가 하나라도 있는데 어느 보고에도 그 id 가 `allowedLogs` 에 없으면
  원인을 고친 것이다 — 항목을 지운다. 그 시나리오의 보고가 없으면 판단하지 않고 알리기만 한다. 스위트(`suite`) 항목은 이 보고에 나오지 않아 세지 않는다
  (호스트 테스트 요약의 `Allowed log lines` 가 맞은 케이스 수를 찍는다).
- `--regen`: 종료 코드 15 보고의 `unhandledLogs` 에서 id 가 있는 줄은 `{ id, scenario, reason: TODO, plan: TODO }` 로 목록에 더한다. TODO 가 남은 목록은 C++ 이 읽지 못해
  모든 시나리오가 읽기 오류(11)로 서므로 사람이 사유 · 계획을 채워야 통과한다. id 가 없는 줄은 목록에 넣지 않고 표로 찍는다 — 소스에 `SW_LOG_WARNING_ID` · `SW_LOG_ERROR_ID` 를 붙인다.

종료 코드: 0 통과, 1 위반(낡은 항목 · 처리하지 않은 로그), 2 판정 불가(보고 없음 · 목록을 못 읽음).
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

#: 허용 목록의 저장소 경로(C++ `AutomationLogPolicy::kAllowListPath` 의 리소스 경로가 가리키는 파일).
kAllowListRelPath = "Resource/engine/automation/allowedlogs.json"
#: 초안이 사람이 채울 칸에 넣는 글(C++ `AutomationLogPolicy::kPlaceholderText` 와 같다 — 이 글이 남으면 목록을 읽지 못한다).
kPlaceholderText = "TODO"
#: 종료 코드 표의 정본(C++ `getAutomationExitCode`) — 숫자를 이 스크립트에 다시 쓰지 않고 헤더의 `case AutomationOutcome::X: return N;` 줄에서 읽는다.
kExitCodeHeaderRelPath = "Source/Engine/Automation/AutomationExitCode.h"
_kExitCodeCaseRe = re.compile(r"case\s+AutomationOutcome::(\w+)\s*:\s*return\s+(\d+)\s*;")


def readOutcomeExitCodes(headerPath: Path) -> dict[str, int]:
    """`AutomationExitCode.h` 에서 결과 이름 → 종료 코드를 읽는다. 표를 못 읽으면 오류로 멈춘다(폴백 없음)."""
    mapCode = {name: int(number) for name, number in _kExitCodeCaseRe.findall(headerPath.read_text(encoding="utf-8"))}
    for name in ("Passed", "Unconfirmed", "UnhandledLog"):
        if name not in mapCode:
            sys.exit(f"{headerPath}: AutomationOutcome::{name} 의 종료 코드 줄을 찾지 못했다 — case 줄 모양이 바뀌었나")
    return mapCode


_mapOutcomeExitCode = readOutcomeExitCodes(getProjectRoot() / kExitCodeHeaderRelPath)
#: 시나리오가 끝까지 돈 종료 코드 — 통과 · 미확정 · 처리하지 않은 로그. 그 밖(실패 · 읽기 오류 · 시간 초과)은 로그 게이트까지 가지 못해 맞은 id 를 모른다.
kRanToEndExitCodes: tuple[int, ...] = (_mapOutcomeExitCode["Passed"], _mapOutcomeExitCode["Unconfirmed"], _mapOutcomeExitCode["UnhandledLog"])
#: 처리하지 않은 로그로 끝난 시나리오의 종료 코드(C++ `AutomationOutcome::UnhandledLog`).
kUnhandledLogExitCode = _mapOutcomeExitCode["UnhandledLog"]
#: `unhandledLogs` 줄의 id 부분 — `[Warning] [호출자] (id=영역.사건) 본문`. id 가 없는 줄은 `(no log id)` 라 맞지 않는다.
_kUnhandledIdRe = re.compile(r"\(id=([A-Za-z0-9_.]+)\)")
#: 보고를 찾을 폴더(저장소 기준) — 시나리오 실행의 작업 폴더가 `Bin` 이라 보고가 `Bin/Saved/Automation` 에 쌓인다.
_kReportFolderPatterns: tuple[str, ...] = ("build/*/Bin/Saved/Automation", "Saved/Automation")


@dataclass(frozen=True)
class ScenarioReport:
    """시나리오 실행 한 번의 보고에서 로그 게이트가 쓰는 부분."""

    path: Path
    name: str
    exitCode: int
    listUnhandled: tuple[str, ...]
    listAllowedId: tuple[str, ...]


@dataclass(frozen=True)
class AllowEntry:
    """허용 목록 항목 하나. 범위는 `scenario` 나 `suite` 중 하나다."""

    id: str
    scenario: str
    suite: str


def parseReport(path: Path) -> ScenarioReport | None:
    """보고 파일 하나를 읽는다. 시나리오 보고가 아니거나(`unhandledLogs` 칸이 없는 옛 형식 포함) 읽을 수 없으면 None."""
    try:
        data = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None
    if not isinstance(data, dict) or "exitCode" not in data or "unhandledLogs" not in data or "allowedLogs" not in data:
        return None
    return ScenarioReport(path=path, name=str(data.get("name", "")), exitCode=int(data["exitCode"]),
                          listUnhandled=tuple(str(line) for line in data["unhandledLogs"]),
                          listAllowedId=tuple(str(item) for item in data["allowedLogs"]))


def findReports(listFolder: list[Path]) -> list[ScenarioReport]:
    """폴더들 아래의 시나리오 보고(`*.json`)를 모두 읽는다."""
    listReport: list[ScenarioReport] = []
    for folder in listFolder:
        if not folder.is_dir():
            continue
        for path in sorted(folder.rglob("*.json")):
            report = parseReport(path)
            if report is not None:
                listReport.append(report)
    return listReport


def readAllowEntries(data: dict) -> list[AllowEntry]:
    """허용 목록 JSON(`{ "allowed": [...] }`)의 항목들을 읽는다. 모양이 틀리면 ValueError — 규칙 검사는 C++ 이 한다."""
    listAllowed = data.get("allowed")
    if not isinstance(listAllowed, list):
        raise ValueError('"allowed" 배열이 없습니다')
    listEntry: list[AllowEntry] = []
    for index, raw in enumerate(listAllowed):
        if not isinstance(raw, dict) or not str(raw.get("id", "")):
            raise ValueError(f"항목 {index} 에 id 가 없습니다")
        listEntry.append(AllowEntry(id=str(raw["id"]), scenario=str(raw.get("scenario", "")), suite=str(raw.get("suite", ""))))
    return listEntry


def findStaleEntries(listEntry: list[AllowEntry], listReport: list[ScenarioReport]) -> tuple[list[AllowEntry], list[AllowEntry]]:
    """(낡은 항목, 판단하지 못한 항목)을 돌려준다. 낡은 항목은 시나리오가 끝까지 돌았는데도 그 id 가 한 번도 맞지 않은 것이다."""
    listStale: list[AllowEntry] = []
    listNotRun: list[AllowEntry] = []
    for entry in listEntry:
        if not entry.scenario:
            continue  # 스위트 항목은 시나리오 보고에 나오지 않는다
        listRan = [report for report in listReport if report.name == entry.scenario and report.exitCode in kRanToEndExitCodes]
        if not listRan:
            listNotRun.append(entry)
        elif not any(entry.id in report.listAllowedId for report in listRan):
            listStale.append(entry)
    return listStale, listNotRun


def collectDraft(listReport: list[ScenarioReport], listExisting: list[AllowEntry]) -> tuple[list[dict[str, str]], list[tuple[str, str]]]:
    """처리하지 않은 로그의 초안 항목과 (시나리오, id 없는 줄) 표를 돌려준다. 이미 목록에 있는 (id, 시나리오)는 더하지 않는다."""
    setKnown = {(entry.id, entry.scenario) for entry in listExisting}
    listNew: list[dict[str, str]] = []
    listNeedsId: list[tuple[str, str]] = []
    for report in listReport:
        if report.exitCode != kUnhandledLogExitCode:
            continue
        for line in report.listUnhandled:
            match = _kUnhandledIdRe.search(line)
            if match is None:
                if (report.name, line) not in listNeedsId:
                    listNeedsId.append((report.name, line))
                continue
            key = (match.group(1), report.name)
            if key in setKnown:
                continue
            setKnown.add(key)
            listNew.append({"id": key[0], "scenario": key[1], "reason": kPlaceholderText, "plan": kPlaceholderText})
    return listNew, listNeedsId


def mergeDraft(data: dict, listNew: list[dict[str, str]]) -> dict:
    """기존 항목에 초안 항목을 더하고 (범위, id) 순으로 정렬한 새 목록 JSON 을 돌려준다."""
    listAllowed = list(data.get("allowed", [])) + listNew
    listAllowed.sort(key=lambda entry: (str(entry.get("scenario") or entry.get("suite") or ""), str(entry.get("id", ""))))
    return {"allowed": listAllowed}


def discoverReportFolders(repositoryRoot: Path) -> list[Path]:
    """기본 보고 폴더들(빌드 폴더별 `Bin/Saved/Automation` 과 저장소의 `Saved/Automation`)."""
    listFolder: list[Path] = []
    for pattern in _kReportFolderPatterns:
        listFolder.extend(sorted(repositoryRoot.glob(pattern)))
    return listFolder


def runCheck(listEntry: list[AllowEntry], listReport: list[ScenarioReport]) -> int:
    """래칫과 처리하지 않은 로그 보고를 판정해 찍고 종료 코드를 돌려준다."""
    listStale, listNotRun = findStaleEntries(listEntry, listReport)
    listUnhandled = [report for report in listReport if report.exitCode == kUnhandledLogExitCode]
    for entry in listStale:
        print(f"[ScenarioLogs] 낡은 항목: {entry.id} (시나리오 {entry.scenario}) — 끝까지 돈 보고에 한 번도 나오지 않았다. 원인을 고쳤으니 항목을 지운다")
    for report in listUnhandled:
        print(f"[ScenarioLogs] 처리하지 않은 로그 {len(report.listUnhandled)} 줄: {report.name} ({report.path.as_posix()})")
        for line in report.listUnhandled:
            print(f"    {line}")
    if listNotRun:
        print(f"[ScenarioLogs] 보고가 없어 판단하지 못한 시나리오 항목 {len(listNotRun)} 개: {', '.join(sorted({entry.scenario for entry in listNotRun}))}")
    countSuite = sum(1 for entry in listEntry if entry.suite)
    print(f"[ScenarioLogs] 보고 {len(listReport)} 편, 시나리오 항목 {len(listEntry) - countSuite} 개(스위트 항목 {countSuite} 개는 세지 않음), 낡은 항목 {len(listStale)} 개, "
          f"처리하지 않은 로그 보고 {len(listUnhandled)} 편")
    return int(ExitCode.Violation) if listStale or listUnhandled else int(ExitCode.Passed)


def runRegen(allowListPath: Path, outPath: Path, listReport: list[ScenarioReport], bDryRun: bool) -> int:
    """처리하지 않은 로그를 초안 항목으로 더한 목록을 `outPath` 에 쓴다(`bDryRun` 이면 찍기만 한다)."""
    data = json.loads(allowListPath.read_text(encoding="utf-8-sig"))
    listNew, listNeedsId = collectDraft(listReport, readAllowEntries(data))
    for entry in listNew:
        print(f"[ScenarioLogs] 초안 항목: {entry['id']} (시나리오 {entry['scenario']}) — reason · plan 을 채운다")
    if listNeedsId:
        print(f"[ScenarioLogs] 로그 id 가 없어 목록으로 가릴 수 없는 줄 {len(listNeedsId)} 개 — 소스에 SW_LOG_WARNING_ID / SW_LOG_ERROR_ID 를 붙인다:")
        for scenario, line in listNeedsId:
            print(f"    {scenario}: {line}")
    if not listNew:
        print("[ScenarioLogs] 더할 초안 항목이 없습니다")
        return int(ExitCode.Passed)
    if bDryRun:
        print(f"[ScenarioLogs] --dry-run: {outPath.as_posix()} 를 쓰지 않았다(초안 {len(listNew)} 개)")
        return int(ExitCode.Passed)
    outPath.parent.mkdir(parents=True, exist_ok=True)
    outPath.write_text(json.dumps(mergeDraft(data, listNew), indent=4, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"[ScenarioLogs] {outPath.as_posix()} 에 초안 {len(listNew)} 개를 썼다 — TODO 가 남은 동안 시나리오는 읽기 오류(11)로 선다")
    return int(ExitCode.Passed)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="시나리오 보고로 로그 허용 목록의 낡은 항목을 세고 초안 항목을 만든다")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="낡은 항목 · 처리하지 않은 로그(종료 코드 15) 보고가 있으면 실패")
    mode.add_argument("--regen", action="store_true", help="처리하지 않은 로그를 사유 · 계획이 TODO 인 초안 항목으로 목록에 더한다")
    parser.add_argument("--reports", action="append", default=[], type=Path, help="보고 폴더(여러 번 가능). 없으면 build/*/Bin/Saved/Automation 과 Saved/Automation")
    parser.add_argument("--allowlist", type=Path, default=None, help=f"허용 목록 파일(기본 {kAllowListRelPath})")
    parser.add_argument("--out", type=Path, default=None, help="--regen 이 쓸 파일(기본: 허용 목록 자신)")
    parser.add_argument("--dry-run", action="store_true", dest="bDryRun", help="--regen 이 파일을 쓰지 않고 찍기만 한다")
    args = parser.parse_args(argv)

    repositoryRoot = getProjectRoot()
    allowListPath = args.allowlist or repositoryRoot / kAllowListRelPath
    listFolder = args.reports or discoverReportFolders(repositoryRoot)
    listReport = findReports(listFolder)
    if not listReport:
        print(f"[ScenarioLogs] 시나리오 보고가 없어 판정할 수 없습니다(찾은 폴더: {', '.join(folder.as_posix() for folder in listFolder) or '없음'}). "
              "시나리오를 -scenario-report 와 함께 돌린 뒤 다시 부른다", file=sys.stderr)
        return int(ExitCode.CheckInvalid)
    try:
        data = json.loads(allowListPath.read_text(encoding="utf-8-sig"))
        listEntry = readAllowEntries(data)
    except (OSError, ValueError) as error:
        print(f"[ScenarioLogs] 허용 목록 {allowListPath.as_posix()} 를 읽지 못했다: {error}", file=sys.stderr)
        return int(ExitCode.CheckInvalid)
    if args.check:
        return runCheck(listEntry, listReport)
    return runRegen(allowListPath, args.out or allowListPath, listReport, args.bDryRun)


if __name__ == "__main__":
    sys.exit(main())
