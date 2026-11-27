#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/Client/GhostHuntHUDViewModel.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    GhostHuntHUDViewModel::GhostHuntHUDViewModel()
        : UIViewModel{}
        , _darkRooms{ 0 }
        , _coins{ 0 }
        , _keys{ 0 }
        , _healthRatio{ 0.0f }
        , _surgeRatio{ 0.0f }
        , _winVisibility{ WidgetVisibility::Collapsed }
        , _loseVisibility{ WidgetVisibility::Collapsed }
    {
    }

    GhostHuntHUDViewModel::~GhostHuntHUDViewModel() = default;

    const TypeInfo* GhostHuntHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void GhostHuntHUDViewModel::setDarkRooms( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _darkRooms, value );
    }

    void GhostHuntHUDViewModel::setCoins( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _coins, value );
    }

    void GhostHuntHUDViewModel::setKeys( int32 value )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _keys, value );
    }

    void GhostHuntHUDViewModel::setHealthRatio( float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _healthRatio, MathUtil::saturate( ratio ) );
    }

    void GhostHuntHUDViewModel::setSurgeRatio( float32 ratio )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _surgeRatio, MathUtil::saturate( ratio ) );
    }

    void GhostHuntHUDViewModel::setWinShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _winVisibility, toHUDVisibility( bShown ) );
    }

    void GhostHuntHUDViewModel::setLoseShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _loseVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
