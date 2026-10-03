/**
 * @file RuntimeAPI/Service/ServiceListColumns.h
 * @brief 서비스 목록(`EngineServiceList.xxx` · `HostServiceList.xxx`)의 낱말 칸을 값으로 바꾸는 매크로입니다.
 * @details 목록의 칸은 `1, 1, 0` 같은 숫자가 아니라 낱말입니다(`Required, GameVisible, HostCreated`). `if constexpr` 처럼 값이
 *          필요한 자리는 여기 매크로로 바꿉니다. 모르는 낱말(오타)은 정의되지 않은 매크로 이름이 되어 컴파일 오류입니다.
 */
#pragma once
#include "Core/Common/Macros.h"

// requirement 칸: `Required` 면 areEngineServicesBound() 검사에 든다.

#define SW_SERVICE_IS_REQUIRED_Required       1
#define SW_SERVICE_IS_REQUIRED_Optional       0
#define SW_SERVICE_IS_REQUIRED( requirement ) SW_CONCAT( SW_SERVICE_IS_REQUIRED_, requirement )

// visibility 칸: `GameVisible` 이면 게임 모듈(SWGame)에 노출, `HostOnly` 면 호스트 · 에디터 전용.
#define SW_SERVICE_IS_GAME_VISIBLE_GameVisible   1
#define SW_SERVICE_IS_GAME_VISIBLE_HostOnly      0
#define SW_SERVICE_IS_GAME_VISIBLE( visibility ) SW_CONCAT( SW_SERVICE_IS_GAME_VISIBLE_, visibility )

// creator 칸: `EngineCreated` 면 `EngineOwnedServices` 가 만들고, `HostCreated` 면 호스트가 직접 만든다.
#define SW_SERVICE_IS_ENGINE_CREATED_EngineCreated 1
#define SW_SERVICE_IS_ENGINE_CREATED_HostCreated   0
#define SW_SERVICE_IS_ENGINE_CREATED( creator )    SW_CONCAT( SW_SERVICE_IS_ENGINE_CREATED_, creator )
