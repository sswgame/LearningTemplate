/**
 * @file Engine/Foundation/EngineServiceListColumns.h
 * @brief 엔진 서비스 목록(`EngineServiceList.xxx`)의 낱말 칸을 값으로 바꾸는 매크로입니다.
 * @details 목록의 칸은 `1, 1, 0` 같은 숫자가 아니라 낱말입니다(`Required, GameVisible, HostCreated`). `if constexpr` 처럼 값이
 *          필요한 자리는 여기 매크로로 바꿉니다. 모르는 낱말(오타)은 정의되지 않은 매크로 이름이 되어 컴파일 오류입니다.
 */
#pragma once
#include "Core/Common/Macros.h"

// requirement 칸: `Required` 면 areEngineServicesBound() 검사에 든다.

#define SW_SERVICE_IS_REQUIRED_Required       1
#define SW_SERVICE_IS_REQUIRED_Optional       0
#define SW_SERVICE_IS_REQUIRED( requirement ) SW_CONCAT( SW_SERVICE_IS_REQUIRED_, requirement )

// visibility 칸: `GameVisible` 이면 게임 모듈(SWGame)에 노출, `HostOnly` 면 호스트 · 에디터 전용(게임 모듈의 범위에 걸지 않는다).
#define SW_SERVICE_IS_GAME_VISIBLE_GameVisible   1
#define SW_SERVICE_IS_GAME_VISIBLE_HostOnly      0
#define SW_SERVICE_IS_GAME_VISIBLE( visibility ) SW_CONCAT( SW_SERVICE_IS_GAME_VISIBLE_, visibility )
