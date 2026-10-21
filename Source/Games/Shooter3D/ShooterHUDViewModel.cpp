#include "pch.h"

#include "Games/Shooter3D/ShooterHUDViewModel.h"

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
        (void)setField( _health, static_cast<int32>( MathUtil::ceil( MathUtil::max( 0.0f, health ) ) ), "_health" );
        (void)setField( _healthRatio, MathUtil::clamp( ratio, 0.0f, 1.0f ), "_healthRatio" );
    }

    void ShooterHUDViewModel::setAmmo( int32 magazineAmmo, int32 reserveAmmo )
    {
        (void)setField( _magazineAmmo, magazineAmmo, "_magazineAmmo" );
        (void)setField( _reserveAmmo, reserveAmmo, "_reserveAmmo" );
    }

    void ShooterHUDViewModel::setWeaponName( string_view weaponName )
    {
        (void)setField( _weaponName, string( weaponName ), "_weaponName" );
    }

    void ShooterHUDViewModel::setItemName( string_view itemName )
    {
        (void)setField( _itemName, string( itemName ), "_itemName" );
    }

    void ShooterHUDViewModel::setCrosshairShown( bool bShown )
    {
        (void)setField( _crosshairVisibility, toVisibility( bShown ), "_crosshairVisibility" );
    }

    void ShooterHUDViewModel::setHitMarkerShown( bool bShown )
    {
        (void)setField( _hitMarkerVisibility, toVisibility( bShown ), "_hitMarkerVisibility" );
    }

    WidgetVisibility ShooterHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
