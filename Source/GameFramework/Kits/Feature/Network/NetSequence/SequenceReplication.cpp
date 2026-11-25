#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSequence/SequenceReplication.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Network/Connection/NetEndpoint.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/NetObjectTable.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Sequencer/SequencePlayerComponent.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Character/Control/Pawn/PawnComponent.h"

namespace sw
{
    namespace
    {
        /** @brief @p director 오브젝트의 시퀀스 플레이어입니다. 오브젝트나 플레이어가 없으면 nullptr 입니다. */
        SequencePlayerComponent* findDirectorPlayer( GameObjectManager& manager, const GameObjectHandle& director )
        {
            GameObject* pDirector = manager.resolveGameObject( director );
            return pDirector != nullptr ? pDirector->getComponent<SequencePlayerComponent>() : nullptr;
        }
    } // namespace
} // namespace sw

namespace sw
{
    SequenceReplicationServer::SequenceReplicationServer()
        : _listPlay{}
        , _listSeenTrigger{}
        , _listJoiningConnection{}
        , _stats{}
        , _writer{}
        , _pHost{ nullptr }
        , _pManager{ nullptr }
        , _nextPlayID{ 1 }
    {
    }

    void SequenceReplicationServer::initialize( INetPeers* pHost, GameObjectManager* pManager )
    {
        _pHost    = pHost;
        _pManager = pManager;
        _listPlay.clear();
        _listSeenTrigger.clear();
        _listJoiningConnection.clear();
        _stats      = SequenceReplicationStats{};
        _nextPlayID = 1;
        if ( pManager == nullptr )
            return;
        pManager->getSubsystem<NetObjectTable>().captureSceneObjects( SceneIterationKey{} );
        // 시작 전에 튼 재생은 보내지 않는다 — 지금 발동 수가 기준이다.
        for ( SequenceTriggerComponent* pTrigger : pManager->getComponentRegistry().getAll<SequenceTriggerComponent>() )
        {
            _listSeenTrigger.push_back( SeenTrigger{ TComponentHandle<SequenceTriggerComponent>( pTrigger ), pTrigger->getActivationCount() } );
        }
    }

    void SequenceReplicationServer::update()
    {
        if ( _pHost == nullptr || _pManager == nullptr )
            return;
        // 끝을 먼저 보낸다 — 같은 디렉터를 새 재생이 쓰면 클라이언트가 옛 재생을 먼저 놓아야 한다.
        sendEnds();
        sendToJoining();
        sendNewPlays();
        _listJoiningConnection.clear();
    }

    NetHandleResult SequenceReplicationServer::receive( NetSequenceMessage::SkipRequest, const NetMessageContext& context, const SequenceNetSkipRequestBody& body )
    {
        // 클라이언트가 보낸 값은 믿지 않는다: 살아 있는 재생 · 그 플레이어만의 재생 · 요청한 연결이 그 폰을 가진 연결일 때만 서버 재생을 끝낸다.
        // 위조 · 늦은 요청(이미 끝난 재생)은 셈만 한다 — 연결마다 로그를 남기면 요청으로 로그를 채울 수 있다.
        const Play*              pPlay   = findPlay( body._playID );
        const bool               bOwner  = pPlay != nullptr && pPlay->_audience == SequenceTriggerAudience::Activator && pPlay->_targetConnection == context._connectionID;
        SequencePlayerComponent* pPlayer = bOwner && _pManager != nullptr ? findDirectorPlayer( *_pManager, pPlay->_director ) : nullptr;
        if ( pPlayer == nullptr || pPlayer->skipToEnd() == false )
        {
            ++_stats._skipRejectedCount;
            return NetHandleResult::Handled;
        }
        ++_stats._skipAcceptedCount; // 끝 메시지는 다음 `update` 가 디렉터가 멈춘 것을 보고 보낸다
        return NetHandleResult::Handled;
    }

    void SequenceReplicationServer::onPeerEnteredWorld( int32 connectionID )
    {
        _listJoiningConnection.push_back( connectionID );
    }

    void SequenceReplicationServer::onPeerLeftWorld( int32 connectionID )
    {
        VectorUtil::removeFirst( _listJoiningConnection, connectionID );
    }

    SequenceReplicationServer::Play* SequenceReplicationServer::findPlay( uint32 playID )
    {
        for ( Play& play : _listPlay )
        {
            if ( play._playID == playID )
                return &play;
        }
        return nullptr;
    }

