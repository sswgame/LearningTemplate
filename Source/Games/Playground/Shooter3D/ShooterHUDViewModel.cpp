#include "pch.h"

#include "Games/Playground/Shooter3D/ShooterHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    ShooterHUDViewModel::ShooterHUDViewModel()
        : UIViewModel{}
        , _health{ 0 }
        , _healthRatio{ 0.0f }
        , _magazineAmmo{ 0 }
        , _reserveAmmo{ 0 }
        , _weaponName{}
        , _itemName{}
        , _crosshairVisibility{ WidgetVisibility::Collapsed }
        , _hitMarkerVisibility{ WidgetVisibility::Collapsed }
    {
    }

    ShooterHUDViewModel::~ShooterHUDViewModel() = default;

    const TypeInfo* ShooterHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void ShooterHUDViewModel::setHealth( float32 health, float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _health, static_cast<int32>( MathUtil::ceil( MathUtil::max( 0.0f, health ) ) ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _healthRatio, MathUtil::clamp( ratio, 0.0f, 1.0f ) );
    }

    void ShooterHUDViewModel::setAmmo( int32 magazineAmmo, int32 reserveAmmo )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _magazineAmmo, magazineAmmo );
        (void)SW_SET_VIEW_MODEL_FIELD( _reserveAmmo, reserveAmmo );
    }

    void ShooterHUDViewModel::setWeaponName( string_view weaponName )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _weaponName, string( weaponName ) );
    }

    void ShooterHUDViewModel::setItemName( string_view itemName )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _itemName, string( itemName ) );
    }

    void ShooterHUDViewModel::setCrosshairShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _crosshairVisibility, toHUDVisibility( bShown ) );
    }

    void ShooterHUDViewModel::setHitMarkerShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _hitMarkerVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
