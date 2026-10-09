#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Message/NetSendBudget.h"

namespace sw
{
    ReplicationServer::ReplicationServer()
        : _listClient{}
        , _world{}
        , _settings{}
        , _pHost{ nullptr }
        , _pPolicy{ nullptr }
        , _defaultPolicy{}
        , _parallel{}
        , _snapshotScratch{}
        , _listConnectionScratch{}
        , _listClientScratch{}
        , _worldEntityCount{ 0 }
        , _oversizedEntityCount{ 0 }
        , _snapshotBudgetBytes{ 0 }
        , _pRangeConnection{ nullptr }
        , _ppRangeClient{ nullptr }
    {
    }

    void ReplicationServer::initialize( NetHost* pHost, const ReplicationServerSettings& settings, const IReplicationPolicy* pPolicy )
    {
        _pHost    = pHost;
        _settings = settings;
        _pPolicy  = pPolicy != nullptr ? pPolicy : &_defaultPolicy;
        _listClient.clear();
        _world            = NetSnapshot{};
        _worldEntityCount = 0;
    }

    ReplicationServer::ClientState& ReplicationServer::acquireClient( int32 connectionId )
    {
        if ( connectionId >= static_cast<int32>( _listClient.size() ) )
            _listClient.resize( static_cast<size_t>( connectionId + 1 ) );
        ClientState& client = _listClient[static_cast<size_t>( connectionId )];
        if ( client._bActive == SW_FALSE )
        {
            client          = ClientState{};
            client._bActive = SW_TRUE;
            client._listSent.initialize( MathUtil::max( 2, _settings._historySize ) );
            client._input.initialize( MathUtil::max( 1, _settings._inputBufferSize ), NetClientServerMessage::kInputFormat, NetInputWindowMode::FollowNewest );
        }
        return client;
    }

    void ReplicationServer::resetClient( int32 connectionId )
    {
        if ( connectionId >= 0 && connectionId < static_cast<int32>( _listClient.size() ) )
            _listClient[static_cast<size_t>( connectionId )] = ClientState{};
    }

    void ReplicationServer::onConnectionOpened( int32 connectionId ) { resetClient( connectionId ); }

    void ReplicationServer::onConnectionClosed( int32 connectionId, NetDisconnectReason reason )
    {
        (void)reason;
        resetClient( connectionId );
    }

    uint16 ReplicationServer::getMessageKindMask() const
    {
        return static_cast<uint16>( ( 1u << ( NetClientServerMessage::kSnapshotAck - NetKitMessageRange::kClientServer ) ) |
                                    ( 1u << ( NetClientServerMessage::kInput - NetKitMessageRange::kClientServer ) ) );
    }

    void ReplicationServer::beginTick( uint32 tick )
    {
        _world._tick      = tick;
        _worldEntityCount = 0; // 자리는 지우지 않는다 — `setEntity` 가 덮어쓰고 `endTick` 이 남는 것을 자른다(버퍼 용량을 다시 쓴다)
    }

    void ReplicationServer::setEntity( uint32 entityId, uint32 typeId, const vector<uint8>& buffer )
    {
        if ( static_cast<int32>( buffer.size() ) > NetSnapshot::kMaxEntityBytes )
        {
            // 월드에는 넣는다(빼면 클라이언트에서 사라진다) — 델타가 싣지 않아 클라이언트는 마지막으로 받은 상태에 머문다.
            if ( _oversizedEntityCount == 0 )
                SW_LOG_WARNING( "ReplicationServer: entity %# has %# state bytes, more than the snapshot limit %# - it is not replicated (further ones are only counted)",
                                entityId, static_cast<int32>( buffer.size() ), NetSnapshot::kMaxEntityBytes );
            ++_oversizedEntityCount;
        }
        if ( _worldEntityCount == _world._listEntity.size() )
            _world._listEntity.emplace_back();
        NetEntityState& entity = _world._listEntity[_worldEntityCount++];
        entity._entityId       = entityId;
        entity._typeId         = typeId;
        entity._buffer         = buffer; // 복사 대입 — 자리의 용량을 다시 쓴다
    }

