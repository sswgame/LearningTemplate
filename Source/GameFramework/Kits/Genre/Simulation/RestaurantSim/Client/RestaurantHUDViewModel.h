/**
 * @file RestaurantHUDViewModel.h
 * @brief 식당 HUD 문서가 보이는 값(날 · 돈 · 이익 · 시각 · 손님 · 고른 요리와 재료 · 단계마다의 묶음 보임)입니다 — `RestaurantCounterComponent` 는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Presentation/WidgetVisibility.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/Component/RestaurantGameModeComponent.h"

namespace sw
{
    /**
     * @class RestaurantHUDViewModel
     * @brief 식당 HUD 의 뷰모델입니다. `RestaurantCounterComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서에 있다(키트 코드는 글을 짓지 않는다) — 요리 이름 칸은 카탈로그의 원문(현지화 키)이고 나머지 글은 단계마다 보이는 위젯 묶음이다.
     *          세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Restaurant HUD View Model" )
    class SW_KIT_CLIENT_API RestaurantHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        RestaurantHUDViewModel();
        ~RestaurantHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 이 단계의 위젯 묶음만 보이게 합니다. */
        void setPhase( RestaurantPhase phase );
        void setDay( int32 day, int32 dayCount );
        void setMoney( int32 gold, int32 totalProfit, int32 targetProfit );
        /** @brief 문을 연 뒤의 시각(시)과 계산한 손님 · 앉은 손님 수입니다. */
        void setCustomers( int32 hour, int32 servedCount, int32 seatedCount );
        /** @brief 고른 줄의 요리 이름(현지화 키) · 가격과 재료의 남은 수입니다. */
        void setSelection( string_view dishName, int32 price, int32 ingredientCount );

    private:
        /** @brief 보임 여부를 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Dish Name", Tooltip = "Localization key (the source text when the table has no entry)" )
        string _dishName;
        PROPERTY( DisplayName = "Day" )
        int32 _day;
        PROPERTY( DisplayName = "Day Count" )
        int32 _dayCount;
        PROPERTY( DisplayName = "Gold" )
        int32 _gold;
        PROPERTY( DisplayName = "Total Profit" )
        int32 _totalProfit;
        PROPERTY( DisplayName = "Target Profit" )
        int32 _targetProfit;
        PROPERTY( DisplayName = "Hour" )
        int32 _hour;
        PROPERTY( DisplayName = "Served" )
        int32 _servedCount;
        PROPERTY( DisplayName = "Seated" )
        int32 _seatedCount;
        PROPERTY( DisplayName = "Price" )
        int32 _price;
        PROPERTY( DisplayName = "Ingredient Count" )
        int32 _ingredientCount;
        PROPERTY( DisplayName = "Prepare Visibility" )
        WidgetVisibility _prepareVisibility;
        PROPERTY( DisplayName = "Open Visibility" )
        WidgetVisibility _openVisibility;
        PROPERTY( DisplayName = "Closed Visibility" )
        WidgetVisibility _closedVisibility;
        PROPERTY( DisplayName = "Victory Visibility" )
        WidgetVisibility _victoryVisibility;
        PROPERTY( DisplayName = "Defeat Visibility" )
        WidgetVisibility _defeatVisibility;
    };
} // namespace sw
