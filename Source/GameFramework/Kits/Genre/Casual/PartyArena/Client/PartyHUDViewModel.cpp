#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/Client/PartyHUDViewModel.h"

namespace sw
{
    PartyHUDViewModel::PartyHUDViewModel()
        : UIViewModel{}
        , _round{ 0 }
        , _timeLeft{ 0 }
        , _roundScore{ 0 }
        , _total{ 0 }
        , _winScore{ 0 }
        , _combo{ 0 }
        , _wonVisibility{ WidgetVisibility::Collapsed }
        , _lostVisibility{ WidgetVisibility::Collapsed }
    {
    }

    PartyHUDViewModel::~PartyHUDViewModel() = default;

    const TypeInfo* PartyHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void PartyHUDViewModel::setRound( int32 round )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _round, round );
    }

    void PartyHUDViewModel::setTimeLeft( int32 timeLeft )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _timeLeft, timeLeft );
    }

    void PartyHUDViewModel::setRoundScore( int32 roundScore )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _roundScore, roundScore );
    }

    void PartyHUDViewModel::setTotal( int32 total )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _total, total );
    }

    void PartyHUDViewModel::setWinScore( int32 winScore )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _winScore, winScore );
    }

    void PartyHUDViewModel::setCombo( int32 combo )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _combo, combo );
    }

    void PartyHUDViewModel::setWonShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _wonVisibility, toHUDVisibility( bShown ) );
    }

    void PartyHUDViewModel::setLostShown( bool bShown )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _lostVisibility, toHUDVisibility( bShown ) );
    }
} // namespace sw
