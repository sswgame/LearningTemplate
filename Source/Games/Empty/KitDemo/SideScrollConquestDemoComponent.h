/**
 * @file SideScrollConquestDemoComponent.h
 * @brief 장르 키트 시연 — 횡스크롤 정복(`GF_SideScrollConquest`)의 판 한 바퀴입니다. 중립 전초기지와 적 요새에 창병을 세워 점령하고 승리하는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다.
 *          시나리오: `game/empty/automation/kit_sidescrollconquest.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Strategy/SideScrollConquest/ConquestCatalog.h"

#include "Games/Empty/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class SideScrollConquestDemoComponent
     * @brief 판을 열고 창병 둘을 전초기지 · 요새에 세워 적 거점이 없어질 때까지 고정 걸음을 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Side Scroll Conquest Demo", Tooltip = "Runs one SideScrollConquest kit match (capture outpost and keep, victory) and keeps the result" )
    class SideScrollConquestDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        SideScrollConquestDemoComponent();
        virtual ~SideScrollConquestDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getVictory() const { return _victory; }
        int32 getTerritory() const { return _territory; }
        int32 getSquadSize() const { return _squadSize; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 판 한 바퀴를 돌리고 결과를 담습니다. 병종을 모르면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runMatch();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Side scroll conquest catalog XML (rules, units, sites)" )
        AssetRef<ConquestCatalog> _catalogPath;

        SharedAssetRef<ConquestCatalog> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _victory; ///< 1 승리, 0 아님
        PROPERTY( RuntimeState )
        int32 _territory; ///< 판이 끝난 뒤 플레이어 거점 수
        PROPERTY( RuntimeState )
        int32 _squadSize; ///< 지휘관 부대의 병사 수
    };
} // namespace sw
