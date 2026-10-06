#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
실패 가능 함수(load · read · write · apply … — `CheckFallibleNodiscard` 의 동사)의 결과를 `(void)` 로 버린 자리와 그 이유를 모은다(보고만 한다).

[왜 필요한가 — 이유는 낡는다]
`CheckDiscardReason` 은 이유 **한 줄이 있는지**만 본다. "피호출자가 로그를 남긴다" 는 이유는 피호출자가 바뀌면 거짓이 되고, 같은 함수를
열 곳이 버리면 그 함수의 실패 길이 설계부터 틀렸을 수 있다. 이 보고서는 버린 자리를 **함수 이름별**로 묶어 이유를 나란히 보여 준다 —
같은 함수를 여러 곳이 버리면 그 함수가 실패를 스스로 처리하게(또는 실패할 수 없게) 바꿀 후보다. `— 결함 의심` 이 붙은 이유는 따로 센다.

[규칙의 범위]
이유가 필수인 것은 실패 가능 동사의 결과뿐이다(사용자 결정 2026-10-07 — 상용 엔진도 `[[nodiscard]]` 를 붙인 것만 "버림" 을 문제 삼는다).
그 밖의 `(void)` (값을 돌려주는 조회 · 쓰지 않는 인자 표시)는 세지 않는다.

사용법:
  py -3 Scripts/lint/report/RunDiscardReasons.py                      # 함수 이름별 상위 40 · 폴더별 수 · 결함 의심
  py -3 Scripts/lint/report/RunDiscardReasons.py --filter Source/Engine --top 100
  py -3 Scripts/lint/report/RunDiscardReasons.py --out discard.txt    # 줄마다 파일:줄 · 이름 · 이유
"""

from __future__ import annotations

import argparse
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — LintReport · gate

from common import collectRepositoryFiles, kLintTargetRelDirs, readTextFiles  # noqa: E402
from gate.CheckDiscardReason import findFallibleDiscards  # noqa: E402
from LintReport import LintReport, ReportContext  # noqa: E402

kSuffixes = (".h", ".hpp", ".inl", ".c", ".cc", ".cpp", ".cxx")
kSuspectMarker = "결함 의심"


class RunDiscardReasonsReport(LintReport):
    description = "실패 가능 함수의 결과를 (void) 로 버린 자리와 이유를 함수 이름별로 모은다"
    bUsesFilter = True
    bUsesOut = True

    def addArguments(self, parser: argparse.ArgumentParser) -> None:
        parser.add_argument("--top", type=int, default=40, help="함수 이름을 몇 개까지 찍을지")

    def produce(self, context: ReportContext, args: argparse.Namespace) -> int:
        repositoryRoot = context.repositoryRoot
        listPath = collectRepositoryFiles(repositoryRoot, kLintTargetRelDirs, suffixes=kSuffixes)
        mapNameToSite: dict[str, list[tuple[str, str]]] = defaultdict(list)
        countByFolder: Counter[str] = Counter()
        listSuspect: list[str] = []
        listRow: list[str] = []
        for path, text in readTextFiles(listPath, mustContain="void"):
            relative = path.relative_to(repositoryRoot).as_posix()
            if args.filter and args.filter not in relative:
                continue
            for lineNumber, name, reason in findFallibleDiscards(text):
                site = f"{relative}:{lineNumber}"
                mapNameToSite[name].append((site, reason))
                countByFolder["/".join(relative.split("/")[:2])] += 1
                listRow.append(f"{site}\t{name}\t{reason or '(이유 없음)'}")
                if kSuspectMarker in reason:
                    listSuspect.append(f"{site}: {name} — {reason}")

        total = sum(len(listSite) for listSite in mapNameToSite.values())
        print(f"[RunDiscardReasons] 실패 가능 결과를 버린 자리 {total} · 함수 {len(mapNameToSite)} · 결함 의심 {len(listSuspect)}")
        print("\n== 폴더별 ==")
        for folder, count in countByFolder.most_common():
            print(f"  {count:5}  {folder}")
        print(f"\n== 함수 이름별 (상위 {args.top}) — 여러 곳이 버리는 함수는 실패를 스스로 처리하게 바꿀 후보 ==")
        for name, listSite in sorted(mapNameToSite.items(), key=lambda item: -len(item[1]))[:args.top]:
            uniqueReason = sorted({reason or "(이유 없음)" for _, reason in listSite})
            print(f"  {len(listSite):4}  {name}")
            for reason in uniqueReason[:3]:
                print(f"          · {reason}")
            if len(uniqueReason) > 3:
                print(f"          · … 이유 {len(uniqueReason) - 3} 개 더")
        if listSuspect:
            print("\n== 결함 의심 ==")
            for line in listSuspect:
                print(f"  {line}")
        if args.out:
            Path(args.out).write_text("\n".join(listRow) + "\n", encoding="utf-8")
            print(f"\n[RunDiscardReasons] 줄 목록: {args.out}")
        return 0


main = RunDiscardReasonsReport.run

if __name__ == "__main__":
    sys.exit(main())
