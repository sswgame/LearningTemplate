#include "pch.h"

#include "Games/Playground/StarSkirmish/LockstepSkirmishGameModeComponent.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"

namespace sw
{
    namespace
    {
        struct LockstepSkirmishGameModeComponentInternal
        {
            // 두 프로세스 시나리오(`netpair/lockstep_*`)가 판정하는 감시 값. 넷 대전이 아니면 값이 없다.
            [[nodiscard]] static bool readPlayer( const LockstepSkirmishGameModeComponent& gameMode, float64& outValue )
            {
                outValue = static_cast<float64>( gameMode.getLockstep().getLocalPlayer() );
                return gameMode.getLockstep().isActive();
            }
            [[nodiscard]] static bool readTick( const LockstepSkirmishGameModeComponent& gameMode, float64& outValue )
            {
                outValue = static_cast<float64>( gameMode.getLockstep().getSession().getCurrentTick() );
                return gameMode.getLockstep().isActive();
            }
            [[nodiscard]] static bool readDesynced( const LockstepSkirmishGameModeComponent& gameMode, float64& outValue )
            {
                outValue = gameMode.getLockstep().getSession().isDesynced() ? 1.0 : 0.0;
                return gameMode.getLockstep().isActive();
            }
            [[nodiscard]] static bool readClientCommands( const LockstepSkirmishGameModeComponent& gameMode, float64& outValue )
            {
                outValue = static_cast<float64>( gameMode.getLockstep().getAppliedCommandCount( 1 ) );
                return gameMode.getLockstep().isActive();
            }
            [[nodiscard]] static bool readLocalArmy( const LockstepSkirmishGameModeComponent& gameMode, float64& outValue )
            {
                outValue = static_cast<float64>( gameMode.getMatch().makeSummary( gameMode.getLocalPlayer() )._army );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_COMPONENT_WATCH( skirmishLockstepPlayer, "Skirmish.LockstepPlayer", "Player this process runs in the network match (0 host, 1 client; no value outside one)", LockstepSkirmishGameModeComponent,
                                   &LockstepSkirmishGameModeComponentInternal::readPlayer );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishLockstepTick, "Skirmish.LockstepTick", "Lockstep tick the network match reached", LockstepSkirmishGameModeComponent, &LockstepSkirmishGameModeComponentInternal::readTick );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishLockstepDesynced, "Skirmish.LockstepDesynced", "1 when the checksums of the two processes differed", LockstepSkirmishGameModeComponent,
                                   &LockstepSkirmishGameModeComponentInternal::readDesynced );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishLockstepClientCommands, "Skirmish.LockstepClientCommands", "Commands of player 1 (the joining client) applied by this process", LockstepSkirmishGameModeComponent,
                                   &LockstepSkirmishGameModeComponentInternal::readClientCommands );
    SW_AUTOMATION_COMPONENT_WATCH( skirmishLocalArmy, "Skirmish.LocalArmy", "Army units of this screen's player", LockstepSkirmishGameModeComponent, &LockstepSkirmishGameModeComponentInternal::readLocalArmy );

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
