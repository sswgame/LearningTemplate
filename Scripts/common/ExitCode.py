"""
스크립트 종료 코드의 한 자리 — 값에 이름을 붙여 부르는 쪽(CTest · 훅 · 세션 가드)과 내는 쪽이 같은 뜻을 쓴다.

- 린트 게이트의 결과는 `통과(0) | 건너뜀(77) | 불성립(5)` 에 위반(1) · 검사 오류(2 — 규칙 데이터 · 입력 파일이 깨졌다)가 더해진다 — 건너뜀과 불성립은 통과와 **종료 코드부터 다르다**
  (훅과 CTest 가 이 값으로 가른다). 불성립(`PrerequisiteMissing`, 5)은 필수 입력(빌드 폴더 · 도구 · 훑을 파일)이 없어 검사를
  하지 못한 것이다 — 통합(CTest)에서는 실패이고, 빌드 없는 PC 의 커밋 훅에서만 건너뜀으로 허용한다.
- 린트 밖 스크립트도 같은 값을 쓴다 — `Violation`(1)은 일이 실패했거나 위반이 있다는 것, `CheckInvalid`(2)는 인자 · 전제가 틀려 일을 시작하지 못한 것이다.
  `main` 과 `sys.exit(f())` 의 `f` 는 숫자 리터럴이 아니라 이 값을 돌려준다.
- `Skipped`(77)는 CTest 의 `SKIP_RETURN_CODE` 다. App 이 "이 기계에서 이 백엔드를 못 돌린다" 로 끝날 때도 같은 값이다. 린트 게이트에서는 `--files` 로 좁힌 훑기에 볼 것이 없을 때만 낸다.
  App 쪽 정본은 C++ `RHIInitResult.h` 의 `kRHIUnusableHereExitCode` 이고 `common/AppRun.kAppRhiUnusableExitCode` 가 그 줄을 읽는다.
  이 모듈은 헤더를 읽지 않으므로 `TestQaRunners` 가 `Skipped` 와 헤더 값(과 CMake 의 `SKIP_RETURN_CODE`)이 같은지 본다.
- `LimitReached`(3)는 기다림 · 요청 한도에 닿아 일을 못 한 것이다(GitHub API 한도, 공유 잠금 대기 시한).
"""

from __future__ import annotations

from enum import IntEnum


class ExitCode(IntEnum):
    """종료 코드. `sys.exit(ExitCode.Passed)` · `return ExitCode.Violation` 처럼 쓴다(정수와 같게 비교된다)."""

    Passed = 0
    Violation = 1
    CheckInvalid = 2
    LimitReached = 3
    PrerequisiteMissing = 5
    Skipped = 77
