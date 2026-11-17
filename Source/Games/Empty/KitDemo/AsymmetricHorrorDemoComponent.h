/**
 * @file AsymmetricHorrorDemoComponent.h
 * @brief 장르 키트 시연 — 비대칭 공포(`GF_AsymmetricHorror`)의 판 한 바퀴입니다. 생존자 하나가 발전기 둘을 고쳐 전원을 넣고 탈출문을 열어 나가 생존자 팀이 이기는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/empty/automation/kit_asymmetrichorror.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Horror/AsymmetricHorror/AsymmetricHorrorRules.h"
#include "GameFramework/Runtime/Foundation/Framework/GameModeComponent.h"

namespace sw
{
    /**
     * @class AsymmetricHorrorDemoComponent
     * @brief 생존자 하나 · 발전기 둘 · 탈출문 하나의 판을 열고, 수리 → 전원 → 문 열기 → 탈출로 판이 끝나는 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Asymmetric Horror Demo", Tooltip = "Runs one AsymmetricHorror kit match (repair, power, open gate, escape) and keeps the result" )
    class AsymmetricHorrorDemoComponent : public GameModeComponent
    {
    public:
        REFLECT_BODY();

        AsymmetricHorrorDemoComponent();
        virtual ~AsymmetricHorrorDemoComponent() override;

        void serializeState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getDemoState() const { return _demoState; }
        int32 getCompletedGenerators() const { return _completedGenerators; }
        int32 getEscapedCount() const { return _escapedCount; }
        int32 getWinningTeam() const { return _winningTeam; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateGame( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;

    private:
        /** @brief 판 한 바퀴를 돌리고 결과를 담습니다. 판을 열지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runMatch();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Asymmetric horror rules catalog XML (generator, endgame, killer)" )
        AssetRef<AsymmetricHorrorRulesCatalog> _catalogPath;

        SharedAssetRef<AsymmetricHorrorRulesCatalog> _pCatalog;
        int32                                        _demoState;           ///< 0 시작 전, 1 한 바퀴를 마침
        int32                                        _completedGenerators; ///< 고친 발전기 수
        int32                                        _escapedCount;        ///< 탈출한 생존자 수
        int32                                        _winningTeam;         ///< 이긴 팀 번호(-1 없음, 0 생존자)
    };
} // namespace sw
