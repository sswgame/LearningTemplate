#include "pch.h"

#include "GameFramework/Runtime/Foundation/GameMode/MatchGameModeComponent.h"

#include "Core/Container/vector.h"
#include "Core/Log/Logger.h"
#include "Core/Time/Countdown.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigameFlow.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct MatchGameModeComponentInternal
        {
            static constexpr float32 kDefaultResultHoldSeconds = 3.0f;
        };
    } // namespace
} // namespace sw

namespace sw
{
    MatchGameModeComponent::MatchGameModeComponent()
        : MatchGameModeComponent( MatchGameModeComponentInternal::kDefaultResultHoldSeconds )
    {
    }

    MatchGameModeComponent::MatchGameModeComponent( float32 resultHoldSeconds )
        : _resultHoldSeconds{ resultHoldSeconds }
        , _endResult{}
        , _resultHoldLeft{ 0.0f }
    {
    }

    MatchGameModeComponent::~MatchGameModeComponent() = default;

    void MatchGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( isMatchEnded() == false )
        {
            updateMatch( deltaTime );
            return;
        }
        Countdown  resultHold( _resultHoldLeft );
        const bool bHeld = resultHold.update( deltaTime ); // 다 된 걸음에 한 번만 true 다
        _resultHoldLeft  = resultHold._remaining;
        if ( bHeld )
            finishMinigame();
    }

    void MatchGameModeComponent::endMatch( const hashed_string& resultName )
    {
        SW_ASSERT( resultName.empty() == false && isMatchEnded() == false );
        _endResult      = resultName;
        _resultHoldLeft = _resultHoldSeconds;
        if ( _resultHoldSeconds <= 0.0f )
            finishMinigame();
    }

    void MatchGameModeComponent::finishMinigame()
    {
        MinigameFlow* const pFlow = game::findSubsystem<MinigameFlow>();
        if ( pFlow != nullptr && pFlow->isObserving() )
            return; // 서버의 판을 보는 클라이언트다 — 끝내는 것은 서버가 정한다
        if ( pFlow == nullptr || pFlow->findActiveDefinition() == nullptr )
        {
            SW_LOG_ERROR_ID( "GameMode.FinishWithoutMinigame", "GameMode '%#' finishes with result '%#' but no minigame is active - only a minigame director ends a minigame",
                             getTypeName().c_str(), _endResult.c_str() );
            return;
        }
        vector<MinigameResultValue> listValue;
        collectMinigameResult( listValue );
        pFlow->requestLeave( _endResult, std::move( listValue ) );
    }
} // namespace sw