    bool SequenceReplicationServer::makePlay( const SequenceTriggerComponent& trigger, const SequenceTriggerPlay& triggerPlay, Play& outPlay )
    {
        const NetObjectTable& table      = _pManager->getSubsystem<NetObjectTable>();
        const GameObject*     pDirector  = _pManager->resolveGameObject( trigger.getSequence() );
        const GameObject*     pActivator = _pManager->resolveGameObject( triggerPlay._activator );
        const NetObjectID     director   = pDirector != nullptr ? table.findID( *pDirector ) : NetObjectID{ 0 };
        const NetObjectID     activator  = pActivator != nullptr ? table.findID( *pActivator ) : NetObjectID{ 0 };
        if ( director == 0 || activator == 0 )
        {
            SW_LOG_ERROR_ID( "SequenceReplication.PlayNotReplicated", "Sequence trigger '%#' played on the server but is not replicated: its %# has no net id (not a captured scene object and not a registered spawned object)",
                             trigger.getOwner() != nullptr ? trigger.getOwner()->getName().c_str() : "?", director == 0 ? "sequence object" : "activator" );
            return false;
        }
        outPlay                 = Play{};
        outPlay._director       = triggerPlay._director;
        outPlay._activator      = triggerPlay._activator;
        outPlay._directorNetID  = director;
        outPlay._activatorNetID = activator;
        outPlay._audience       = trigger.getAudience();
        if ( outPlay._audience == SequenceTriggerAudience::Activator )
        {
            const PawnComponent*  pPawn      = pActivator->getComponent<PawnComponent>();
            const optional<int32> connection = pPawn != nullptr ? pPawn->findInputConnection() : optional<int32>{};
            if ( connection.has_value() )
                outPlay._targetConnection = *connection;
            // 가진 연결이 없는 폰(리슨 서버 호스트 · 혼자 조종)은 서버 월드에서만 돈다 — 원격에 보낼 곳이 없다.
        }
        return true;
    }

    bool SequenceReplicationServer::writeStart( const Play& play )
    {
        const SequencePlayerComponent* pPlayer = findDirectorPlayer( *_pManager, play._director );
        if ( pPlayer == nullptr || pPlayer->isPlaying() == false )
            return false;
        SequenceNetStartBody body;
        body._playID     = play._playID;
        body._director   = play._directorNetID;
        body._activator  = play._activatorNetID;
        body._frame      = static_cast<uint32>( pPlayer->getCurrentFrame() > 0 ? pPlayer->getCurrentFrame() : 0 );
        body._audience   = static_cast<uint8>( play._audience );
        body._serverTick = _pManager->getSubsystem<SceneNetLink>().getServerTick();
        return _writer.writeMessage<NetSequenceMessage::Table, NetSequenceMessage::Start>( body );
    }

    uint32 SequenceReplicationServer::sendToAudience( const Play& play )
    {
        if ( play._audience == SequenceTriggerAudience::Everyone )
            return static_cast<uint32>( _writer.broadcastMessage( *_pHost ) );
        if ( play._targetConnection < 0 )
            return 0;
        if ( _writer.sendMessage( *_pHost, play._targetConnection ) )
            return 1;
        ++_stats._sendRejectedCount; // 대상이 떠났다
        return 0;
    }

