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
        (void)setField( _quotaLeft, value, "_quotaLeft" );
    }

    void ScavengerHUDViewModel::setDaysLeft( int32 value )
    {
        (void)setField( _daysLeft, value, "_daysLeft" );
    }

    void ScavengerHUDViewModel::setShipValue( int32 value )
    {
        (void)setField( _shipValue, value, "_shipValue" );
    }

    void ScavengerHUDViewModel::setCredits( int32 value )
    {
        (void)setField( _credits, value, "_credits" );
    }

    void ScavengerHUDViewModel::setHealthRatio( float32 ratio )
    {
        (void)setField( _healthRatio, MathUtil::saturate( ratio ), "_healthRatio" );
    }

    void ScavengerHUDViewModel::setDayRatio( float32 ratio )
    {
        (void)setField( _dayRatio, MathUtil::saturate( ratio ), "_dayRatio" );
    }

    void ScavengerHUDViewModel::setOrbitShown( bool bShown )
    {
        (void)setField( _orbitVisibility, toVisibility( bShown ), "_orbitVisibility" );
    }

    void ScavengerHUDViewModel::setWinShown( bool bShown )
    {
        (void)setField( _winVisibility, toVisibility( bShown ), "_winVisibility" );
    }

    void ScavengerHUDViewModel::setLoseShown( bool bShown )
    {
        (void)setField( _loseVisibility, toVisibility( bShown ), "_loseVisibility" );
    }

    WidgetVisibility ScavengerHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
