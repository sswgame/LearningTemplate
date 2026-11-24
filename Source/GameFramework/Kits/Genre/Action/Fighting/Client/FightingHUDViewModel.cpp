#include "pch.h"

#include "GameFramework/Kits/Genre/Action/Fighting/Client/FightingHUDViewModel.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Kits/Genre/Action/Fighting/Component/FightingGameModeComponent.h"

namespace sw
{
    FightingHUDViewModel::FightingHUDViewModel()
        : UIViewModel{}
        , _playerHealthRatio{ 1.0f }
        , _opponentHealthRatio{ 1.0f }
        , _playerRounds{ 0 }
        , _opponentRounds{ 0 }
        , _secondsRemaining{ 0 }
        , _winVisibility{ WidgetVisibility::Collapsed }
        , _loseVisibility{ WidgetVisibility::Collapsed }
        , _drawVisibility{ WidgetVisibility::Collapsed }
    {
    }

    FightingHUDViewModel::~FightingHUDViewModel() = default;

    const TypeInfo* FightingHUDViewModel::getTypeInfo() const
    {
        return StaticType();
    }

    void FightingHUDViewModel::setHealth( float32 playerRatio, float32 opponentRatio )
    {
        (void)setField( _playerHealthRatio, MathUtil::clamp( playerRatio, 0.0f, 1.0f ), "_playerHealthRatio" );
        (void)setField( _opponentHealthRatio, MathUtil::clamp( opponentRatio, 0.0f, 1.0f ), "_opponentHealthRatio" );
    }

    void FightingHUDViewModel::setRounds( int32 playerRounds, int32 opponentRounds )
    {
        (void)setField( _playerRounds, playerRounds, "_playerRounds" );
        (void)setField( _opponentRounds, opponentRounds, "_opponentRounds" );
    }

    void FightingHUDViewModel::setSecondsRemaining( float32 seconds )
    {
        (void)setField( _secondsRemaining, static_cast<int32>( MathUtil::ceil( seconds ) ), "_secondsRemaining" );
    }

    void FightingHUDViewModel::setMatchWinner( int32 winner )
    {
        (void)setField( _winVisibility, winner == FightingGameModeComponent::kPlayer ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed, "_winVisibility" );
        (void)setField( _loseVisibility, winner == FightingGameModeComponent::kOpponent ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed, "_loseVisibility" );
        (void)setField( _drawVisibility, winner == FightingMatch::kDraw ? WidgetVisibility::HitTestInvisible : WidgetVisibility::Collapsed, "_drawVisibility" );
    }
} // namespace sw
