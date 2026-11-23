/**
 * @file BattleRoyaleDemoComponent.h
 * @brief 장르 키트 시연 — 배틀로얄(`GF_BattleRoyale`)의 판 한 바퀴입니다. 두 팀을 세우고 자기장을 몇 걸음 돌린 뒤 한 팀을 전멸시켜 판이 끝나는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/playground/automation/kit_battleroyale.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/BattleRoyaleCatalog.h"

#include "Games/Playground/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class BattleRoyaleDemoComponent
     * @brief 팀 A(둘) 대 팀 B(하나)의 판을 열고 B 가 A 를 모두 쓰러뜨려 판이 끝나는 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Battle Royale Demo", Tooltip = "Runs one BattleRoyale kit match (zone ticks, knocks, team wipe, winner) and keeps the result" )
    class BattleRoyaleDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        BattleRoyaleDemoComponent();
        virtual ~BattleRoyaleDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getWinningTeam() const { return _winningTeam; }
        int32 getWinnerKills() const { return _winnerKills; }
        int32 getRemainingPlayers() const { return _remainingPlayers; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 판 한 바퀴를 돌리고 결과를 담습니다. 판을 열지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runMatch();

    private:
        SharedAssetRef<BattleRoyaleCatalog> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _winningTeam; ///< 이긴 팀 번호(-1 없음)
        PROPERTY( RuntimeState )
        int32 _winnerKills; ///< 이긴 사람의 처치 수
        PROPERTY( RuntimeState )
        int32 _remainingPlayers; ///< 판이 끝난 뒤 남은 사람
    };
} // namespace sw
