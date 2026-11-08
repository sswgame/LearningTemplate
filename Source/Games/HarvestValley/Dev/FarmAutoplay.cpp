/**
 * @file Games/HarvestValley/Dev/FarmAutoplay.cpp
 * @brief HarvestValley 의 자동 플레이 개발 스위치입니다 — 켜는 전역 변수와 그 등록(`SW_GAME_AUTOPLAY`)을 듭니다.
 * @details 개발 전용 소스다(Dev/ — 배포 구성이 짓지 않는다). 게임 모드는 이 파일을 모르고 `GameModeComponent::isAutoPlayOn` 이 런타임 칸
 *          `IGameAutoplayQuery` 로 읽는다. 배포본의 자동 플레이는 씬 데이터(`_bAutoPlay`)뿐이다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Config/GameAutoplay.h"

namespace sw
{
    /**
     * @brief `-gv_farmAutoPlay=1`: 디렉터의 자동 농부를 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도 켭니다). 입력 없이 계절을 넘겨 보는 확인용입니다.
     * @details `App -gv_farmAutoPlay=1 -gv_profileFrames=36000` 은 약 10분이며 하루 다섯 번이다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_farmAutoPlay, 0, "HarvestValley: 농부도 AI 가 조종 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_farmAutoPlay, "HarvestValley", "The auto farmer AI controller possesses the farmer" );
} // namespace sw
