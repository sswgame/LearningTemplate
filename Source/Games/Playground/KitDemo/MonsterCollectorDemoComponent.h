/**
 * @file MonsterCollectorDemoComponent.h
 * @brief 장르 키트 시연 — 몬스터 수집(`GF_MonsterCollector`)의 전투 한 판과 포획 한 판입니다. 야생 개체를 쓰러뜨려 경험치를 받고, 다른 야생 개체를 볼로 잡습니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/playground/automation/kit_monstercollector.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/RPG/MonsterCollector/MonsterCollectorCatalog.h"

#include "Games/Playground/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class MonsterCollectorDemoComponent
     * @brief 빠른 개체가 체력 1 인 야생 개체를 한 번에 쓰러뜨리는 전투와, 포획률 255 · 볼 x3 으로 반드시 잡히는 포획을 차례로 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Monster Collector Demo", Tooltip = "Runs one MonsterCollector kit wild battle (win, exp) and one capture (ball shakes) and keeps the result" )
    class MonsterCollectorDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        MonsterCollectorDemoComponent();
        virtual ~MonsterCollectorDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getWonExp() const { return _wonExp; }
        int32 getCaptured() const { return _captured; }
        int32 getCaptureShakes() const { return _captureShakes; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 전투와 포획을 돌리고 결과를 담습니다. 개체를 만들지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runBattleAndCapture();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Monster collector catalog XML (moves, species)" )
        AssetRef<MonsterCollectorCatalog> _catalogPath;

        SharedAssetRef<MonsterCollectorCatalog> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _wonExp; ///< 이긴 전투에서 받은 경험치(이기지 못하면 0)
        PROPERTY( RuntimeState )
        int32 _captured; ///< 포획 판이 잡음으로 끝났으면 1
        PROPERTY( RuntimeState )
        int32 _captureShakes; ///< 포획 판의 볼 흔들림 수
    };
} // namespace sw
