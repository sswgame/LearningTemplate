/**
 * @file Games/VoxelCraft/Dev/VoxelAutoplay.cpp
 * @brief VoxelCraft 의 자동 플레이 개발 스위치입니다 — 켜는 전역 변수와 그 등록(`SW_GAME_AUTOPLAY`)을 듭니다.
 * @details 개발 전용 소스다(Dev/ — 배포 구성이 짓지 않는다). 게임 모드는 이 파일을 모르고 `GameModeComponent::isAutoPlayOn` 이 런타임 칸
 *          `IGameAutoplayQuery` 로 읽는다. 배포본의 자동 플레이는 씬 데이터(`_bAutoPlay`)뿐이다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Config/Dev/GameAutoplay.h"

namespace sw
{
    /** @brief `-gv_voxelAutoPlay=1` 이면 씬의 `_bAutoPlay` 가 꺼져 있어도 디렉터의 자동 플레이를 켭니다. 걷기, 점프, 부수기, 놓기를 AI 가 합니다. */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_voxelAutoPlay, 0, "VoxelCraft: 걷기 · 부수기 · 놓기도 AI 가 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_voxelAutoPlay, "VoxelCraft", "Walk, break and place blocks by AI" );
} // namespace sw
