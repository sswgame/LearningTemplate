/**
 * @file DevCommandsEnabled.h
 * @brief 개발 명령 · 디버그 HUD 섹션이 컴파일되는 구성인지(`SW_DEV_COMMANDS_ENABLED`)만 정합니다.
 * @details 레지스트리 헤더를 끌어오지 않고 이 값만 보려는 헤더가 include 한다. `#if` 로 읽으므로 반드시 이 헤더를 거친다(정의 안 된 이름은 0 이 된다).
 */
#pragma once

/** @brief 개발 명령이 컴파일되는 구성이면 1 입니다(Dev). Shipping 은 0 이고 명령을 등록하지 않으며 콘솔이 없습니다(레지스트리 선언 · 명령 본문은 모든 구성에서 컴파일된다). */
#if defined( SW_SHIPPING )
    #define SW_DEV_COMMANDS_ENABLED 0
#else
    #define SW_DEV_COMMANDS_ENABLED 1
#endif
