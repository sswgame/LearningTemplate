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
        (void)setField( _darkRooms, value, "_darkRooms" );
    }

    void GhostHuntHUDViewModel::setCoins( int32 value )
    {
        (void)setField( _coins, value, "_coins" );
    }

    void GhostHuntHUDViewModel::setKeys( int32 value )
    {
        (void)setField( _keys, value, "_keys" );
    }

    void GhostHuntHUDViewModel::setHealthRatio( float32 ratio )
    {
        (void)setField( _healthRatio, MathUtil::saturate( ratio ), "_healthRatio" );
    }

    void GhostHuntHUDViewModel::setSurgeRatio( float32 ratio )
    {
        (void)setField( _surgeRatio, MathUtil::saturate( ratio ), "_surgeRatio" );
    }

    void GhostHuntHUDViewModel::setWinShown( bool bShown )
    {
        (void)setField( _winVisibility, toVisibility( bShown ), "_winVisibility" );
    }

    void GhostHuntHUDViewModel::setLoseShown( bool bShown )
    {
        (void)setField( _loseVisibility, toVisibility( bShown ), "_loseVisibility" );
    }

    WidgetVisibility GhostHuntHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }
} // namespace sw
