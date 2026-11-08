/**
 * @file Games/AbilityArena/Dev/ArenaAutoplay.cpp
 * @brief AbilityArena 의 자동 플레이 개발 스위치입니다 — 켜는 전역 변수와 그 등록(`SW_GAME_AUTOPLAY`)을 듭니다.
 * @details 개발 전용 소스다(Dev/ — 배포 구성이 짓지 않는다). 게임 모드는 이 파일을 모르고 `GameModeComponent::isAutoPlayOn` 이 런타임 칸
 *          `IGameAutoplayQuery` 로 읽는다. 배포본의 자동 플레이는 씬 데이터(`_bAutoPlay`)뿐이다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Config/Dev/GameAutoplay.h"

namespace sw
{
    /**
     * @brief `-gv_arenaAutoPlay=1`: 디렉터의 자동 전투를 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도 켭니다). 플레이어도 AI 가 움직입니다.
     * @details `App -gv_arenaAutoPlay=1 -gv_profileFrames=1200` 처럼 쓴다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_arenaAutoPlay, 0, "AbilityArena: 플레이어도 AI 가 조종 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_arenaAutoPlay, "AbilityArena", "The auto battle AI controller possesses the player" );
} // namespace sw
