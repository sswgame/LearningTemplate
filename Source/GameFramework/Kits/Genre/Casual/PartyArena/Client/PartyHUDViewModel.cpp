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

    WidgetVisibility PartyHUDViewModel::toVisibility( bool bShown )
    {
        return bShown ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed;
    }

    void PartyHUDViewModel::setRound( int32 round )
    {
        (void)setField( _round, round, "_round" );
    }

    void PartyHUDViewModel::setTimeLeft( int32 timeLeft )
    {
        (void)setField( _timeLeft, timeLeft, "_timeLeft" );
    }

    void PartyHUDViewModel::setRoundScore( int32 roundScore )
    {
        (void)setField( _roundScore, roundScore, "_roundScore" );
    }

    void PartyHUDViewModel::setTotal( int32 total )
    {
        (void)setField( _total, total, "_total" );
    }

    void PartyHUDViewModel::setWinScore( int32 winScore )
    {
        (void)setField( _winScore, winScore, "_winScore" );
    }

    void PartyHUDViewModel::setCombo( int32 combo )
    {
        (void)setField( _combo, combo, "_combo" );
    }

    void PartyHUDViewModel::setWonShown( bool bShown )
    {
        (void)setField( _wonVisibility, toVisibility( bShown ), "_wonVisibility" );
    }

    void PartyHUDViewModel::setLostShown( bool bShown )
    {
        (void)setField( _lostVisibility, toVisibility( bShown ), "_lostVisibility" );
    }
} // namespace sw
