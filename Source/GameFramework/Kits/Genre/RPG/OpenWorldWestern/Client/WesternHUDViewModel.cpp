#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/Client/WesternHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    WesternHUDViewModel::WesternHUDViewModel()
        : UIViewModel{}
        , _rideStatus{}
        , _outlawStatus{}
        , _stamina{ 0 }
        , _staminaRatio{ 0.0f }
    {
    }

    WesternHUDViewModel::~WesternHUDViewModel() = default;

    const TypeInfo* WesternHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void WesternHUDViewModel::setRideStatus( string_view rideStatus )
    {
        (void)setField( _rideStatus, string( rideStatus ), "_rideStatus" );
    }

    void WesternHUDViewModel::setOutlawStatus( string_view outlawStatus )
    {
        (void)setField( _outlawStatus, string( outlawStatus ), "_outlawStatus" );
    }

    void WesternHUDViewModel::setStamina( float32 stamina, float32 ratio )
    {
        (void)setField( _stamina, static_cast<int32>( MathUtil::ceil( MathUtil::max( 0.0f, stamina ) ) ), "_stamina" );
        (void)setField( _staminaRatio, MathUtil::clamp( ratio, 0.0f, 1.0f ), "_staminaRatio" );
    }
} // namespace sw
