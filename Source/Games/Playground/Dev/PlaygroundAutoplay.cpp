/**
 * @file Games/Playground/Dev/PlaygroundAutoplay.cpp
 * @brief Playground 의 자동 플레이 개발 스위치입니다 — 켜는 전역 변수와 그 등록(`SW_GAME_AUTOPLAY`)을 듭니다.
 * @details 개발 전용 소스다(Dev/ — 배포 구성이 짓지 않는다). 디렉터는 이 파일을 모르고 `GameModeComponent::isAutoPlayOn` 이 런타임 칸
 *          `IGameAutoplayQuery` 로 읽는다. 배포본의 자동 플레이는 씬 데이터(`_bAutoPlay`)뿐이다. 스위치는 게임에 하나라 지금 씬의 디렉터가 제 자동 플레이를 한다
 *          (복셀 광산은 걷기 · 부수기 · 놓기를 AI 조종자가 한다).
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Config/GameAutoplay.h"

namespace sw
{
    SW_TEST_GLOBAL_VARIABLE( int32, gv_playgroundAutoPlay, 0, "Playground: the active scene's game mode plays by itself (the voxel mine walks, breaks and places blocks by AI) (1=on)" );
    SW_GAME_AUTOPLAY( gv_playgroundAutoPlay, "Playground", "The active scene's game mode plays by itself (the voxel mine walks, breaks and places blocks by AI)" );
} // namespace sw
