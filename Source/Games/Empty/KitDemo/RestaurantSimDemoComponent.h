/**
 * @file RestaurantSimDemoComponent.h
 * @brief 장르 키트 시연 — 식당 경영(`GF_RestaurantSim`)의 하루 영업입니다. 요리사 · 서버를 두고 손님 둘을 받아 조리 · 서빙 · 계산을 마친 뒤 문을 닫고 결산합니다.
 * @details 판을 여는 순간(`startGame`) 규칙을 끝까지 돌리고 결과만 쥔다 — 모습 · 입력이 없는 규칙 확인 시연이다(언리얼 Functional Test 의 규칙 단계와 같은 자리).
 *          시나리오: `game/empty/automation/kit_restaurantsim.scenario.xml`.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"

#include "Games/Empty/KitDemo/KitDemoComponent.h"

namespace sw
{
    /**
     * @class RestaurantSimDemoComponent
     * @brief 라멘 하나뿐인 메뉴로 문을 열고 손님 둘을 들여 한 시간을 돌린 뒤 닫아, 손님 수 · 매출 · 이익(매출 - 재료비 - 급여)을 봅니다.
     */
    REFLECT( Category = "KitDemo", DisplayName = "Restaurant Sim Demo", Tooltip = "Runs one RestaurantSim kit business day (cook, serve, checkout, close) and keeps the summary" )
    class RestaurantSimDemoComponent : public KitDemoComponent
    {
    public:
        REFLECT_BODY();

        RestaurantSimDemoComponent();
        virtual ~RestaurantSimDemoComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 자동화 감시 값이 읽는 것 ----
        int32 getServed() const { return _served; }
        int32 getRevenue() const { return _revenue; }
        int32 getProfit() const { return _profit; }

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;

    private:
        /** @brief 하루 영업을 돌리고 결산을 담습니다. 창고를 열지 못하면 false 입니다. */
        [[nodiscard]] bool runDay();

    private:
        PROPERTY( Category = "Data", DisplayName = "Catalog", Tooltip = "Restaurant catalog XML (menu, customers)" )
        AssetRef<RestaurantCatalog> _catalogPath;
        PROPERTY( Category = "Data", DisplayName = "Recipes", Tooltip = "Recipe catalog XML the menu dishes cook from" )
        AssetRef<RecipeCatalog> _catalog;

        SharedAssetRef<RestaurantCatalog> _pCatalog;
        SharedAssetRef<RecipeCatalog>     _pRecipeCatalog;
        PROPERTY( RuntimeState )
        int32 _served; ///< 계산까지 마친 손님 수
        PROPERTY( RuntimeState )
        int32 _revenue; ///< 매출(팁 제외)
        PROPERTY( RuntimeState )
        int32 _profit; ///< 이익 = 매출 + 팁 - 재료비 - 급여 - 버린 재료
    };
} // namespace sw
