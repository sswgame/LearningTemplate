"""
스크립트 종료 코드의 한 자리 — 값에 이름을 붙여 부르는 쪽(CTest · 훅 · 세션 가드)과 내는 쪽이 같은 뜻을 쓴다.

- 린트(`lint/LintGate.run`)는 통과 · 위반 · 검사 불성립 셋만 낸다.
- `Skipped`(77)는 CTest 의 `SKIP_RETURN_CODE` 다. App 이 "이 기계에서 이 백엔드를 못 돌린다" 로 끝날 때도 같은 값이다.
  App 쪽 정본은 C++ `RHIInitResult.h` 의 `kRHIUnusableHereExitCode` 이고 `common/AppRun.kAppRhiUnusableExitCode` 가 그 줄을 읽는다.
  이 모듈은 헤더를 읽지 않으므로 `TestQaRunners` 가 `Skipped` 와 헤더 값(과 CMake 의 `SKIP_RETURN_CODE`)이 같은지 본다.
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
    Skipped = 77
