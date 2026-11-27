#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Client/ScavengerHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    ScavengerHUDViewModel::ScavengerHUDViewModel()
        : UIViewModel{}
        , _quotaLeft{ 0 }
        , _daysLeft{ 0 }
        , _shipValue{ 0 }
        , _credits{ 0 }
        , _healthRatio{ 0.0f }
        , _dayRatio{ 0.0f }
        , _terminalName{}
        , _terminalPrice{ 0 }
        , _hour{ 0 }
        , _scrapPercent{ 0 }
        , _threatPercent{ 0 }
        , _orbitVisibility{ WidgetVisibility::Collapsed }
        , _winVisibility{ WidgetVisibility::Collapsed }
        , _loseVisibility{ WidgetVisibility::Collapsed }
    {
    }

    ScavengerHUDViewModel::~ScavengerHUDViewModel() = default;

    const TypeInfo* ScavengerHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void ScavengerHUDViewModel::setQuotaLeft( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _quotaLeft, value );
    }

    void ScavengerHUDViewModel::setDaysLeft( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _daysLeft, value );
    }

    void ScavengerHUDViewModel::setShipValue( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _shipValue, value );
    }

    void ScavengerHUDViewModel::setCredits( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _credits, value );
    }

    void ScavengerHUDViewModel::setHealthRatio( float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _healthRatio, MathUtil::saturate( ratio ) );
    }

    void ScavengerHUDViewModel::setDayRatio( float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _dayRatio, MathUtil::saturate( ratio ) );
    }

    void ScavengerHUDViewModel::setHour( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _hour, value );
    }

    void ScavengerHUDViewModel::setScrapPercent( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _scrapPercent, value );
    }

    void ScavengerHUDViewModel::setThreatPercent( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _threatPercent, value );
    }

    void ScavengerHUDViewModel::setTerminalItem( string_view name, int32 price )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _terminalName, string( name ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _terminalPrice, price );
    }

    void ScavengerHUDViewModel::setOrbitShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _orbitVisibility, toHUDVisibility( bShown ) );
    }

    void ScavengerHUDViewModel::setWinShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _winVisibility, toHUDVisibility( bShown ) );
    }

    void ScavengerHUDViewModel::setLoseShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _loseVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
