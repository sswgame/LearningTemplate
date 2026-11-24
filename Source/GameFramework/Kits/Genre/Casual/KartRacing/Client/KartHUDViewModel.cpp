#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/KartRacing/Client/KartHUDViewModel.h"

namespace sw
{
    KartHUDViewModel::KartHUDViewModel()
        : UIViewModel{}
        , _place{ 0 }
        , _racerCount{ 0 }
        , _lap{ 0 }
        , _lapCount{ 0 }
        , _speed{ 0 }
        , _countdown{ 0 }
        , _countdownVisibility{ WidgetVisibility::Collapsed }
        , _itemVisibility{ WidgetVisibility::Collapsed }
        , _wrongWayVisibility{ WidgetVisibility::Collapsed }
        , _finishVisibility{ WidgetVisibility::Collapsed }
    {
    }

    KartHUDViewModel::~KartHUDViewModel() = default;

    const TypeInfo* KartHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    WidgetVisibility KartHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }

    void KartHUDViewModel::setPlace( int32 place )
    {
        (void)setField( _place, place, "_place" );
    }

    void KartHUDViewModel::setRacerCount( int32 racerCount )
    {
        (void)setField( _racerCount, racerCount, "_racerCount" );
    }

    void KartHUDViewModel::setLap( int32 lap )
    {
        (void)setField( _lap, lap, "_lap" );
    }

    void KartHUDViewModel::setLapCount( int32 lapCount )
    {
        (void)setField( _lapCount, lapCount, "_lapCount" );
    }

    void KartHUDViewModel::setSpeed( int32 speed )
    {
        (void)setField( _speed, speed, "_speed" );
    }

    void KartHUDViewModel::setCountdown( int32 countdown )
    {
        (void)setField( _countdown, countdown, "_countdown" );
    }

    void KartHUDViewModel::setCountdownShown( bool bShown )
    {
        (void)setField( _countdownVisibility, toVisibility( bShown ), "_countdownVisibility" );
    }

    void KartHUDViewModel::setItemShown( bool bShown )
    {
        (void)setField( _itemVisibility, toVisibility( bShown ), "_itemVisibility" );
    }

    void KartHUDViewModel::setWrongWayShown( bool bShown )
    {
        (void)setField( _wrongWayVisibility, toVisibility( bShown ), "_wrongWayVisibility" );
    }

    void KartHUDViewModel::setFinishShown( bool bShown )
    {
        (void)setField( _finishVisibility, toVisibility( bShown ), "_finishVisibility" );
    }
} // namespace sw
