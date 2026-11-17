/**
 * @file CoopScavengerDemoComponent.h
 * @brief 장르 키트 시연 — 협동 수집 공포(`GF_CoopScavenger`)의 원정 한 번입니다. 위성에 내려 바닥 고철을 모두 우주선에 싣고, 회사에서 팔아 할당량을 채우는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/empty/automation/kit_coopscavenger.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Catalog/ScavengerCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    class ScavengerExpedition;

    /**
     * @class CoopScavengerDemoComponent
     * @brief 두 사람의 원정을 열고 착륙 → 고철 나르기 → 이륙 → 회사에서 매입 → 할당량 판정까지 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Coop Scavenger Demo", Tooltip = "Runs one CoopScavenger kit expedition (land, haul scrap, sell at the company, meet the quota) and keeps the result" )
    class CoopScavengerDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        CoopScavengerDemoComponent();
        virtual ~CoopScavengerDemoComponent() override;

        void serializeState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getDemoState() const { return _demoState; }
        int32 getSoldValue() const { return _soldValue; }
        int32 getCredits() const { return _credits; }
        int32 getNextQuota() const { return _nextQuota; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 원정 한 바퀴를 돌리고 결과를 담습니다. 원정을 열지 못하면 false 입니다. */
        [[nodiscard]] bool runExpedition();
        /** @brief @p player 를 지날 수 있는 가장 짧은 길로 @p target 까지 옮깁니다. 길이 없으면 false 입니다. */
        [[nodiscard]] static bool walkTo( ScavengerExpedition& inoutExpedition, int32 player, const hashed_string& target );

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Coop scavenger catalog XML (quota, carry, facility, scrap, moons)" )
        AssetRef<ScavengerCatalog> _catalogPath;

        SharedAssetRef<ScavengerCatalog> _pCatalog;
        int32                            _demoState; ///< 0 시작 전, 1 한 바퀴를 마침
        int32                            _soldValue; ///< 회사에서 판 값
        int32                            _credits;   ///< 할당량 판정 뒤의 크레딧
        int32                            _nextQuota; ///< 다음 주기의 할당량
    };
} // namespace sw
