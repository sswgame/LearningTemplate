#include "pch.h"

#include "GameFramework/Kits/Network/NetClientServer/ReplicationServer.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetHost.h"

#include <algorithm>

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
        , _oversizedEntityCount{ 0 }
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
        _world = NetSnapshot{};
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
            client._listSent.resize( static_cast<size_t>( MathUtil::max( 2, _settings._historySize ) ) );
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
        _world._tick = tick;
        _world._listEntity.clear();
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
        NetEntityState entity;
        entity._entityId = entityId;
        entity._typeId   = typeId;
        entity._buffer   = buffer;
        _world._listEntity.push_back( std::move( entity ) );
    }

    void ReplicationServer::endTick() { _world.sortEntities(); }

    void ReplicationServer::setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold ) { _parallel.setTaskManager( pTaskManager, serialThreshold ); }

    void ReplicationServer::sendSnapshots()
    {
        if ( _pHost == nullptr )
            return;
        // 클라이언트 상태는 나누기 전에 모두 잡는다 — `acquireClient` 는 목록을 키울 수 있다(나누는 중에는 아무도 목록을 건드리지 않는다).
        _pHost->collectConnected( _listConnectionScratch );
        _listClientScratch.resize( _listConnectionScratch.size() );
        for ( size_t index = 0; index < _listConnectionScratch.size(); ++index )
            acquireClient( _listConnectionScratch[index] );
        for ( size_t index = 0; index < _listConnectionScratch.size(); ++index )
            _listClientScratch[index] = &_listClient[static_cast<size_t>( _listConnectionScratch[index] )];
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
            sendSnapshot( _pRangeConnection[index], *_ppRangeClient[index], scratch );
    }

    void ReplicationServer::sendSnapshot( int32 connectionId, ClientState& client, SnapshotScratch& scratch )
    {
        // 이 클라이언트에게 관련 있는 것만 — 엔티티 버퍼는 자리에 덮어써 용량을 남긴다.
        NetSnapshot& filtered            = scratch._filtered;
        filtered._tick                   = _world._tick;
        filtered._lastProcessedInputTick = client._lastProcessedInputTick;
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
        // 쌓인 것이 큰 것부터 — 관련 엔티티는 id 순이라 자리를 이분 탐색으로 찾는다.
        vector<uint32>& listOrderEntity = scratch._listOrderEntity;
        prioritizer.collectOrder( listOrderEntity );
        vector<int32>& listOrder = scratch._listOrder;
        listOrder.resize( listOrderEntity.size() );
        for ( size_t index = 0; index < listOrderEntity.size(); ++index )
            listOrder[index] = static_cast<int32>( filtered.findEntity( listOrderEntity[index] ) - filtered._listEntity.data() );

        // 기준 — 클라이언트가 확인한 틱의 우리가 보낸 재구성. 너무 오래돼 덮였으면 기준 없이.
        const NetSnapshot* pBaseline = nullptr;
        if ( client._bHasAck )
        {
            const NetSnapshot& candidate = client._listSent[static_cast<size_t>( client._ackedTick % client._listSent.size() )];
            if ( candidate._tick == client._ackedTick && _world._tick - client._ackedTick < client._listSent.size() )
                pBaseline = &candidate;
        }
        BitWriter& writer = scratch._messageWriter.begin( NetClientServerMessage::kSnapshot );
        // 보낸 재구성은 그 틱의 자리에 바로 쓴다(기준 자리와 겹치면 — 확인이 한 바퀴 늦었다 — 사본을 거친다).
        NetSnapshot& slot = client._listSent[static_cast<size_t>( _world._tick % client._listSent.size() )];
        if ( &slot == pBaseline )
        {
            NetSnapshot written;
            filtered.writeDelta( writer, pBaseline, _settings._snapshotBudgetBytes, written, &listOrder, &scratch._listCurrent );
            slot = std::move( written );
        }
        else
        {
            filtered.writeDelta( writer, pBaseline, _settings._snapshotBudgetBytes, slot, &listOrder, &scratch._listCurrent );
        }
        // 실었거나 받는 쪽이 이미 지금 상태인 것만 0 으로 — 못 실은 것은 쌓인 채로 다음 스냅샷에서 앞선다.
        for ( size_t index = 0; index < filtered._listEntity.size(); ++index )
        {
            if ( scratch._listCurrent[index] != 0 )
                prioritizer.markSent( filtered._listEntity[index]._entityId );
        }
        (void)scratch._messageWriter.send( *_pHost, connectionId, NetChannelType::UnreliableSequenced ); // 여러 스레드가 동시에 — NetHost 가 지킨다
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
        const uint32 latestTick = static_cast<uint32>( reader.readVarUint() );
        const uint32 viewTick   = static_cast<uint32>( reader.readVarUint() );
        const uint32 count      = static_cast<uint32>( reader.readVarUint() );
        if ( reader.hasOverflowed() || count > static_cast<uint32>( NetClientServerMessage::kMaxRedundantInputCount ) )
            return false;
        client._viewTick = static_cast<float32>( viewTick ) / 256.0f;
        // 새 것부터 실려 있다 — 이미 쓴 틱 · 이미 가진 틱은 건너뛴다.
        for ( uint32 index = 0; index < count && index <= latestTick; ++index )
        {
            const uint32 tick = latestTick - index;
            // 쓸 틱인지 먼저 본다 — 같은 입력이 여러 패킷에 겹쳐 실려 오므로 대부분은 버릴 것이고, 버릴 것에는 버퍼를 잡지 않는다.
            // 길이가 상한을 넘으면 자르지 않고 깨짐으로 본다 — 자르면 남은 바이트를 다음 입력의 길이로 읽는다.
            const auto inputIter    = std::lower_bound( client._listInput.begin(), client._listInput.end(), tick,
                                                        []( const InputEntry& entry, uint32 value )
               { return entry._tick < value; } );
            const bool bAlreadyUsed = client._bHasInput && tick <= client._lastProcessedInputTick;
            const bool bAlreadyHave = inputIter != client._listInput.end() && inputIter->_tick == tick;
            if ( bAlreadyUsed || bAlreadyHave )
            {
                if ( reader.skipBlob( NetClientServerMessage::kMaxInputBytes ) == false )
                    return false;
                continue;
            }
            vector<uint8> inputBuffer;
            if ( reader.readBlob( inputBuffer, NetClientServerMessage::kMaxInputBytes ) == false )
                return false;
            client._listInput.insert( inputIter, InputEntry{ std::move( inputBuffer ), tick } );
        }
        while ( static_cast<int32>( client._listInput.size() ) > _settings._inputBufferSize )
            client._listInput.pop_front();
        return true;
    }

    bool ReplicationServer::popInput( int32 connectionId, uint32 tick, vector<uint8>& outInputBuffer, bool& outbExact )
    {
        outbExact = false;
        if ( connectionId < 0 || connectionId >= static_cast<int32>( _listClient.size() ) )
            return false;
        ClientState& client = _listClient[static_cast<size_t>( connectionId )];
        // 지난 틱의 입력은 버린다(늦게 왔다 — 이미 되풀이로 처리했다).
        while ( client._listInput.empty() == false && client._listInput.front()._tick < tick )
        {
            client._lastInput = std::move( client._listInput.front()._buffer );
            client._bHasInput = SW_TRUE;
            client._listInput.pop_front();
        }
        if ( client._listInput.empty() == false && client._listInput.front()._tick == tick )
        {
            client._lastInput = std::move( client._listInput.front()._buffer );
            client._bHasInput = SW_TRUE;
            client._listInput.pop_front();
            outInputBuffer = client._lastInput;
            outbExact      = true;
            return true;
        }
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
