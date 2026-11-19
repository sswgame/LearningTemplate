/**
 * @file MonsterHuntRPGDemoComponent.h
 * @brief 장르 키트 시연 — 몬스터 사냥(`GF_MonsterHuntRPG`)의 사냥 계약 한 바퀴입니다. 단서를 순서대로 찾아 계약을 풀고, 보상을 흥정하고, 대상을 처치해 도감 지식을 올립니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/empty/automation/kit_monsterhuntrpg.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/RPG/MonsterHuntRPG/MonsterHuntCatalog.h"

#include "Games/Empty/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class MonsterHuntRPGDemoComponent
     * @brief griffin 계약의 조사(단서 셋, 두 단계) → 흥정(한도 1 초과 요구는 맞제안, 한도와 같은 요구는 수락) → ghoul 두 번 처치(지식 1 단계)를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Monster Hunt RPG Demo", Tooltip = "Runs one MonsterHuntRPG kit contract (investigation, haggle, bestiary kills) and keeps the result" )
    class MonsterHuntRPGDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        MonsterHuntRPGDemoComponent();
        virtual ~MonsterHuntRPGDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getSolved() const { return _solved; }
        int32 getReward() const { return _reward; }
        int32 getKnowledge() const { return _knowledge; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 계약 한 바퀴를 돌리고 결과를 담습니다. 계약을 열지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runContract();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Monster hunt catalog XML (monster, contract)" )
        AssetRef<MonsterHuntCatalog> _catalogPath;

        SharedAssetRef<MonsterHuntCatalog> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _solved; ///< 조사가 풀렸으면 1
        PROPERTY( RuntimeState )
        int32 _reward; ///< 흥정으로 정한 보상(끝나지 않았으면 0)
        PROPERTY( RuntimeState )
        int32 _knowledge; ///< 처치 뒤 대상의 도감 지식 단계
    };
} // namespace sw
