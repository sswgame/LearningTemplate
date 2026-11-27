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
        (void)SW_SET_VIEW_MODEL_FIELD( _healthRatio, MathUtil::clamp( healthRatio, 0.0f, 1.0f ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _boostRatio, MathUtil::clamp( boostRatio, 0.0f, 1.0f ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _ammo, ammo );
    }

    void MechArenaHUDViewModel::setTeamGauge( int32 playerGauge, int32 enemyGauge )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _playerGauge, playerGauge );
        (void)SW_SET_VIEW_MODEL_FIELD( _enemyGauge, enemyGauge );
    }

    void MechArenaHUDViewModel::setHints( bool bOverheated, bool bDestroyed )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _overheatVisibility, toHUDVisibility( bOverheated ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _destroyedVisibility, toHUDVisibility( bDestroyed ) );
    }

    void MechArenaHUDViewModel::setResult( bool bWon, bool bLost )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _winVisibility, toHUDVisibility( bWon ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _loseVisibility, toHUDVisibility( bLost ) );
    }
} // namespace sw
