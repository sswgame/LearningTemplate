"""스크립트 종료 코드 — `main` 이 숫자 리터럴 대신 이 값을 돌려준다."""

from __future__ import annotations

from enum import IntEnum


class ExitCode(IntEnum):
    Passed = 0
    Violation = 1  # 일이 실패했거나 위반이 있다
    CheckInvalid = 2  # 인자 · 전제가 틀려 일을 시작하지 못했다
