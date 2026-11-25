#include "pch.h"

#include "Games/Playground/StarSkirmish/LockstepSkirmishGameModeComponent.h"

#include "Core/String/StringBuilder.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Service/GameService.h"

namespace sw
{
    namespace
    {
        struct LockstepSkirmishGameModeComponentInternal
        {
            /** @brief 개발 명령 `skirmish.lockstep` 입니다. */
            static bool runLockstepStatus( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.empty() == false )
                    return false;
                const SceneManager* const                      pScenes   = game::findSubsystem<SceneManager>();
                const Scene* const                             pScene    = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                const GameObjectManager* const                 pManager  = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                const SkirmishGameModeComponent* const         pMatch    = pManager != nullptr ? SkirmishGameModeComponent::findInScene( *pManager ) : nullptr;
                const LockstepSkirmishGameModeComponent* const pDirector = pMatch != nullptr ? pMatch->getOwner()->getComponent<LockstepSkirmishGameModeComponent>() : nullptr;
                if ( pDirector == nullptr )
                {
                    outReply = "skirmish: no lockstep skirmish director in the active scene";
                    return true;
                }
                const SkirmishLockstepLink& lockstep = pDirector->getLockstep();
                if ( lockstep.isActive() == false )
                {
                    outReply = "skirmish: not a network match";
                    return true;
                }
                const LockstepSession&                   session = lockstep.getSession();
                StringBuilder<constant::buffer::kMax256> replyBuilder;
                replyBuilder.appendFormat( "skirmish lockstep: player %# tick %# stalls %# desynced %# applied %# %#", lockstep.getLocalPlayer(), session.getCurrentTick(), session.getStallCount(),
                                           session.isDesynced() ? 1 : 0, lockstep.getAppliedCommandCount( 0 ), lockstep.getAppliedCommandCount( 1 ) );
                outReply = string( replyBuilder.view() );
                SW_LOG_INFO_ID( "LockstepSkirmish.Status", "[Skirmish] %#", outReply.c_str() );
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( SkirmishLockstepStatus, "skirmish.lockstep", "skirmish.lockstep", "Show the network match lockstep tick, stalls, desync and applied command counts",
                    &LockstepSkirmishGameModeComponentInternal::runLockstepStatus );

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

    void LockstepSkirmishGameModeComponent::stepMatch( float32 deltaTime )
    {
        if ( _lockstep.releaseLostSession() )
            SW_LOG_INFO_ID( "LockstepSkirmish.PeerLost", "[Skirmish] the other player left the match - it goes on here without the other player" );
        if ( _lockstep.isActive() )
            (void)_lockstep.advance( deltaTime, getWorld().getSettings()._fixedStep, getMutableMatch() ); // 두 프로세스가 같은 걸음으로 돈다 - 속도 · 일시정지는 쓰지 않는다
        else
            SkirmishGameModeComponent::stepMatch( deltaTime );
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
