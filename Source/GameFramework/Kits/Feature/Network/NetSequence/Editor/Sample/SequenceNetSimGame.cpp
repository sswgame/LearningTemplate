#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSequence/Editor/Sample/SequenceNetSimGame.h"

#include "Core/Container/vector.h"
#include "Core/Foundation/StdHeaders.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Kits/Feature/Network/NetSequence/SequenceNetSessionComponent.h"
#include "GameFramework/Kits/Feature/Network/NetSimulation/NetSimGameRegistry.h"
#include "GameFramework/Runtime/Character/Camera/SequenceTriggerComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/PlayerControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/PlayerStateComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/RemoteControllerComponent.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    namespace
    {
        constexpr const utf8* kSessionName    = "SessionState";
        constexpr const utf8* kNetSessionName = "NetSession";
        constexpr const utf8* kLocalName      = "LocalController";

        string makeControllerName( int32 playerNumber )
        {
            StringBuilder<constant::buffer::kMax32> name;
            name.appendFormat( "Controller%#", playerNumber );
            return string( name.view() );
        }

        void addDirector( GameObjectManager& manager, const utf8* name, int32 frameCount )
        {
            GameObject*              pDirector = manager.createGameObject( hashed_string( name ) );
            SequencePlayerComponent* pPlayer   = pDirector != nullptr ? pDirector->addComponent<SequencePlayerComponent>() : nullptr;
            if ( pPlayer == nullptr )
                return;
            SequenceAsset asset;
            asset._frameMin = 0;
            asset._frameMax = frameCount;
            pPlayer->setSequence( asset );
            pPlayer->setAutoPlay( false ); // 트리거가 튼다 — 켜 두면 모든 월드가 시작 때 스스로 틀어 서버 판정 · 대상이 의미를 잃는다
        }

        void addTrigger( GameObjectManager& manager, const utf8* name, const utf8* directorName, const utf8* flag, SequenceTriggerAudience audience )
        {
            GameObject*               pTriggerObject = manager.createGameObject( hashed_string( name ) );
            SequenceTriggerComponent* pTrigger       = pTriggerObject != nullptr ? pTriggerObject->addComponent<SequenceTriggerComponent>() : nullptr;
            const GameObject*         pDirector      = manager.findGameObjectByName( hashed_string( directorName ) );
            if ( pTrigger == nullptr || pDirector == nullptr )
                return;
            pTrigger->setSequence( pDirector->getHandle() );
            pTrigger->setProgressFlag( hashed_string( flag ) );
            pTrigger->setAudience( audience );
        }

        /** @brief 월드 하나의 세션입니다 — 등록한 게임이면 서버가 정해진 차례로 진입을 열고, 감시 값을 냅니다. */
        class SequenceNetSimSession final : public INetSimSession
        {
        public:
            SequenceNetSimSession( NetSimWorld& world, SequenceNetSimSchedule schedule )
                : _pWorld{ &world }
                , _scheduleTick{ 0 }
                , _schedule{ schedule }
                , _bSecondOpened{ SW_FALSE }
            {
            }

            void onTickEnd( NetSimWorld& world, float32 deltaTime ) override
            {
                (void)deltaTime;
                if ( world.isServer() == false || _schedule != SequenceNetSimSchedule::Scheduled )
                    return;
                if ( _scheduleTick == 0 )
                {
                    vector<int32> listConnection;
                    world.getPeers().collectConnected( listConnection );
                    if ( listConnection.size() >= 2 )
                        _scheduleTick = world.getLocalTick() + SequenceNetSimGame::kScheduleDelay;
                    return;
                }
                if ( world.getLocalTick() == _scheduleTick )
                {
                    (void)SequenceNetSimGame::activateTrigger( world, SequenceNetSimGame::kEveryoneTrigger, 1 );
                    (void)SequenceNetSimGame::activateTrigger( world, SequenceNetSimGame::kPlayerTrigger, 1 );
                    return;
                }
                const bool bFirstDone = world.getLocalTick() > _scheduleTick && SequenceNetSimGame::isTriggerPlaying( world, SequenceNetSimGame::kPlayerTrigger ) == false;
                if ( bFirstDone && _bSecondOpened == SW_FALSE )
                {
                    (void)SequenceNetSimGame::activateTrigger( world, SequenceNetSimGame::kPlayerTrigger, 2 );
                    _bSecondOpened = SW_TRUE;
                }
            }

            bool readValue( string_view key, float64& outValue ) const override
            {
                const SequencePlayerComponent* pEveryone = SequenceNetSimGame::findDirector( *_pWorld, SequenceNetSimGame::kEveryoneDirector );
                const SequencePlayerComponent* pPlayer   = SequenceNetSimGame::findDirector( *_pWorld, SequenceNetSimGame::kPlayerDirector );
                if ( key == "EveryonePlaying" && pEveryone != nullptr )
                    outValue = pEveryone->isPlaying() ? 1.0 : 0.0;
                else if ( key == "PlayerPlaying" && pPlayer != nullptr )
                    outValue = pPlayer->isPlaying() ? 1.0 : 0.0;
                else if ( key == "EveryoneFrame" && pEveryone != nullptr )
                    outValue = static_cast<float64>( pEveryone->getCurrentFrame() );
                else if ( key == "PlayerStarts" && pPlayer != nullptr )
                    outValue = static_cast<float64>( pPlayer->getHistory().countKind( SequenceHistoryKind::Started ) );
                else
                    return readServerValue( key, outValue );
                return true;
            }

        private:
            bool readServerValue( string_view key, float64& outValue ) const
            {
                if ( _pWorld->isServer() == false )
                    return false;
                const SequenceNetSessionComponent* pNet = SequenceNetSimGame::findNetSession( *_pWorld );
                if ( key == "SkipRejected" && pNet != nullptr )
                {
                    outValue = static_cast<float64>( pNet->getServer().getStats()._skipRejectedCount );
                    return true;
                }
                if ( key == "SkipAccepted" && pNet != nullptr )
                {
                    outValue = static_cast<float64>( pNet->getServer().getStats()._skipAcceptedCount );
                    return true;
                }
                if ( key == "EveryoneSeen" )
                {
                    const GameObject*         pSessionObject = _pWorld->getObjectManager().findGameObjectByName( hashed_string( kSessionName ) );
                    const GameStateComponent* pSession       = pSessionObject != nullptr ? pSessionObject->getComponent<GameStateComponent>() : nullptr;
                    if ( pSession == nullptr )
                        return false;
                    outValue = static_cast<float64>( pSession->getFlags().getFlag( hashed_string( SequenceNetSimGame::kEveryoneFlag ) ) );
                    return true;
                }
                for ( int32 playerNumber = 1; playerNumber <= SequenceNetSimGame::kMaxPlayerCount; ++playerNumber )
                {
                    fixed_string<constant::buffer::kMax32> seenKey;
                    formatstring( seenKey.data(), seenKey.capacity(), "PlayerSeen%#", playerNumber );
                    if ( key != seenKey.view() )
                        continue;
                    const PlayerStateComponent* pState = SequenceNetSimGame::findPlayerState( *_pWorld, playerNumber );
                    if ( pState == nullptr )
                        return false;
                    outValue = static_cast<float64>( pState->getProgressFlags().getFlag( hashed_string( SequenceNetSimGame::kPlayerFlag ) ) );
                    return true;
                }
                return false;
            }

        private:
            NetSimWorld*           _pWorld;       ///< 이 세션을 가진 월드(월드가 세션보다 오래 산다)
            uint32                 _scheduleTick; ///< 첫 진입을 열 서버 틱(0 = 클라이언트 둘을 기다린다)
            SequenceNetSimSchedule _schedule;
            uint8                  _bSecondOpened; ///< 둘째 플레이어의 진입을 열었다
        };
    } // namespace

    SequenceNetSimGame::SequenceNetSimGame( SequenceNetSimSchedule schedule )
        : _schedule{ schedule }
    {
    }

    unique_ptr<INetSimGame> SequenceNetSimGame::create()
    {
        return make_unique<SequenceNetSimGame>( SequenceNetSimSchedule::Scheduled );
    }

    bool SequenceNetSimGame::isTriggerPlaying( NetSimWorld& world, const utf8* triggerName )
    {
        const GameObjectManager&        manager  = world.getObjectManager();
        const GameObject*               pObject  = manager.findGameObjectByName( hashed_string( triggerName ) );
        const SequenceTriggerComponent* pTrigger = pObject != nullptr ? pObject->getComponent<SequenceTriggerComponent>() : nullptr;
        if ( pTrigger == nullptr )
            return false;
        for ( const SequenceTriggerPlay& play : pTrigger->getPlays() )
        {
            const GameObject*              pDirector = manager.resolveGameObject( play._director );
            const SequencePlayerComponent* pPlayer   = pDirector != nullptr ? pDirector->getComponent<SequencePlayerComponent>() : nullptr;
            if ( pPlayer != nullptr && pPlayer->isPlaying() )
                return true;
        }
        return false;
    }

    string SequenceNetSimGame::makePawnName( int32 playerNumber )
    {
        StringBuilder<constant::buffer::kMax32> name;
        name.appendFormat( "Pawn%#", playerNumber );
        return string( name.view() );
    }

    unique_ptr<INetSimSession> SequenceNetSimGame::createSession( NetSimWorld& world )
    {
        GameObjectManager& manager        = world.getObjectManager();
        GameObject*        pSessionObject = manager.createGameObject( hashed_string( kSessionName ) );
        if ( pSessionObject != nullptr )
        {
            GameStateComponent* pSession = pSessionObject->addComponent<GameStateComponent>();
            if ( pSession != nullptr )
                (void)pSession->initialize( GameStateSettings{} );
        }
        addDirector( manager, kEveryoneDirector, kEveryoneFrames );
        addDirector( manager, kPlayerDirector, kPlayerFrames );
        addTrigger( manager, kEveryoneTrigger, kEveryoneDirector, kEveryoneFlag, SequenceTriggerAudience::Everyone );
        addTrigger( manager, kPlayerTrigger, kPlayerDirector, kPlayerFlag, SequenceTriggerAudience::Activator );
        for ( int32 playerNumber = 1; playerNumber <= kMaxPlayerCount; ++playerNumber )
        {
            GameObject*    pPawnObject = manager.createGameObject( hashed_string( makePawnName( playerNumber ) ) );
            PawnComponent* pPawn       = pPawnObject != nullptr ? pPawnObject->addComponent<PawnComponent>() : nullptr;
            if ( pPawn != nullptr )
            {
                // 본보기 폰은 움직이지도 보지도 않는다(컷씬을 켜는 주인일 뿐이다) — 게임의 입력 맵이 없는 하니스 월드에서 축을 묻지 않는다.
                pPawn->setMoveAction( hashed_string{} );
                pPawn->setLookAction( hashed_string{} );
            }
            if ( pPawn == nullptr || world.isServer() == false )
                continue;
            // 언리얼 서버가 접속마다 PlayerController 를 세우는 자리 — 원격 조종자가 플레이어 상태를 들고 그 연결의 폰을 쥔다.
            RemoteControllerComponent* pRemote = RemoteControllerComponent::spawnForConnection( manager, hashed_string( makeControllerName( playerNumber ) ), playerNumber - 1, *pPawn );
            if ( pRemote != nullptr )
                (void)pRemote->getOwner()->addComponent<PlayerStateComponent>();
        }
        if ( world.isServer() == false )
        {
            GameObject*                pLocal  = manager.createGameObject( hashed_string( kLocalName ) );
            PlayerControllerComponent* pPlayer = pLocal != nullptr ? pLocal->addComponent<PlayerControllerComponent>() : nullptr;
            GameObject*                pOwn    = manager.findGameObjectByName( hashed_string( makePawnName( world.getWorldIndex() ) ) );
            PawnComponent*             pPawn   = pOwn != nullptr ? pOwn->getComponent<PawnComponent>() : nullptr;
            if ( pPlayer != nullptr && pPawn != nullptr )
            {
                (void)pLocal->addComponent<PlayerStateComponent>();
                pPlayer->possess( *pPawn );
            }
        }
        GameObject* pNet = manager.createGameObject( hashed_string( kNetSessionName ) );
        if ( pNet != nullptr )
            (void)pNet->addComponent<SequenceNetSessionComponent>();
        return make_unique<SequenceNetSimSession>( world, _schedule );
    }

    bool SequenceNetSimGame::activateTrigger( NetSimWorld& world, const utf8* triggerName, int32 playerNumber )
    {
        GameObjectManager&        manager        = world.getObjectManager();
        GameObject*               pTriggerObject = manager.findGameObjectByName( hashed_string( triggerName ) );
        SequenceTriggerComponent* pTrigger       = pTriggerObject != nullptr ? pTriggerObject->getComponent<SequenceTriggerComponent>() : nullptr;
        const GameObject*         pPawn          = manager.findGameObjectByName( hashed_string( makePawnName( playerNumber ) ) );
        return pTrigger != nullptr && pTrigger->activate( pPawn );
    }

    SequencePlayerComponent* SequenceNetSimGame::findDirector( NetSimWorld& world, const utf8* directorName )
    {
        GameObject* pDirector = world.getObjectManager().findGameObjectByName( hashed_string( directorName ) );
        return pDirector != nullptr ? pDirector->getComponent<SequencePlayerComponent>() : nullptr;
    }

    SequenceNetSessionComponent* SequenceNetSimGame::findNetSession( NetSimWorld& world )
    {
        GameObject* pNet = world.getObjectManager().findGameObjectByName( hashed_string( kNetSessionName ) );
        return pNet != nullptr ? pNet->getComponent<SequenceNetSessionComponent>() : nullptr;
    }

    PlayerStateComponent* SequenceNetSimGame::findPlayerState( NetSimWorld& world, int32 playerNumber )
    {
        GameObject* pController = world.getObjectManager().findGameObjectByName( hashed_string( makeControllerName( playerNumber ) ) );
        return pController != nullptr ? pController->getComponent<PlayerStateComponent>() : nullptr;
    }
} // namespace sw

namespace sw::editor
{
    namespace
    {
        /** @brief 이 확장 모듈이 로드된 동안 본보기 게임을 한 프로세스 재생의 게임 목록에 등록합니다. 실제 게임은 자기 모듈에서 같은 줄로 등록한다. */
        const NetSimGameRegistrar s_sequenceNetSimRegistrar{ SequenceNetSimGame::kName, &SequenceNetSimGame::create };
    } // namespace
} // namespace sw::editor
