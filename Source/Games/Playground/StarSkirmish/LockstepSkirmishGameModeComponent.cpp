#include "pch.h"

#include "Games/Playground/StarSkirmish/LockstepSkirmishGameModeComponent.h"

namespace sw
{
    LockstepSkirmishGameModeComponent::LockstepSkirmishGameModeComponent()
        : _lockstep{}
    {
    }

    LockstepSkirmishGameModeComponent::~LockstepSkirmishGameModeComponent() = default;

    void LockstepSkirmishGameModeComponent::onEndPlay()
    {
        _lockstep.close();
        SkirmishGameModeComponent::onEndPlay();
    }

    optional<uint8> LockstepSkirmishGameModeComponent::openPlayers( bool bAutoPlay )
    {
        if ( _lockstep.open() == false )
            return nullopt; // 까닭은 락스텝이 남겼다
        if ( _lockstep.isActive() && getMap()._listPlayerStart.size() != static_cast<size_t>( SkirmishLockstepLink::kPlayerCount ) )
        {
            SW_LOG_ERROR_ID( "LockstepSkirmish.MapPlayerCount", "[Skirmish] a network match needs a map with %# player starts, this map has %#", SkirmishLockstepLink::kPlayerCount, getMap()._listPlayerStart.size() );
            return nullopt;
        }
        if ( bAutoPlay == false && _lockstep.isActive() )
            return static_cast<uint8>( SkirmishMatch::kHumanPlayer0 | SkirmishMatch::kHumanPlayer1 );
        return SkirmishGameModeComponent::openPlayers( bAutoPlay );
    }

    void LockstepSkirmishGameModeComponent::stepMatch( GameSeconds frameTime )
    {
        const float32 deltaTime = frameTime.count();
        if ( _lockstep.releaseLostSession() )
            SW_LOG_INFO_ID( "LockstepSkirmish.PeerLost", "[Skirmish] the other player left the match - it goes on here without the other player" );
        if ( _lockstep.isActive() )
            (void)_lockstep.advance( deltaTime, getWorld().getSettings()._fixedStep, getMutableMatch() ); // 두 프로세스가 같은 걸음으로 돈다 - 속도 · 일시정지는 쓰지 않는다
        else
            SkirmishGameModeComponent::stepMatch( frameTime );
    }

    int32 LockstepSkirmishGameModeComponent::submitCommand( RTSCommand&& command )
    {
        if ( _lockstep.isActive() == false )
            return SkirmishGameModeComponent::submitCommand( std::move( command ) );
        return _lockstep.queueCommand( std::move( command ) ) ? -1 : 0;
    }

    bool LockstepSkirmishGameModeComponent::submitTimeInput( bool bPauseTriggered, bool bFasterTriggered, bool bSlowerTriggered )
    {
        if ( _lockstep.isActive() == false )
            return SkirmishGameModeComponent::submitTimeInput( bPauseTriggered, bFasterTriggered, bSlowerTriggered );
        if ( bPauseTriggered || bFasterTriggered || bSlowerTriggered )
            SW_LOG_INFO_ID( "LockstepSkirmish.TimingInNetMatch", "[Skirmish] speed and pause are off in a network match - both players run the same steps" );
        return false;
    }
} // namespace sw
