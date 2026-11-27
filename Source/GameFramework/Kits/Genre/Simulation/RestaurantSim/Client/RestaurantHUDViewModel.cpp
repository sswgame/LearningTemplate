#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/Client/RestaurantHUDViewModel.h"

namespace sw
{
    RestaurantHUDViewModel::RestaurantHUDViewModel()
        : UIViewModel{}
        , _dishName{}
        , _day{ 0 }
        , _dayCount{ 0 }
        , _gold{ 0 }
        , _totalProfit{ 0 }
        , _targetProfit{ 0 }
        , _reputation{ 0 }
        , _cookLevel{ 0 }
        , _hour{ 0 }
        , _servedCount{ 0 }
        , _seatedCount{ 0 }
        , _price{ 0 }
        , _ingredientCount{ 0 }
        , _prepareVisibility{ WidgetVisibility::Collapsed }
        , _openVisibility{ WidgetVisibility::Collapsed }
        , _closedVisibility{ WidgetVisibility::Collapsed }
        , _victoryVisibility{ WidgetVisibility::Collapsed }
        , _defeatVisibility{ WidgetVisibility::Collapsed }
    {
    }

    RestaurantHUDViewModel::~RestaurantHUDViewModel() = default;

    const TypeInfo* RestaurantHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void RestaurantHUDViewModel::setPhase( RestaurantPhase phase )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _prepareVisibility, toHUDVisibility( phase == RestaurantPhase::Prepare ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _openVisibility, toHUDVisibility( phase == RestaurantPhase::Open ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _closedVisibility, toHUDVisibility( phase == RestaurantPhase::Closed ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _victoryVisibility, toHUDVisibility( phase == RestaurantPhase::Victory ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _defeatVisibility, toHUDVisibility( phase == RestaurantPhase::Defeat ) );
    }

    void RestaurantHUDViewModel::setDay( int32 day, int32 dayCount )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _day, day );
        (void)SW_SET_VIEW_MODEL_FIELD( _dayCount, dayCount );
    }

    void RestaurantHUDViewModel::setMoney( int32 gold, int32 totalProfit, int32 targetProfit )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _gold, gold );
        (void)SW_SET_VIEW_MODEL_FIELD( _totalProfit, totalProfit );
        (void)SW_SET_VIEW_MODEL_FIELD( _targetProfit, targetProfit );
    }

    void RestaurantHUDViewModel::setHouse( int32 reputation, int32 cookLevel )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _reputation, reputation );
        (void)SW_SET_VIEW_MODEL_FIELD( _cookLevel, cookLevel );
    }

    void RestaurantHUDViewModel::setCustomers( int32 hour, int32 servedCount, int32 seatedCount )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _hour, hour );
        (void)SW_SET_VIEW_MODEL_FIELD( _servedCount, servedCount );
        (void)SW_SET_VIEW_MODEL_FIELD( _seatedCount, seatedCount );
    }

    void RestaurantHUDViewModel::setSelection( string_view dishName, int32 price, int32 ingredientCount )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _dishName, string( dishName ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _price, price );
        (void)SW_SET_VIEW_MODEL_FIELD( _ingredientCount, ingredientCount );
    }
} // namespace sw
