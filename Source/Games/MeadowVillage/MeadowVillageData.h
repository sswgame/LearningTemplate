/**
 * @file MeadowVillageData.h
 * @brief 두 디렉터가 같은 판을 여는 공유 상태 설정과 게임이 쓰는 이름입니다 — 어느 디렉터가 먼저 열어도 같은 값.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Input/Map/CommonInputActions.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerProgressComponent.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestCatalog.h"

namespace sw
{
    /** @struct MeadowVillageData @brief 하루 2 분(1 초 = 게임 12 분), 계절 넷 × 28 일, 공유 땅 40 × 40 칸. 카탈로그는 게임 인스턴스가 게임 범위에 건 것. */
    struct MeadowVillageData
    {
        static constexpr const utf8* kFastForwardAction = CommonInputActions::kFastForward; ///< 누르는 동안 8 배속
        static constexpr const utf8* kTalkAction        = CommonInputActions::kInteract;    ///< 생물과 대화(하루 한 번)
        static constexpr const utf8* kHarvestedFlag     = "farm.harvested";                 ///< 밭이 거둔 수(공유 플래그)
        static constexpr const utf8* kSproutSpecies     = "sprout";
        static constexpr const utf8* kRequestQuest      = "meadow_request";
        static constexpr int32       kLandSize          = 40;    ///< 공유 땅 한 변의 칸 수(칸 1 m — 밭 · 마을 · 집 부지를 덮는다)
        static constexpr float32     kLandHalfExtent    = 20.0f; ///< 땅이 월드 원점을 가운데에 두도록 원점을 이만큼 뺀다(m)

        static GameStateSettings makeStateSettings()
        {
            GameStateSettings settings;
            settings._clock._listSeason    = { "Spring", "Summer", "Fall", "Winter" };
            settings._clock._secondsPerDay = 120.0f;
            settings._clock._daysPerSeason = 28;
            settings._clock._startHour     = 6.0f;
            // 공유 땅 — 하우징 부지가 칸을 차지한다(밭 · 마을 자리와 겹치지 않게 씬에서 둔다). 칸 1 m 는 하우징 카탈로그의 칸과 같아야 한다.
            settings._landWidth    = kLandSize;
            settings._landHeight   = kLandSize;
            settings._landCellSize = 1.0f;
            settings._landOrigin   = float3{ -kLandHalfExtent, 0.0f, -kLandHalfExtent };
            return settings;
        }

        /** @brief 플레이어 진행(지갑 · 부탁 일지 · 생물 호감도)의 설정입니다. */
        static PlayerProgressSettings makeProgressSettings()
        {
            PlayerProgressSettings settings;
            settings._pQuestCatalog      = game::findSubsystem<const QuestCatalog>();
            settings._pReputationCatalog = game::findSubsystem<const ReputationCatalog>(); // 생물 호감도 = 세력 creature.<종>
            return settings;
        }
    };
} // namespace sw
