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
        (void)SW_SET_VIEW_MODEL_FIELD( _rideStatus, string( rideStatus ) );
    }

    void WesternHUDViewModel::setOutlawStatus( string_view outlawStatus )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _outlawStatus, string( outlawStatus ) );
    }

    void WesternHUDViewModel::setStamina( float32 stamina, float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _stamina, static_cast<int32>( MathUtil::ceil( MathUtil::max( 0.0f, stamina ) ) ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _staminaRatio, MathUtil::clamp( ratio, 0.0f, 1.0f ) );
    }
} // namespace sw
