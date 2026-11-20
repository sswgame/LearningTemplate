/**
 * @file SurvivalHorrorDemoComponent.h
 * @brief 장르 키트 시연 — 생존 공포(`GF_SurvivalHorror`)의 세션 한 바퀴입니다. 괴물을 두 번 보아 정신력이 깎이고, 다친 뒤 격자 가방의 약초로 체력을 채우는 것을 봅니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/playground/automation/kit_survivalhorror.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Horror/SurvivalHorror/SurvivalHorrorCatalog.h"

#include "Games/Playground/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class SurvivalHorrorDemoComponent
     * @brief 지도 없이 세션을 열고 같은 괴물을 두 번 목격(처음은 전부, 다음은 배율만큼)한 뒤 피해를 입고 약초 하나를 쓰는 한 바퀴를 돌립니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Survival Horror Demo", Tooltip = "Runs one SurvivalHorror kit session (monster sightings, damage, healing item) and keeps the result" )
    class SurvivalHorrorDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        SurvivalHorrorDemoComponent();
        virtual ~SurvivalHorrorDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getSanity() const { return _sanity; }
        int32 getHealth() const { return _health; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 세션 한 바퀴를 돌리고 결과를 담습니다. 세션을 열지 못하거나 약초를 쓰지 못하면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool runSession();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Survival horror catalog XML (rules, item, monster)" )
        AssetRef<SurvivalHorrorCatalog> _catalogPath;

        SharedAssetRef<SurvivalHorrorCatalog> _pCatalog;
        PROPERTY( RuntimeState )
        int32 _sanity; ///< 한 바퀴 뒤 정신력
        PROPERTY( RuntimeState )
        int32 _health; ///< 한 바퀴 뒤 체력
    };
} // namespace sw
