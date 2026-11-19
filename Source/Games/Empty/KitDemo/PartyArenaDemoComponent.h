/**
 * @file PartyArenaDemoComponent.h
 * @brief 장르 키트 시연 — 파티 아레나(`GF_PartyArena`)의 라운드 시리즈 한 바퀴입니다. 세 사람이 라운드를 거듭해 순위 점수를 쌓고 먼저 목표 점수에 닿은 사람이 우승하는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          라운드 안의 미니 게임은 돌리지 않고 고정 라운드 점수를 넘긴다(시리즈는 라운드 안에서 무슨 일이 있었는지 모른다).
 *          시나리오: `game/empty/automation/kit_partyarena.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyRoundSeries.h"

#include "Games/Empty/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class PartyArenaDemoComponent
     * @brief 카탈로그의 라운드 목록 · 순위 점수 · 우승 점수로 세 사람의 시리즈를 열고, 같은 라운드 점수를 우승자가 나올 때까지 넘기는 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Party Arena Demo", Tooltip = "Runs one PartyArena kit round series (round ranks, placement points, series winner) and keeps the result" )
    class PartyArenaDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        PartyArenaDemoComponent();
        virtual ~PartyArenaDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getWinner() const { return _winner; }
        int32 getWinnerTotal() const { return _winnerTotal; }
        int32 getRoundsPlayed() const { return _roundsPlayed; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 시리즈 한 바퀴를 돌리고 결과를 담습니다. 시리즈를 열지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runSeries();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Party round series XML (rounds, placement points, win score)" )
        AssetRef<PartyRoundSeries> _catalogPath;

        SharedAssetRef<PartyRoundSeries> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _winner; ///< 우승한 사람 번호(-1 없음)
        PROPERTY( RuntimeState )
        int32 _winnerTotal; ///< 우승한 사람의 총점
        PROPERTY( RuntimeState )
        int32 _roundsPlayed; ///< 결과를 넘긴 라운드 수
    };
} // namespace sw