    void SequenceReplicationServer::sendEnds()
    {
        for ( size_t index = _listPlay.size(); index > 0; --index )
        {
            const Play&                    play    = _listPlay[index - 1];
            const SequencePlayerComponent* pPlayer = findDirectorPlayer( *_pManager, play._director );
            if ( pPlayer != nullptr && pPlayer->isPlaying() )
                continue;
            SequenceNetEndBody body;
            body._playID = play._playID;
            if ( _writer.writeMessage<NetSequenceMessage::Table, NetSequenceMessage::End>( body ) )
                _stats._endSentCount += sendToAudience( play );
            _listPlay.erase( _listPlay.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }
    }

    void SequenceReplicationServer::sendToJoining()
    {
        if ( _listJoiningConnection.empty() )
            return;
        for ( const Play& play : _listPlay )
        {
            // 그 플레이어만의 재생은 그 폰을 가진 연결 하나의 것이다 — 새 연결은 그 폰을 갖지 않는다.
            if ( play._audience != SequenceTriggerAudience::Everyone || writeStart( play ) == false )
                continue;
            for ( const int32 connectionID : _listJoiningConnection )
            {
                if ( _writer.sendMessage( *_pHost, connectionID ) )
                    ++_stats._startSentCount;
                else
                    ++_stats._sendRejectedCount;
            }
        }
    }

    void SequenceReplicationServer::startPlay( const SequenceTriggerComponent& trigger, const SequenceTriggerPlay& triggerPlay )
    {
        Play play{};
        if ( makePlay( trigger, triggerPlay, play ) == false )
        {
            ++_stats._unreplicableCount;
            return;
        }
        play._playID = _nextPlayID++;
        if ( writeStart( play ) == false )
            return; // 같은 프레임에 끝났다(건너뛰기) — 보낼 재생이 없다
        _stats._startSentCount += sendToAudience( play );
        _listPlay.push_back( play );
    }

    void SequenceReplicationServer::sendNewPlays()
    {
        for ( SequenceTriggerComponent* pTrigger : _pManager->getComponentRegistry().getAll<SequenceTriggerComponent>() )
        {
            const TComponentHandle<SequenceTriggerComponent> handle( pTrigger );
            SeenTrigger*                                     pSeen = nullptr;
            for ( SeenTrigger& seen : _listSeenTrigger )
            {
                if ( seen._trigger == handle )
                    pSeen = &seen;
            }
            if ( pSeen == nullptr )
            {
                _listSeenTrigger.push_back( SeenTrigger{ handle, 0 } ); // 시작 뒤에 생긴 트리거(스트리밍 셀) — 0 부터 센다
                pSeen = &_listSeenTrigger.back();
            }
            if ( pTrigger->getActivationCount() == pSeen->_activationCount )
                continue;
            // 한 갱신 사이에 여러 플레이어가 들어올 수 있다 — 본 번호 뒤의 재생을 모두 보낸다(이미 거둔 재생은 끝난 것이라 보낼 것이 없다).
            const uint32 seenCount  = pSeen->_activationCount;
            pSeen->_activationCount = pTrigger->getActivationCount();
            for ( const SequenceTriggerPlay& triggerPlay : pTrigger->getPlays() )
            {
                if ( triggerPlay._serial > seenCount )
                    startPlay( *pTrigger, triggerPlay );
            }
        }
        // 사라진 트리거(셀이 내려감 · 파괴)의 기록은 지운다 — 다시 올라오면 0 부터 센다(트리거의 발동 수도 다시 0 이다).
        VectorUtil::removeIf( _listSeenTrigger, [this]( const SeenTrigger& seen )
        { return _pManager->resolveComponent( seen._trigger ) == nullptr; } );
    }
} // namespace sw

namespace sw
{
    SequenceReplicationClient::SequenceReplicationClient()
        : _listPlay{}
        , _stats{}
        , _writer{}
        , _pHost{ nullptr }
        , _pManager{ nullptr }
    {
    }

    void SequenceReplicationClient::initialize( INetPeers* pHost, GameObjectManager* pManager )
    {
        _pHost    = pHost;
        _pManager = pManager;
        _stats    = SequenceReplicationStats{};
        _listPlay.clear();
        if ( pManager != nullptr )
            pManager->getSubsystem<NetObjectTable>().captureSceneObjects( SceneIterationKey{} );
    }

    void SequenceReplicationClient::update()
    {
        if ( _pHost == nullptr || _pManager == nullptr )
            return;
        for ( Play& play : _listPlay )
        {
            if ( play._audience != SequenceTriggerAudience::Activator || play._bSkipRequested == SW_TRUE )
                continue;
            const SequencePlayerComponent* pPlayer = findDirectorPlayer( *_pManager, play._director );
            if ( pPlayer != nullptr && pPlayer->isPlaying() )
                continue;
            // 서버의 끝보다 먼저 끝났다 — 로컬 건너뛰기(`Cutscene.Skip`)다(제 속도로 끝났으면 서버도 곧 끝나 요청은 무해하다). 서버가 소유 연결로 검증한다.
            SequenceNetSkipRequestBody body;
            body._playID = play._playID;
            if ( _writer.writeMessage<NetSequenceMessage::Table, NetSequenceMessage::SkipRequest>( body ) && _writer.sendMessage( *_pHost, 0 ) )
                ++_stats._skipRequestCount;
            play._bSkipRequested = SW_TRUE;
        }
    }