    void ReplicationServer::endTick()
    {
        _world._listEntity.resize( _worldEntityCount );
        _world.sortEntities();
    }

    void ReplicationServer::setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold ) { _parallel.setTaskManager( pTaskManager, serialThreshold ); }

    void ReplicationServer::sendSnapshots()
    {
        if ( _pHost == nullptr )
            return;
        // 이번 틱의 예산 — 설정과 연결 상한의 몫 중 작은 것(호스트 잠금은 여기서 한 번, 워커는 이 값만 읽는다).
        _snapshotBudgetBytes = NetSendBudget::computeTickBudget( _settings._snapshotBudgetBytes, _pHost->getMaxBytesPerSecond(), static_cast<float64>( _settings._tickInterval ) );
        // 클라이언트 상태는 나누기 전에 모두 잡는다 — `acquireClient` 는 목록을 키울 수 있다(나누는 중에는 아무도 목록을 건드리지 않는다).
        _pHost->collectConnected( _listConnectionScratch );
        _listClientScratch.resize( _listConnectionScratch.size() );
        for ( size_t index = 0; index < _listConnectionScratch.size(); ++index )
        {
            acquireClient( _listConnectionScratch[index] );
        }
        for ( size_t index = 0; index < _listConnectionScratch.size(); ++index )
        {
            _listClientScratch[index] = &_listClient[static_cast<size_t>( _listConnectionScratch[index] )];
        }
        _snapshotScratch.prepare( _parallel );
        _pRangeConnection = _listConnectionScratch.data();
        _ppRangeClient    = _listClientScratch.data();
        _parallel.run( static_cast<uint32>( _listConnectionScratch.size() ),
                       SW_DELEGATE_METHOD( ParallelBlockDelegate, &ReplicationServer::sendSnapshotRange, this ) );
    }

    void ReplicationServer::sendSnapshotRange( uint32 start, uint32 end )
    {
        SnapshotScratch& scratch = _snapshotScratch.acquire( _parallel );
        for ( uint32 index = start; index < end; ++index )
        {
            sendSnapshot( _pRangeConnection[index], *_ppRangeClient[index], scratch );
        }
    }

    void ReplicationServer::sendSnapshot( int32 connectionId, ClientState& client, SnapshotScratch& scratch )
    {
        // 이 클라이언트에게 관련 있는 것만 — 엔티티 버퍼는 자리에 덮어써 용량을 남긴다.
        NetSnapshot& filtered            = scratch._filtered;
        filtered._tick                   = _world._tick;
        filtered._lastProcessedInputTick = client._lastProcessedInputTick;
        filtered._firstMissingInputTick  = client._input.getFirstMissingTick(); // 여러 워커가 읽기만 한다 — 입력은 보내기 전 게임 스레드가 넣었다
        NetPrioritizer& prioritizer      = client._prioritizer;
        prioritizer.beginAccumulate();
        size_t relevantCount = 0;
        for ( const NetEntityState& entity : std::as_const( _world._listEntity ) ) // 여러 워커가 함께 읽는다
        {
            if ( _pPolicy->isRelevant( connectionId, entity ) == false )
                continue;
            if ( relevantCount < filtered._listEntity.size() )
                filtered._listEntity[relevantCount] = entity;
            else
                filtered._listEntity.push_back( entity );
            ++relevantCount;
            prioritizer.accumulate( entity._entityId, _pPolicy->computePriority( connectionId, entity ), 1.0f ); // 스냅샷 하나 = 한 번
        }
        filtered._listEntity.resize( relevantCount );
        prioritizer.removeUntouched(); // 더는 관련 없는 엔티티는 잊는다(다시 관련되면 0 에서)
        resolveUnconfirmedSends( client, filtered );
        // 쌓인 것이 큰 것부터 — 관련 엔티티는 id 순이라 자리를 이분 탐색으로 찾는다.
        vector<uint32>& listOrderEntity = scratch._listOrderEntity;
        prioritizer.collectOrder( listOrderEntity );
        vector<int32>& listOrder = scratch._listOrder;
        listOrder.resize( listOrderEntity.size() );
        for ( size_t index = 0; index < listOrderEntity.size(); ++index )
        {
            listOrder[index] = static_cast<int32>( filtered.findEntity( listOrderEntity[index] ) - filtered._listEntity.data() );
        }

        // 기준 — 클라이언트가 확인한 틱의 우리가 보낸 재구성. 너무 오래돼 덮였으면 기준 없이.
        const NetSnapshot* pBaseline = nullptr;
        if ( client._bHasAck )
        {
            const NetSnapshot* pCandidate = client._listSent.find( client._ackedTick );
            const bool         bRecent    = _world._tick - client._ackedTick < static_cast<uint32>( client._listSent.getCapacity() );
            if ( pCandidate != nullptr && bRecent )
                pBaseline = pCandidate;
        }
        BitWriter& writer = scratch._messageWriter.begin( NetClientServerMessage::kSnapshot );
        // 보낸 재구성은 그 틱의 자리에 바로 쓴다(기준 자리와 겹치면 — 같은 틱을 두 번 보낸다 — 사본을 거친다). 자리의 옛 버퍼는 남는다.
        NetSnapshot& slot = client._listSent.acquire( _world._tick );
        if ( &slot == pBaseline )
        {
            NetSnapshot written;
            filtered.writeDelta( writer, pBaseline, _snapshotBudgetBytes, written, &listOrder, &scratch._listCurrent );
            slot = std::move( written );
        }
        else
        {
            filtered.writeDelta( writer, pBaseline, _snapshotBudgetBytes, slot, &listOrder, &scratch._listCurrent );
        }
        // 받는 쪽이 이미 지금 상태인 것은 확정으로 0, 이번에 실은 것은 확인 기다리는 보냄으로 0 — 못 실은 것은 쌓인 채로 다음 스냅샷에서 앞선다.
        for ( size_t index = 0; index < filtered._listEntity.size(); ++index )
        {
            const uint32 entityId = filtered._listEntity[index]._entityId;
            if ( scratch._listCurrent[index] == NetSnapshot::kEntityAlreadyCurrent )
                prioritizer.markSent( entityId );
            else if ( scratch._listCurrent[index] == NetSnapshot::kEntityWritten )
                prioritizer.markSentUnconfirmed( entityId, _world._tick );
        }
        (void)scratch._messageWriter.send( *_pHost, connectionId, NetChannelType::UnreliableSequenced ); // 여러 스레드가 동시에 — NetHost 가 지킨다
    }

    void ReplicationServer::resolveUnconfirmedSends( ClientState& client, const NetSnapshot& filtered ) const
    {
        // 여러 워커가 클라이언트마다 따로 부른다 — 이 클라이언트의 상태만 쓰고 월드는 읽기만 한다.
        NetPrioritizer&    prioritizer = client._prioritizer;
        const NetSnapshot* pAcked      = client._bHasAck == SW_TRUE ? client._listSent.find( client._ackedTick ) : nullptr;
        const uint32       capacity    = static_cast<uint32>( client._listSent.getCapacity() );
        for ( const NetEntityState& entity : filtered._listEntity )
        {
            uint32 sentTick = 0;
            if ( prioritizer.findUnconfirmedSendTick( entity._entityId, sentTick ) == false )
                continue;
            const bool bAckedPast = client._bHasAck == SW_TRUE && client._ackedTick >= sentTick;
            const bool bExpired   = _world._tick - sentTick >= capacity; // 보낸 재구성이 고리에서 밀렸다 — 더 기다리지 않는다
            if ( bAckedPast == false && bExpired == false )
                continue; // 아직 오가는 중
            const NetSnapshot*    pSent      = bExpired ? nullptr : client._listSent.find( sentTick );
            const NetEntityState* pSentState = pSent != nullptr ? pSent->findEntity( entity._entityId ) : nullptr;
            const NetEntityState* pHave      = pAcked != nullptr ? pAcked->findEntity( entity._entityId ) : nullptr;
            const bool            bDelivered = pSentState != nullptr && pHave != nullptr && pHave->_typeId == pSentState->_typeId && pHave->_buffer == pSentState->_buffer;
            prioritizer.resolveSend( entity._entityId, bDelivered );
        }
    }

    NetHandleResult ReplicationServer::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        if ( context._connectionId < 0 )
            return NetHandleResult::Malformed;
        ClientState& client = acquireClient( context._connectionId );
        if ( context._kind == NetClientServerMessage::kSnapshotAck )
        {
            const uint32 tick = static_cast<uint32>( body.readVarUint() );
            if ( body.hasOverflowed() )
                return NetHandleResult::Malformed;
            if ( ( client._bHasAck == SW_FALSE || tick > client._ackedTick ) && tick <= _world._tick )
            {
                client._ackedTick = tick;
                client._bHasAck   = SW_TRUE;
            }
            return NetHandleResult::Handled;
        }
        return handleInput( client, body ) ? NetHandleResult::Handled : NetHandleResult::Malformed;
    }

    bool ReplicationServer::handleInput( ClientState& client, BitReader& reader )
    {
        const uint32 viewTick = static_cast<uint32>( reader.readVarUint() );
        if ( reader.hasOverflowed() )
            return false;
        // 이미 꺼낸 틱 · 이미 가진 틱은 버퍼 없이 넘기고, 길이 · 개수 상한을 넘거나 모자라면 하나도 넣지 않고 깨짐으로 본다.
        if ( client._input.read( reader ) == false )
            return false;
        client._viewTick = static_cast<float32>( viewTick ) / 256.0f;
        return true;
    }

    bool ReplicationServer::popInput( int32 connectionId, uint32 tick, vector<uint8>& outInputBuffer, bool& outbExact )
    {
        outbExact = false;
        if ( connectionId < 0 || connectionId >= static_cast<int32>( _listClient.size() ) )
            return false;
        ClientState& client = _listClient[static_cast<size_t>( connectionId )];
        // 그 틱 것이 있으면 그것, 없으면 지난번에 꺼낸 틱 뒤로 늦게 온 것 중 가장 새것(되풀이할 값을 새것으로 바꾼다).
        const NetInputEntry* pEntry = client._input.findLatestAtOrBefore( tick, client._input.getWindowFirst() );
        if ( pEntry != nullptr )
        {
            client._lastInput = pEntry->_bytes;
            client._bHasInput = SW_TRUE;
            outbExact         = pEntry->_tick == tick;
        }
        // 이 틱까지는 다 썼다 — 더 받지 않고, 확인이 넘어가 클라이언트가 다시 싣지 않는다.
        client._input.setWindow( tick + 1u, NetInputReceiveBuffer::kNoWindowEnd );
        if ( client._bHasInput == SW_FALSE )
            return false;
        outInputBuffer = client._lastInput;
        return true;
    }

    void ReplicationServer::setLastProcessedInputTick( int32 connectionId, uint32 tick )
    {
        if ( connectionId >= 0 && connectionId < static_cast<int32>( _listClient.size() ) )
            _listClient[static_cast<size_t>( connectionId )]._lastProcessedInputTick = tick;
    }

    float32 ReplicationServer::getClientViewTick( int32 connectionId ) const
    {
        return connectionId >= 0 && connectionId < static_cast<int32>( _listClient.size() ) ? _listClient[static_cast<size_t>( connectionId )]._viewTick : 0.0f;
    }

    uint64 ReplicationServer::getOversizedEntityCount() const { return _oversizedEntityCount; }

    uint32 ReplicationServer::getAckedTick( int32 connectionId ) const
    {
        return connectionId >= 0 && connectionId < static_cast<int32>( _listClient.size() ) ? _listClient[static_cast<size_t>( connectionId )]._ackedTick : 0;
    }
} // namespace sw
