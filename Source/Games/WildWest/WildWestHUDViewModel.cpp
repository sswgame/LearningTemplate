#include "pch.h"

#include "Games/WildWest/WildWestHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    WildWestHUDViewModel::WildWestHUDViewModel()
        : UIViewModel{}
        , _rideStatus{}
        , _stamina{ 0 }
        , _staminaRatio{ 0.0f }
    {
    }

    WildWestHUDViewModel::~WildWestHUDViewModel() = default;

    const TypeInfo* WildWestHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void WildWestHUDViewModel::setRideStatus( string_view rideStatus )
    {
        (void)setField( _rideStatus, string( rideStatus ), "_rideStatus" );
    }

    void WildWestHUDViewModel::setStamina( float32 stamina, float32 ratio )
    {
        (void)setField( _stamina, static_cast<int32>( MathUtil::ceil( MathUtil::max( 0.0f, stamina ) ) ), "_stamina" );
        (void)setField( _staminaRatio, MathUtil::clamp( ratio, 0.0f, 1.0f ), "_staminaRatio" );
    }
} // namespace sw
