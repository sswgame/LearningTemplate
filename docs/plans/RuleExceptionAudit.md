# 규칙 예외 감사에서 정한 일의 적용

> 상태: 끝 · 선행: 없음 · 영역: project · 옛번호: C-11 · 진행: 4/4

2026-10-07 규칙 예외 감사에서 정한 넷([결정 기록](../09_Decisions.md) 5-2 "규칙 예외 감사에서 정한 넷")의 적용 상태다. 결정과 근거는 결정 기록에 있고 여기는 적용 단위만 적는다.

## 1. 단위

| 단위 | 내용 | 상태 |
|---|---|---|
| E1 | 백엔드 명령줄 철자는 `-dx12` · `-dx11` · `-vk` · `-gl` 하나씩(쿠킹 표 `command_line_name` 이 정본) | 끝남(결정 기록 5-2 "App 의 백엔드 명령줄 철자는 백엔드마다 하나입니다", 2026-10-07) |
| E2 | `(void)` 와 이유 주석은 `[[nodiscard]]` 를 붙인 실패 가능 함수에만 요구한다 | 끝남(`CheckDiscardReason` 이 실패 가능 동사의 결과만 보고 그 밖의 `(void)` 는 규칙 밖 — `lint/DiscardReasonRules.py`, `RunDiscardReasons`) |
| E3 | 두 플랫폼이 같은 명시적 경고 목록을 쓴다 | 적용함(2026-10-24). `clang-cl -### -Wall`(LLVM 20.1.8)의 출력이 `-Weverything` 이었다(`-clang:-Wall -clang:-Wextra` 는 `-Wall -Wextra` 로 나온다). `Clang.cmake` 가 clang-cl 에는 `-clang:-Wall -clang:-Wextra`, GNU 드라이버에는 `-Wall -Wextra` 를 넘기고 `GCC.cmake` 와 같은 집합이 된다. Windows 의 경고 수는 줄어든다(`-Werror=*` 는 그대로). |
| E4 | 출처를 모르는 리소스를 저장소에서 생성한 것이나 CC0 로 확인한 것으로 바꾼다 | 끝남(2026-10-24). 목록을 만들 필요가 없었다 — `CheckResourceCredits` 게이트가 이미 `Resource/` 의 원본 자산(2,197 개)을 크레딧 표 · 출처 목록과 대조하고, 예외 표(`rules/CheckResourceCredits.toml`)도 없다. 출처를 모르는 파일은 0 개다 |

## 2. 정리

- 결정 기록 5-2 의 "규칙 예외 감사에서 정한 넷" 줄에 E3 적용을 적었다(E4 는 게이트가 이미 막는다).
