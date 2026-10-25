"""
스크립트 종료 코드의 한 자리 — 값에 이름을 붙여 부르는 쪽(CTest · 훅 · 세션 가드)과 내는 쪽이 같은 뜻을 쓴다.

- 린트 게이트의 결과는 `통과(0) | 건너뜀(77) | 불성립(5)` 에 위반(1) · 검사 오류(2 — 규칙 데이터 · 입력 파일이 깨졌다)가 더해진다 — 건너뜀과 불성립은 통과와 **종료 코드부터 다르다**
  (`lint/LintGate.GateResult.skipped` · `unavailable`). 불성립(`PrerequisiteMissing`, 5)은 필수 입력(빌드 폴더 · 도구 · 훑을 파일)이 없어 검사를
  하지 못한 것이다 — 통합(`lint-suite` · CTest)에서는 실패이고, 빌드 없는 PC 의 커밋 훅에서만 건너뜀으로 허용한다.
- `Skipped`(77)는 CTest 의 `SKIP_RETURN_CODE` 다. App 이 "이 기계에서 이 백엔드를 못 돌린다" 로 끝날 때도 같은 값이다
  (`common/AppRun.kAppRhiUnusableExitCode`, C++ 쪽 `RHIInitResult.h`). 린트 게이트에서는 `--files` 로 좁힌 훑기에 볼 것이 없을 때만 낸다.
- `LimitReached`(3)는 기다림 · 요청 한도에 닿아 일을 못 한 것이다(GitHub API 한도, 공유 잠금 대기 시한).
- `CiFailing`(4)는 `ci-jobs --summary` 가 CI 실패를 알리는 값이다. 세션 가드 훅(`.claude/hooks/SessionGuard.py` 의 `kCiFailingExitCode`)은
  세션 설정이라 이 모듈을 import 하지 않고 값을 적어 두며, `TestDevWorktreeTools` 가 둘이 같은지 본다.
"""

from __future__ import annotations

from enum import IntEnum


class ExitCode(IntEnum):
    """종료 코드. `sys.exit(ExitCode.Passed)` · `return ExitCode.Violation` 처럼 쓴다(정수와 같게 비교된다)."""

    Passed = 0
    Violation = 1
    CheckInvalid = 2
    LimitReached = 3
    CiFailing = 4
    PrerequisiteMissing = 5
    Skipped = 77
