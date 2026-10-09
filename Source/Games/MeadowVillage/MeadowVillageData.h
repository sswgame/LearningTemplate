/**
 * @file MeadowVillageData.h
 * @brief 두 디렉터가 같은 판을 여는 공유 상태 설정과 게임이 쓰는 이름입니다 — 어느 디렉터가 먼저 열어도 같은 값.
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Base/Gameplay/Progression/Reputation.h"
#include "GameFramework/Base/Gameplay/Quest/QuestCatalog.h"

namespace sw
{
    /** @struct MeadowVillageData @brief 하루 2 분(1 초 = 게임 12 분), 계절 넷 × 28 일. 카탈로그는 게임 인스턴스가 로컬 서비스로 건 것. */
    struct MeadowVillageData
    {
        static constexpr const utf8* kFastForwardAction = "Village.FastForward"; ///< 누르는 동안 8 배속
        static constexpr const utf8* kTalkAction        = "Town.Talk";           ///< 생물과 대화(하루 한 번)
        static constexpr const utf8* kHarvestedFlag     = "farm.harvested";      ///< 밭이 거둔 수(공유 플래그)
        static constexpr const utf8* kSproutSpecies     = "sprout";
        static constexpr const utf8* kRequestQuest      = "meadow_request";

        static GameStateSettings makeStateSettings()
        {
            GameStateSettings settings;
            settings._clock._listSeason    = { "Spring", "Summer", "Fall", "Winter" };
            settings._clock._secondsPerDay = 120.0f;
            settings._clock._daysPerSeason = 28;
            settings._clock._startHour     = 6.0f;
            settings._pQuestCatalog        = game::getService<QuestCatalog>();
            settings._pReputationCatalog   = game::getService<ReputationCatalog>(); // 생물 호감도 = 세력 creature.<종>
            return settings;
        }
    };
} // namespace sw
