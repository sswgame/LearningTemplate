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

    WidgetVisibility RestaurantHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }

    void RestaurantHUDViewModel::setPhase( RestaurantPhase phase )
    {
        (void)setField( _prepareVisibility, toVisibility( phase == RestaurantPhase::Prepare ), "_prepareVisibility" );
        (void)setField( _openVisibility, toVisibility( phase == RestaurantPhase::Open ), "_openVisibility" );
        (void)setField( _closedVisibility, toVisibility( phase == RestaurantPhase::Closed ), "_closedVisibility" );
        (void)setField( _victoryVisibility, toVisibility( phase == RestaurantPhase::Victory ), "_victoryVisibility" );
        (void)setField( _defeatVisibility, toVisibility( phase == RestaurantPhase::Defeat ), "_defeatVisibility" );
    }

    void RestaurantHUDViewModel::setDay( int32 day, int32 dayCount )
    {
        (void)setField( _day, day, "_day" );
        (void)setField( _dayCount, dayCount, "_dayCount" );
    }

    void RestaurantHUDViewModel::setMoney( int32 gold, int32 totalProfit, int32 targetProfit )
    {
        (void)setField( _gold, gold, "_gold" );
        (void)setField( _totalProfit, totalProfit, "_totalProfit" );
        (void)setField( _targetProfit, targetProfit, "_targetProfit" );
    }

    void RestaurantHUDViewModel::setHouse( int32 reputation, int32 cookLevel )
    {
        (void)setField( _reputation, reputation, "_reputation" );
        (void)setField( _cookLevel, cookLevel, "_cookLevel" );
    }

    void RestaurantHUDViewModel::setCustomers( int32 hour, int32 servedCount, int32 seatedCount )
    {
        (void)setField( _hour, hour, "_hour" );
        (void)setField( _servedCount, servedCount, "_servedCount" );
        (void)setField( _seatedCount, seatedCount, "_seatedCount" );
    }

    void RestaurantHUDViewModel::setSelection( string_view dishName, int32 price, int32 ingredientCount )
    {
        (void)setField( _dishName, string( dishName ), "_dishName" );
        (void)setField( _price, price, "_price" );
        (void)setField( _ingredientCount, ingredientCount, "_ingredientCount" );
    }
} // namespace sw
