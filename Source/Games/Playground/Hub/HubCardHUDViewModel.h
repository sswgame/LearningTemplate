/**
 * @file HubCardHUDViewModel.h
 * @brief 허브 카드 탁자 HUD 문서(`hub_cards.ui.xml`)가 보이는 값입니다 — 탁자는 받은 모습을 넣기만 하고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Presentation/WidgetVisibility.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    struct HubCardSeatView;

    /**
     * @class HubCardHUDViewModel
     * @brief 카드 탁자 HUD 의 뷰모델입니다. `HubCardTableComponent` 가 소유하고 `IHUDViewModelBinder` 로 겁니다(UI 를 모른다 — 전용 서버도 짓는다). 게임 스레드만.
     */
    REFLECT( DisplayName = "Hub Card HUD View Model" )
    class HubCardHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        HubCardHUDViewModel();
        ~HubCardHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 앉았는지와 받은 모습 @p view, 내 차례인지를 보입니다. 앉지 않았으면 모두 접습니다. */
        void setTable( bool bSeated, const HubCardSeatView& view, bool bMyTurn );

        const string&    getHand() const { return _hand; }
        const string&    getTopCard() const { return _topCard; }
        int32            getOpponentCount() const { return _opponentCount; }
        WidgetVisibility getTableVisibility() const { return _tableVisibility; }
        WidgetVisibility getTurnVisibility() const { return _turnVisibility; }

    private:
        PROPERTY( DisplayName = "Hand", Tooltip = "Cards in hand as colour and value (R5 is red 5, W is wild)" )
        string _hand;
        PROPERTY( DisplayName = "Top Card" )
        string _topCard;
        PROPERTY( DisplayName = "Opponent Count" )
        int32 _opponentCount;
        PROPERTY( DisplayName = "Table Visibility" )
        WidgetVisibility _tableVisibility;
        PROPERTY( DisplayName = "Turn Visibility" )
        WidgetVisibility _turnVisibility;
    };
} // namespace sw
