#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
시나리오를 차례로 새 프로세스로 돌린다 — 앞 실행이 디스크에 남긴 상태(예: 같은 `-gv_userSettingsFile` 의 사용자 설정)를 뒤 실행이 읽는 "재시작 뒤에도 남는가" 확인용이다.

    py -3 -m Scripts scenario-seq game/shooter3d/automation/sequence/rebindsave game/shooter3d/automation/sequence/rebindrestart --backend dx12 --config Release

위치 인자는 시나리오(`scenario` 명령이 받는 이름 · 경로)들이고, 나머지 옵션은 모든 시나리오에 그대로 넘긴다(`--backend` · `--config` · `--app` · `--timeout` · `--dry-run` …).
한 시나리오가 통과하지 못하면 뒤 시나리오는 돌리지 않는다(앞 실행이 남긴 상태를 전제하므로). 조립 · 실행 · 표는 `qa.ScenarioRun` 이 한다 — 이 명령은 순서만 정한다.
종료 코드는 `scenario` 와 같다: 0 모두 통과, 1 하나가 통과가 아님, 2 돌릴 수 없다.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import common  # noqa: E402,F401 — import 하면 콘솔이 UTF-8 이 된다(common/__init__.py)
from common.ExitCode import ExitCode  # noqa: E402
from qa import ScenarioRun  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="시나리오를 차례로 새 프로세스로 돌린다(앞 실행이 남긴 상태를 뒤 실행이 읽는다). 나머지 옵션은 scenario 명령과 같다.")
    parser.add_argument("listScenario", nargs="+", metavar="scenario", help="돌릴 순서대로 시나리오(둘 이상)")
    args, listRest = parser.parse_known_args(argv)
    if len(args.listScenario) < 2:
        print("[ScenarioSequence] 시나리오가 둘 이상이어야 한다(하나면 `scenario` 를 쓴다)", file=sys.stderr)
        return ExitCode.CheckInvalid
    for index, scenario in enumerate(args.listScenario):
        print(f"[ScenarioSequence] {index + 1}/{len(args.listScenario)} {scenario}")
        code = ScenarioRun.main([scenario, *listRest])
        if code != ExitCode.Passed:
            print(f"[ScenarioSequence] {scenario} 가 통과하지 못해 뒤 {len(args.listScenario) - index - 1}개를 돌리지 않는다", file=sys.stderr)
            return code
    return ExitCode.Passed


if __name__ == "__main__":
    sys.exit(main())
