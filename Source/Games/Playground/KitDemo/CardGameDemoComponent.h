/**
 * @file CardGameDemoComponent.h
 * @brief 장르 키트 시연 — 카드 게임(`GF_CardGame`)의 덱 빌딩 전투(`CardBattle`) 한 판입니다. 공격 카드 덱으로 적 하나를 두 턴 만에 쓰러뜨리는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          키트의 다른 게임(클론다이크 · 맞고 · 포커 · 셰딩 카드 게임)은 에셋 카탈로그가 없어 덱 카탈로그(`.deckbattle.xml`)를 쓰는 전투 하나만 시연한다.
 *          시나리오: `game/playground/automation/kit_cardgame.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Casual/CardGame/CardBattle/CardBattle.h"

#include "Games/Playground/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class CardGameDemoComponent
     * @brief 공격 카드 다섯 장으로 전투를 열고, 턴마다 낼 수 있는 패를 모두 낸 뒤 턴을 마쳐 적을 쓰러뜨리는 한 판을 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Card Game Demo", Tooltip = "Runs one CardGame kit deck battle (play cards, enemy intent, win) and keeps the result" )
    class CardGameDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        CardGameDemoComponent();
        virtual ~CardGameDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getPhase() const { return _phase; }
        int32 getTurn() const { return _turn; }
        int32 getPlayerHP() const { return _playerHP; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 전투 한 판을 돌리고 결과를 담습니다. 전투를 열지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runBattle();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Deck battle card catalog XML" )
        AssetRef<CardBattleCatalog> _catalogPath;

        SharedAssetRef<CardBattleCatalog> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _phase; ///< 끝난 단계(`CardBattlePhase` 0 내 턴, 1 승리, 2 패배)
        PROPERTY( RuntimeState )
        int32 _turn; ///< 끝난 턴 번호(1 부터)
        PROPERTY( RuntimeState )
        int32 _playerHP; ///< 끝난 뒤 내 체력
    };
} // namespace sw
