#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Client/BattleRoyaleHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    BattleRoyaleHUDViewModel::BattleRoyaleHUDViewModel()
        : UIViewModel{}
        , _healthRatio{ 1.0f }
        , _weaponName{}
        , _magazine{ 0 }
        , _spareAmmo{ 0 }
        , _remaining{ 0 }
        , _zoneSeconds{ 0 }
        , _dropHintVisibility{ WidgetVisibility::Collapsed }
        , _downedVisibility{ WidgetVisibility::Collapsed }
        , _winVisibility{ WidgetVisibility::Collapsed }
        , _loseVisibility{ WidgetVisibility::Collapsed }
    {
    }

    BattleRoyaleHUDViewModel::~BattleRoyaleHUDViewModel() = default;

    const TypeInfo* BattleRoyaleHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void BattleRoyaleHUDViewModel::setHealth( float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _healthRatio, MathUtil::clamp( ratio, 0.0f, 1.0f ) );
    }

    void BattleRoyaleHUDViewModel::setWeapon( string_view name, int32 magazine, int32 spareAmmo )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _weaponName, string( name ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _magazine, magazine );
        (void)SW_SET_VIEW_MODEL_FIELD( _spareAmmo, spareAmmo );
    }

    void BattleRoyaleHUDViewModel::setRemaining( int32 remaining )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _remaining, remaining );
    }

    void BattleRoyaleHUDViewModel::setZoneSeconds( float32 seconds )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _zoneSeconds, static_cast<int32>( MathUtil::ceil( seconds ) ) );
    }

    void BattleRoyaleHUDViewModel::setHints( bool bDropHint, bool bDowned )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _dropHintVisibility, toHUDVisibility( bDropHint ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _downedVisibility, toHUDVisibility( bDowned ) );
    }

    void BattleRoyaleHUDViewModel::setResult( bool bWon, bool bLost )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _winVisibility, toHUDVisibility( bWon ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _loseVisibility, toHUDVisibility( bLost ) );
    }
} // namespace sw
