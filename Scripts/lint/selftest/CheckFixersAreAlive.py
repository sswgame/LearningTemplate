#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
픽서들이 아직 무언가를 고치는지, 그리고 **고치면 안 되는 것을 안 고치는지** 검사합니다 (음성 테스트).

`CheckLintsAreAlive` 는 `gate/` 만 본다. 그런데 `fixer/` 도 `--check` 에서 0 이 아닌 값을 주는
게이트이고(CLAUDE.md 의 폴더 표), 게다가 **게이트보다 더 조용히 망가진다**:

- 게이트가 죽으면 "위반 0건" 이라 통과처럼 보인다. 나쁘다.
- 픽서가 죽으면 마찬가지로 조용하다. 그런데 픽서가 **너무 많이 잡으면** 빨간 줄이 뜨는 게 아니라
  **소스가 바뀐다.** `py -3 -m Scripts format` 은 969개 파일을 한 번에 고쳐 쓴다.

그래서 변환마다 조각을 **둘** 요구한다:

- `badSample`  — 이 변환이 반드시 고쳐야 하는 것. 안 고치면 그 변환은 죽었다.
- `goodSample` — 이 변환이 건드리면 안 되는 것. 고치면 규칙이 너무 넓어진 것이다.

조각은 `FixPass` 안, 변환 바로 옆에 있다 — 게이트가 `selfTestCases` 를 드는 것과 같은 이유다.

**게이트와 달리 프로세스를 띄우지 않는다.** 픽서의 변환은 `(텍스트) -> (새 텍스트, 바뀌었는가)`
순수 함수라 그냥 부르면 된다. 임시 트리도, 서브프로세스도 필요 없다.

  python Scripts/lint/selftest/CheckFixersAreAlive.py [--root <repo>] [--verbose]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))   # Scripts/lint — 사촌 린트 패키지
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))   # Scripts — common

from common import useUtf8Stdout  # noqa: E402
from LintCatalog import discoverLintScripts  # noqa: E402
from LintFixer import findFixerClass  # noqa: E402

#: CMake 등록 정보 — 게이트는 `LintGate` 클래스가 들고, 클래스가 없는 이쪽은 모듈이 든다
#: (`Scripts/lint/LintCatalog.py`). 영어인 이유는 ninja 가 찍는 줄이기 때문이다.
kLintBuildComment = "Checking that every fixer still rewrites what it must and leaves the rest alone..."
kLintTimeoutSeconds = 30


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="픽서 음성 테스트")
    parser.add_argument("--root", type=Path, default=None, help="저장소 루트 (이 검사는 쓰지 않는다 — CTest 가 준다)")
    parser.add_argument("--verbose", action="store_true", help="변환마다 결과를 모두 출력")
    args = parser.parse_args(argv)
    useUtf8Stdout()

    errors: list[str] = []
    checkedPasses = 0

    listScript = discoverLintScripts("fixer")
    if not listScript:
        print("[CheckFixersAreAlive] Scripts/lint/fixer/ 에 픽서가 하나도 없습니다 — 이 검사가 헛돌고 있습니다",
              file=sys.stderr)
        return 1

    for script in listScript:
        fixerClass = findFixerClass(script.module)
        if fixerClass is None:
            skipReason = getattr(script.module, "kFixerSkipReason", "")
            if skipReason:
                if args.verbose:
                    print(f"  [{script.name}] 건너뜀 — {skipReason}")
                continue
            errors.append(f"{script.name}: `LintFixer` 를 상속한 픽서 클래스가 없습니다 — `fixer/` 에 있는 것은 "
                          f"픽서여야 합니다 (정말 아니면 `kFixerSkipReason` 에 이유를 적으세요)")
            continue

        if not fixerClass.listPass:
            errors.append(f"{script.name}: `listPass` 가 비어 있습니다 — 이 픽서는 아무것도 하지 않습니다")
            continue

        for fixPass in fixerClass.listPass:
            checkedPasses += 1
            label = f"{script.name}/{fixPass.done}"

            if not fixPass.badSample:
                errors.append(f"{label}: `badSample` 이 없습니다 — 이 변환이 죽어도 아무도 모릅니다")
            else:
                _, bChanged = fixPass.transform(fixPass.badSample)
                if args.verbose:
                    print(f"  [{label}] badSample changed={bChanged}")
                if not bChanged:
                    errors.append(f"{label}: 고쳐야 할 조각을 그냥 뒀습니다 — 이 변환은 죽었습니다")

            if not fixPass.goodSample:
                errors.append(f"{label}: `goodSample` 이 없습니다 — 오탐이 나도 아무도 모릅니다 "
                              f"(픽서의 오탐은 빨간 줄이 아니라 소스 변경입니다)")
            else:
                _, bChanged = fixPass.transform(fixPass.goodSample)
                if args.verbose:
                    print(f"  [{label}] goodSample changed={bChanged}")
                if bChanged:
                    errors.append(f"{label}: 건드리면 안 되는 조각을 고쳤습니다 — 규칙이 너무 넓습니다")

    if errors:
        print(f"[CheckFixersAreAlive] 문제 {len(errors)}건", file=sys.stderr)
        for error in errors:
            print(f"  {error}")
        return 1

    print(f"[CheckFixersAreAlive] OK ({checkedPasses} passes across {len(listScript)} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
