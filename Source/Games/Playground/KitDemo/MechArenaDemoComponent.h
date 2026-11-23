/**
 * @file MechArenaDemoComponent.h
 * @brief 장르 키트 시연 — 기체 대전(`GF_MechArena`)의 판 한 바퀴입니다. 1 대 1 로 세우고 B 를 두 번 격추해 팀 게이지가 바닥나 판이 끝나는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/playground/automation/kit_mecharena.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Action/MechArena/MechCatalog.h"

#include "Games/Playground/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class MechArenaDemoComponent
     * @brief 팀 A · B 에 무기 없는 기체를 하나씩 세우고, A 가 B 를 격추 → 리스폰 → 다시 격추해 B 팀 게이지(코스트 x 2)가 바닥나는 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Mech Arena Demo", Tooltip = "Runs one MechArena kit match (destroy, respawn, gauge out, winner) and keeps the result" )
    class MechArenaDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        MechArenaDemoComponent();
        virtual ~MechArenaDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getWinningTeam() const { return _winningTeam; }
        int32 getWinnerKills() const { return _winnerKills; }
        int32 getLoserGauge() const { return _loserGauge; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 판 한 바퀴를 돌리고 결과를 담습니다. 판을 열지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runMatch();

    private:
        SharedAssetRef<MechCatalog> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _winningTeam; ///< 이긴 팀 번호(-1 없음)
        PROPERTY( RuntimeState )
        int32 _winnerKills; ///< A 조종사의 격추 수
        PROPERTY( RuntimeState )
        int32 _loserGauge; ///< 판이 끝난 뒤 B 팀 게이지
    };
} // namespace sw
