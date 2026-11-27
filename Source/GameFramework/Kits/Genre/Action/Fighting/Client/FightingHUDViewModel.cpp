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
        (void)SW_SET_VIEW_MODEL_FIELD( _playerHealthRatio, MathUtil::clamp( playerRatio, 0.0f, 1.0f ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _opponentHealthRatio, MathUtil::clamp( opponentRatio, 0.0f, 1.0f ) );
    }

    void FightingHUDViewModel::setRounds( int32 playerRounds, int32 opponentRounds )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _playerRounds, playerRounds );
        (void)SW_SET_VIEW_MODEL_FIELD( _opponentRounds, opponentRounds );
    }

    void FightingHUDViewModel::setSecondsRemaining( float32 seconds )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _secondsRemaining, static_cast<int32>( MathUtil::ceil( seconds ) ) );
    }

    void FightingHUDViewModel::setMatchWinner( int32 winner )
    {
        (void)SW_SET_VIEW_MODEL_FIELD( _winVisibility, toHUDVisibility( winner == FightingGameModeComponent::kPlayer ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _loseVisibility, toHUDVisibility( winner == FightingGameModeComponent::kOpponent ) );
        (void)SW_SET_VIEW_MODEL_FIELD( _drawVisibility, toHUDVisibility( winner == FightingMatch::kDraw ) );
    }
} // namespace sw
