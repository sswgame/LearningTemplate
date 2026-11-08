/**
 * @file Games/NileCity/Dev/NileAutoplay.cpp
 * @brief NileCity 의 자동 플레이 개발 스위치입니다 — 켜는 전역 변수와 그 등록(`SW_GAME_AUTOPLAY`)을 듭니다.
 * @details 개발 전용 소스다(Dev/ — 배포 구성이 짓지 않는다). 게임 모드는 이 파일을 모르고 `GameModeComponent::isAutoPlayOn` 이 런타임 칸
 *          `IGameAutoplayQuery` 로 읽는다. 배포본의 자동 플레이는 씬 데이터(`_bAutoPlay`)뿐이다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Config/GameAutoplay.h"

namespace sw
{
    /**
     * @brief `-gv_nileAutoPlay=1`: 디렉터의 자동 계획을 켭니다(씬의 `_bAutoPlay` 가 꺼져 있어도 켭니다). 입력 없이 도시가 커지는지 확인합니다.
     * @details 실행: `App -gv_nileAutoPlay=1`. 달마다 `[Nile] month N pop P money M` 이 로그에 남는다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_nileAutoPlay, 0, "NileCity: 자동 계획으로 도시를 짓고 돌리기 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_nileAutoPlay, "NileCity", "Build and run the city from the auto plan" );
} // namespace sw
