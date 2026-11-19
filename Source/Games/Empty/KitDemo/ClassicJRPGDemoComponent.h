/**
 * @file ClassicJRPGDemoComponent.h
 * @brief 장르 키트 시연 — 클래식 JRPG(`GF_ClassicJRPG`)의 전투 한 바퀴입니다. 용사 하나가 약한 적을 한 라운드에 쓰러뜨려 경험치와 골드를 받는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/empty/automation/kit_classicjrpg.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/RPG/ClassicJRPG/JRPGCatalog.h"

#include "Games/Empty/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class ClassicJRPGDemoComponent
     * @brief 용사 하나의 파티로 체력 1 인 적과 전투를 열고, 공격 한 번으로 이겨 보상(경험치 · 골드)이 파티와 지갑으로 가는 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Classic JRPG Demo", Tooltip = "Runs one ClassicJRPG kit battle (attack command, victory, rewards) and keeps the result" )
    class ClassicJRPGDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        ClassicJRPGDemoComponent();
        virtual ~ClassicJRPGDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getBattleResult() const { return _battleResult; }
        int32 getHeroExp() const { return _heroExp; }
        int32 getGold() const { return _gold; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 전투 한 바퀴를 돌리고 결과를 담습니다. 멤버를 넣지 못하거나 전투가 열리지 않으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runBattle();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Classic JRPG catalog XML (experience curve, class, enemy)" )
        AssetRef<JRPGCatalog> _catalogPath;

        SharedAssetRef<JRPGCatalog> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _battleResult; ///< `JRPGBattleResult` 값(0 진행, 1 승리, 2 패배, 3 도망)
        PROPERTY( RuntimeState )
        int32 _heroExp; ///< 용사가 받은 경험치
        PROPERTY( RuntimeState )
        int32 _gold; ///< 지갑의 기본 통화 잔액
    };
} // namespace sw
