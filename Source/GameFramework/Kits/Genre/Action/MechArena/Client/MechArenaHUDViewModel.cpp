#include "pch.h"

#include "GameFramework/Kits/Genre/Action/MechArena/Client/MechArenaHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    MechArenaHUDViewModel::MechArenaHUDViewModel()
        : UIViewModel{}
        , _healthRatio{ 1.0f }
        , _boostRatio{ 1.0f }
        , _ammo{ 0 }
        , _playerGauge{ 0 }
        , _enemyGauge{ 0 }
        , _overheatVisibility{ WidgetVisibility::Collapsed }
        , _destroyedVisibility{ WidgetVisibility::Collapsed }
        , _winVisibility{ WidgetVisibility::Collapsed }
        , _loseVisibility{ WidgetVisibility::Collapsed }
    {
    }

    MechArenaHUDViewModel::~MechArenaHUDViewModel() = default;

    const TypeInfo* MechArenaHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void MechArenaHUDViewModel::setPilot( float32 healthRatio, float32 boostRatio, int32 ammo )
    {
        (void)setField( _healthRatio, MathUtil::clamp( healthRatio, 0.0f, 1.0f ), "_healthRatio" );
        (void)setField( _boostRatio, MathUtil::clamp( boostRatio, 0.0f, 1.0f ), "_boostRatio" );
        (void)setField( _ammo, ammo, "_ammo" );
    }

    void MechArenaHUDViewModel::setTeamGauge( int32 playerGauge, int32 enemyGauge )
    {
        (void)setField( _playerGauge, playerGauge, "_playerGauge" );
        (void)setField( _enemyGauge, enemyGauge, "_enemyGauge" );
    }

    void MechArenaHUDViewModel::setHints( bool bOverheated, bool bDestroyed )
    {
        (void)setField( _overheatVisibility, toVisibility( bOverheated ), "_overheatVisibility" );
        (void)setField( _destroyedVisibility, toVisibility( bDestroyed ), "_destroyedVisibility" );
    }

    void MechArenaHUDViewModel::setResult( bool bWon, bool bLost )
    {
        (void)setField( _winVisibility, toVisibility( bWon ), "_winVisibility" );
        (void)setField( _loseVisibility, toVisibility( bLost ), "_loseVisibility" );
    }

    WidgetVisibility MechArenaHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
