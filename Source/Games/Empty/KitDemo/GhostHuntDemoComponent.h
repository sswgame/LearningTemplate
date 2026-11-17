/**
 * @file GhostHuntDemoComponent.h
 * @brief 장르 키트 시연 — 유령 사냥(`GF_GhostHunt`)의 싸움 한 바퀴입니다. 유령 하나를 내어 손전등으로 기절시키고 청소기 흡입 · 서지로 잡아 동전을 받는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/empty/automation/kit_ghosthunt.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/GhostCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    /**
     * @class GhostHuntDemoComponent
     * @brief 유령 하나를 공격 동작까지 기다려 손전등으로 기절시키고, 흡입으로 서지 게이지를 채운 뒤 서지로 잡는 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Ghost Hunt Demo", Tooltip = "Runs one GhostHunt kit fight (beam stun, suction, surge, catch) and keeps the result" )
    class GhostHuntDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        GhostHuntDemoComponent();
        virtual ~GhostHuntDemoComponent() override;

        void serializeState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getDemoState() const { return _demoState; }
        int32 getStunnedCount() const { return _stunnedCount; }
        int32 getCaughtCoins() const { return _caughtCoins; }
        int32 getRemainingGhosts() const { return _remainingGhosts; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 싸움 한 바퀴를 돌리고 결과를 담습니다. 싸움을 열지 못하거나 흡입이 시작되지 않으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runFight();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Ghost hunt catalog XML (flashlight, vacuum, ghost)" )
        AssetRef<GhostCatalog> _catalogPath;

        SharedAssetRef<GhostCatalog> _pCatalog;
        int32                        _demoState;       ///< 0 시작 전, 1 한 바퀴를 마침
        int32                        _stunnedCount;    ///< 손전등이 기절시킨 유령 수
        int32                        _caughtCoins;     ///< 잡은 유령이 떨어뜨린 동전
        int32                        _remainingGhosts; ///< 싸움이 끝난 뒤 잡히지 않은 유령
    };
} // namespace sw
