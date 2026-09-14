#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
게이트 린트들이 아직 무언가를 잡는지 검사합니다 (음성 테스트).

**린트는 조용히 죽는다.** 정규식 한 글자, 경로 한 조각이 어긋나면 아무것도 못 잡게 되는데 결과는
"위반 0건" 이라 통과처럼 보인다. 이 저장소는 실제로 겪었다 — 죽은 사본 검사가 자기 독스트링을
참조로 세어 통과한 적이 있고, 소유 검사가 구조체 이름 다섯 개만 보고 있던 적도 있다.

**증거는 린트가 직접 든다.** 각 게이트 클래스가 `selfTestCases` 에 "이건 반드시 잡아야 한다" 는 조각을
적어 두고, 이 장치가 임시 트리에 그것을 써서 린트를 돌린다. 0 이 아닌 종료 코드가 나와야 통과다.
조각 표를 여기 모아 두지 않는 이유는 하나다 — **표가 둘이면 언제든 어긋난다.**

게이트가 `selfTestCases` 를 선언하지 않으면 이 검사가 "덮이지 않은 린트" 로 실패한다.
정말 조각을 만들 수 없는 린트는 `selfTestSkipReason` 에 이유를 적는다(이유 없는 예외는 없다).

**대상 목록도 두지 않는다** — `Scripts/lint/gate/` 에 있는 것이 게이트다. 목록을 적어 두면 새 게이트를
거기 넣는 걸 잊는 순간 그 게이트는 아무에게도 검사받지 않는다. 자리가 규칙이다.
폴더를 훑는 일은 `Scripts/lint/LintCatalog.py` 가 한다 — CMake 등록 파일을 만드는 쪽과 **같은 훑기**다.

  python Scripts/lint/selftest/CheckLintsAreAlive.py [--root <repo>] [--verbose]
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — 사촌 린트 패키지 · LintGate
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from common import useUtf8Stdout  # noqa: E402
from LintCatalog import LintScript, discoverLintScripts  # noqa: E402

#: CMake 등록 정보 — 게이트는 `LintGate` 클래스가 들고, 클래스가 없는 이쪽은 모듈이 든다
#: (`Scripts/lint/LintCatalog.py`). 영어인 이유는 ninja 가 찍는 줄이기 때문이다.
kLintBuildComment = "Checking that every gate lint still fails on a deliberately broken fixture..."
kLintTimeoutSeconds = 120

def runLintInternal(script: LintScript, root: Path, extraArgs: list[str]) -> tuple[int, str]:
    """린트를 **실제 진입점으로** 돌립니다 (import 가 아니라 프로세스로 — 전역 캐시 오염을 피한다)."""
    result = subprocess.run(
        [sys.executable, str(script.scriptPath), "--root", str(root), *extraArgs],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    return result.returncode, (result.stdout or "") + (result.stderr or "")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="게이트 린트 음성 테스트")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[3]))
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)
    useUtf8Stdout()

    errors: list[str] = []
    checkedCases = 0

    listGateScript = discoverLintScripts("gate")
    if not listGateScript:
        print("[CheckLintsAreAlive] Scripts/lint/gate/ 에 게이트가 하나도 없습니다 — 이 검사가 헛돌고 있습니다",
              file=sys.stderr)
        return 1

    for script in listGateScript:
        moduleName = script.name
        gateClass = script.gateClass
        if gateClass is None:
            errors.append(f"{moduleName}: `LintGate` 를 상속한 게이트 클래스가 없습니다 — "
                          f"`gate/` 에 있는 것은 게이트여야 합니다 (Scripts/lint/LintGate.py 참고)")
            continue

        cases = gateClass.selfTestCases
        if not cases:
            if gateClass.selfTestSkipReason:
                if args.verbose:
                    print(f"  [{moduleName}] 건너뜀 — {gateClass.selfTestSkipReason}")
                continue
            errors.append(f"{moduleName}: `selfTestCases` 가 없습니다 — 이 린트가 죽어도 아무도 모릅니다 "
                          f"(정말 못 만들면 `selfTestSkipReason` 에 이유를 적으세요)")
            continue

        for case in cases:
            checkedCases += 1
            caseName = case.get("name", "?")
            tempRoot = Path(tempfile.mkdtemp(prefix="swLintAlive"))
            try:
                for relPath, content in case["files"].items():
                    target = tempRoot / relPath
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_text(content, encoding="utf-8")

                code, output = runLintInternal(script, tempRoot, list(case.get("args", [])))

                if args.verbose:
                    print(f"  [{moduleName}/{caseName}] exit={code}")

                if code == 0:
                    errors.append(f"{moduleName}/{caseName}: 위반을 넣었는데 통과했습니다 — 이 검사는 죽었습니다\n"
                                  f"      출력: {output.strip().splitlines()[-1] if output.strip() else '(없음)'}")
            finally:
                shutil.rmtree(tempRoot, ignore_errors=True)

    if errors:
        print(f"[CheckLintsAreAlive] 문제 {len(errors)}건", file=sys.stderr)
        for error in errors:
            print(f"  {error}")
        return 1

    print(f"[CheckLintsAreAlive] OK ({checkedCases} cases across {len(listGateScript)} gates)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
