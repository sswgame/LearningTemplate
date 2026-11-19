/**
 * @file FightingDemoComponent.h
 * @brief 장르 키트 시연 — 3D 격투(`GF_Fighting`)의 한 판입니다. 같은 캐릭터 둘을 세우고 0 번이 하단 한 방으로 K.O. 를 내 선승 1 라운드 대전이 끝나는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/empty/automation/kit_fighting.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Action/Fighting/FighterCatalog.h"

#include "Games/Empty/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class FightingDemoComponent
     * @brief 기술 카탈로그(파일)에 캐릭터를 얹어 대전을 열고, 0 번의 하단이 1 번을 쓰러뜨려 대전이 끝나는 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Fighting Demo", Tooltip = "Runs one Fighting kit match (low hit, knockout, match end) and keeps the result" )
    class FightingDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        FightingDemoComponent();
        virtual ~FightingDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getMatchWinner() const { return _matchWinner; }
        int32 getLoserHealth() const { return _loserHealth; }
        int32 getWinnerRounds() const { return _winnerRounds; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 대전 한 바퀴를 돌리고 결과를 담습니다. 캐릭터가 카탈로그에 없거나 대전을 열지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runMatch();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Fighter catalog XML (roster; its moves attribute names the frame data)" )
        AssetRef<FighterCatalog> _catalogPath;

        SharedAssetRef<FighterCatalog> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _matchWinner; ///< 대전 결과(0, 1, 무승부 2, 끝나지 않으면 -1)
        PROPERTY( RuntimeState )
        int32 _loserHealth; ///< 쓰러진 1 번의 체력
        PROPERTY( RuntimeState )
        int32 _winnerRounds; ///< 0 번의 라운드 승 수
    };
} // namespace sw
