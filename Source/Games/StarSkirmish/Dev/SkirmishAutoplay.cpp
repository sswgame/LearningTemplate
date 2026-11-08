/**
 * @file Games/StarSkirmish/Dev/SkirmishAutoplay.cpp
 * @brief StarSkirmish 의 자동 플레이 개발 스위치입니다 — 켜는 전역 변수와 그 등록(`SW_GAME_AUTOPLAY`)을 듭니다.
 * @details 개발 전용 소스다(Dev/ — 배포 구성이 짓지 않는다). 게임 모드는 이 파일을 모르고 `GameModeComponent::isAutoPlayOn` 이 런타임 칸
 *          `IGameAutoplayQuery` 로 읽는다. 배포본의 자동 플레이는 씬 데이터(`_bAutoPlay`)뿐이다.
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Config/GameAutoplay.h"

namespace sw
{
    /**
     * @brief `-gv_skirmishAutoPlay=1`: 디렉터를 컴퓨터 대 컴퓨터(러시 대 운영)로 엽니다(씬의 `_bAutoPlay` 가 꺼져 있어도 엽니다). 입력 없이 승패까지 가므로 확인과 화면 녹화에 씁니다.
     * @details `App -gv_skirmishAutoPlay=1` 로 실행하면 30초마다 `[Skirmish] t=.. p0 workers .. army ..` 가, 끝에는 승패가 로그에 남는다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_skirmishAutoPlay, 0, "StarSkirmish: 두 플레이어 모두 AI 로 돌리기 (1=켜기)" );
    SW_GAME_AUTOPLAY( gv_skirmishAutoPlay, "StarSkirmish", "Both players are AI" );
} // namespace sw