    void SequenceReplicationClient::onWorldOpened( uint32 generation )
    {
        (void)generation;
        _listPlay.clear();
    }

    NetHandleResult SequenceReplicationClient::receive( NetSequenceMessage::Start, const NetMessageContext& context, const SequenceNetStartBody& body )
    {
        (void)context;
        if ( _pManager == nullptr )
            return NetHandleResult::Handled; // 시작하지 않은 클라이언트 — 받을 씬이 없다
        const NetObjectTable&         table      = _pManager->getSubsystem<NetObjectTable>();
        GameObject*                   pDirector  = table.findObject( body._director );
        const GameObject*             pActivator = table.findObject( body._activator );
        SequencePlayerComponent*      pPlayer    = pDirector != nullptr ? pDirector->getComponent<SequencePlayerComponent>() : nullptr;
        const SequenceTriggerAudience audience   = static_cast<SequenceTriggerAudience>( body._audience );
        if ( pPlayer == nullptr )
        {
            SW_LOG_ERROR_ID( "SequenceReplication.StartNoPlayer", "Replicated sequence play %# rejected: net object %# is not an object with a SequencePlayerComponent here", body._playID, body._director );
            ++_stats._startRejectedCount;
            return NetHandleResult::Handled;
        }
        if ( audience == SequenceTriggerAudience::Activator )
        {
            // 서버가 그 폰을 가진 연결에만 보낸다 — 이 월드의 로컬 폰이 아니면 서버 · 번호표가 어긋난 것이다(다른 플레이어의 컷씬을 틀지 않는다).
            const PawnComponent* pPawn  = pActivator != nullptr ? pActivator->getComponent<PawnComponent>() : nullptr;
            const bool           bLocal = pPawn != nullptr && ControlSystem::isLocallyControlled( *_pManager, *pPawn );
            if ( bLocal == false )
            {
                SW_LOG_ERROR_ID( "SequenceReplication.StartNotLocalActivator", "Replicated sequence play %# rejected: it plays only for its activator but net object %# is not this client's locally controlled pawn",
                                 body._playID, body._activator );
                ++_stats._startRejectedCount;
                return NetHandleResult::Handled;
            }
        }
        // 같은 디렉터의 옛 재생은 서버가 끝을 먼저 보냈다 — 번호가 같으면 늦은 참가의 다시 보냄이라 바꾼다.
        VectorUtil::removeIf( _listPlay, [&body]( const Play& play )
        { return play._playID == body._playID; } );
        pPlayer->playFromFrame( static_cast<int32>( body._frame ) );
        if ( pPlayer->isPlaying() == false )
        {
            ++_stats._startRejectedCount; // 데이터 결함 — 플레이어가 오류를 남겼다
            return NetHandleResult::Handled;
        }
        _listPlay.push_back( Play{ body._playID, pDirector->getHandle(), audience, SW_FALSE } );
        ++_stats._startAppliedCount;
        pPlayer->advance( _pManager->getSubsystem<SceneNetLink>().computeSecondsSince( body._serverTick ) ); // 오는 동안 흐른 만큼 따라잡는다
        return NetHandleResult::Handled;
    }

    NetHandleResult SequenceReplicationClient::receive( NetSequenceMessage::End, const NetMessageContext& context, const SequenceNetEndBody& body )
    {
        (void)context;
        if ( _pManager == nullptr )
            return NetHandleResult::Handled;
        for ( size_t index = 0; index < _listPlay.size(); ++index )
        {
            if ( _listPlay[index]._playID != body._playID )
                continue;
            SequencePlayerComponent* pPlayer = findDirectorPlayer( *_pManager, _listPlay[index]._director );
            if ( pPlayer != nullptr && pPlayer->skipToEnd() )
                ++_stats._endAppliedCount; // 서버가 먼저 끝냈다(건너뛰기 · 멈춤) — 끝 상태로 점프한다
            _listPlay.erase( _listPlay.begin() + static_cast<ptrdiff_t>( index ) );
            break;
        }
        return NetHandleResult::Handled; // 모르는 번호(이 연결 전의 재생 · 거절한 시작)는 할 일이 없다
    }
} // namespace sw
