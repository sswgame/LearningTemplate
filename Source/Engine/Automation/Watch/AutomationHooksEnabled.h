/**
 * @file AutomationHooksEnabled.h
 * @brief 기능 폴더의 자동화 등록(감시 값 · 단계 · 막는 조건 · 단계 되돌리기)이 등록되는 구성인지(`SW_AUTOMATION_HOOKS_ENABLED`)만 정합니다.
 * @details 등록을 읽는 쪽은 자동화 실행기(`SW_WITH_AUTOMATION` — 배포 구성의 GPU 검증도 돈다)와 개발 구성의 도구(디버그 HUD 감시 값 · 에디터 자체 테스트)다.
 *          둘 다 없는 구성(자동화를 뺀 배포본)은 등록하지 않고 본문 함수를 `[[maybe_unused]]` 상수로만 붙잡는다 — 본문은 모든 구성에서 컴파일되고(썩지 않는다)
 *          부르는 곳이 없어 배포 바이너리에서 빠진다. 개발 명령(`DevCommandsEnabled.h`)과 같은 방식이다(`docs/agents/BuildConfigSwitch.md`).
 *          `#if` 로 읽으므로 반드시 이 헤더를 거친다(정의 안 된 이름은 `-Werror=undef` 가 멈춘다).
 */
#pragma once

#if !defined( SW_SHIPPING ) || defined( SW_WITH_AUTOMATION )
    #define SW_AUTOMATION_HOOKS_ENABLED 1
#else
    #define SW_AUTOMATION_HOOKS_ENABLED 0
#endif
