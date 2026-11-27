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

    void KartHUDViewModel::setPlace( int32 place )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _place, place );
    }

    void KartHUDViewModel::setRacerCount( int32 racerCount )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _racerCount, racerCount );
    }

    void KartHUDViewModel::setLap( int32 lap )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _lap, lap );
    }

    void KartHUDViewModel::setLapCount( int32 lapCount )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _lapCount, lapCount );
    }

    void KartHUDViewModel::setSpeed( int32 speed )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _speed, speed );
    }

    void KartHUDViewModel::setCountdown( int32 countdown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _countdown, countdown );
    }

    void KartHUDViewModel::setCountdownShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _countdownVisibility, toHUDVisibility( bShown ) );
    }

    void KartHUDViewModel::setItemShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _itemVisibility, toHUDVisibility( bShown ) );
    }

    void KartHUDViewModel::setWrongWayShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _wrongWayVisibility, toHUDVisibility( bShown ) );
    }

    void KartHUDViewModel::setFinishShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _finishVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
