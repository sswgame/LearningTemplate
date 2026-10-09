#include "pch.h"

#include "GameFramework/Base/UI/Hud/HudViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    HudViewModel::HudViewModel()
        : UiViewModel{}
        , _health{ 0 }
        , _healthRatio{ 0.0f }
        , _magazineAmmo{ 0 }
        , _reserveAmmo{ 0 }
        , _weaponName{}
        , _crosshairVisibility{ WidgetVisibility::Collapsed }
        , _hitMarkerVisibility{ WidgetVisibility::Collapsed }
    {
    }

    HudViewModel::~HudViewModel() = default;

    const TypeInfo* HudViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void HudViewModel::setHealth( float32 health, float32 ratio )
    {
        (void)setField( _health, static_cast<int32>( MathUtil::ceil( MathUtil::max( 0.0f, health ) ) ), "_health" );
        (void)setField( _healthRatio, MathUtil::clamp( ratio, 0.0f, 1.0f ), "_healthRatio" );
    }

    void HudViewModel::setAmmo( int32 magazineAmmo, int32 reserveAmmo )
    {
        (void)setField( _magazineAmmo, magazineAmmo, "_magazineAmmo" );
        (void)setField( _reserveAmmo, reserveAmmo, "_reserveAmmo" );
    }

    void HudViewModel::setWeaponName( string_view weaponName )
    {
        (void)setField( _weaponName, string( weaponName ), "_weaponName" );
    }

    void HudViewModel::setCrosshairShown( bool bShown )
    {
        (void)setField( _crosshairVisibility, toVisibility( bShown ), "_crosshairVisibility" );
    }

    void HudViewModel::setHitMarkerShown( bool bShown )
    {
        (void)setField( _hitMarkerVisibility, toVisibility( bShown ), "_hitMarkerVisibility" );
    }

    WidgetVisibility HudViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
