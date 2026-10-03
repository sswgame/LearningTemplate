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

    void ReplicationServer::onDisconnected( int32 connectionId )
    {
        if ( connectionId >= 0 && connectionId < static_cast<int32>( _listClient.size() ) )
            _listClient[static_cast<size_t>( connectionId )] = ClientState{};
    }

    void ReplicationServer::beginTick( uint32 tick )
    {
        _world._tick = tick;
        _world._listEntity.clear();
    }

    void ReplicationServer::setEntity( uint32 entityId, uint32 typeId, const vector<uint8>& buffer )
    {
        NetEntityState entity;
        entity._entityId = entityId;
        entity._typeId   = typeId;
        entity._buffer   = buffer;
        _world._listEntity.push_back( std::move( entity ) );
    }

    void ReplicationServer::endTick() { _world.sortEntities(); }

    void ReplicationServer::sendSnapshots()
    {
        if ( _pHost == nullptr )
            return;
        vector<int32> listConnection;
        _pHost->collectConnected( listConnection );
        for ( const int32 connectionId : listConnection )
        {
            ClientState& client = acquireClient( connectionId );
            // 이 클라이언트에게 관련 있는 것만.
            NetSnapshot filtered;
            filtered._tick                   = _world._tick;
            filtered._lastProcessedInputTick = client._lastProcessedInputTick;
            vector<float32> listPriority;
            for ( const NetEntityState& entity : _world._listEntity )
            {
                if ( _pPolicy->isRelevant( connectionId, entity ) == false )
                    continue;
                filtered._listEntity.push_back( entity );
                listPriority.push_back( _pPolicy->computePriority( connectionId, entity ) );
            }
            vector<int32> listOrder( filtered._listEntity.size() );
            for ( size_t index = 0; index < listOrder.size(); ++index )
                listOrder[index] = static_cast<int32>( index );
            std::stable_sort( listOrder.begin(), listOrder.end(),
                              [&listPriority]( int32 lhs, int32 rhs )
            { return listPriority[static_cast<size_t>( lhs )] > listPriority[static_cast<size_t>( rhs )]; } );

            // 기준 — 클라이언트가 확인한 틱의 우리가 보낸 재구성. 너무 오래돼 덮였으면 기준 없이.
            const NetSnapshot* pBaseline = nullptr;
            if ( client._bHasAck )
            {
                const NetSnapshot& candidate = client._listSent[static_cast<size_t>( client._ackedTick % client._listSent.size() )];
                if ( candidate._tick == client._ackedTick && _world._tick - client._ackedTick < client._listSent.size() )
                    pBaseline = &candidate;
            }
            BitWriter writer;
            writer.writeBits( NetClientServerMessage::kSnapshot, 8 );
            NetSnapshot written;
            filtered.writeDelta( writer, pBaseline, _settings._snapshotBudgetBytes, written, &listOrder );
            client._listSent[static_cast<size_t>( _world._tick % client._listSent.size() )] = std::move( written );
            (void)_pHost->sendMessage( connectionId, NetChannelType::UnreliableSequenced, writer.getBytes() );
        }
    }

    bool ReplicationServer::handleMessage( int32 connectionId, const vector<uint8>& buffer )
    {
        if ( buffer.empty() || NetMessageRange::isInRange( buffer[0], NetMessageRange::kClientServer ) == false || connectionId < 0 )
            return false;
        ClientState& client = acquireClient( connectionId );
        BitReader    reader( buffer.data() + 1, static_cast<int32>( buffer.size() ) - 1 );
        if ( buffer[0] == NetClientServerMessage::kSnapshotAck )
        {
            const uint32 tick = static_cast<uint32>( reader.readVarUint() );
            if ( reader.hasOverflowed() == false && ( client._bHasAck == SW_FALSE || tick > client._ackedTick ) && tick <= _world._tick )
            {
                client._ackedTick = tick;
                client._bHasAck   = SW_TRUE;
            }
        }
        else if ( buffer[0] == NetClientServerMessage::kInput )
        {
            handleInput( client, buffer );
        }
        return true;
    }

    void ReplicationServer::handleInput( ClientState& client, const vector<uint8>& buffer )
    {
        BitReader    reader( buffer.data() + 1, static_cast<int32>( buffer.size() ) - 1 );
        const uint32 latestTick = static_cast<uint32>( reader.readVarUint() );
        const uint32 viewTick   = static_cast<uint32>( reader.readVarUint() );
        const uint32 count      = static_cast<uint32>( reader.readVarUint() );
        if ( reader.hasOverflowed() || count > 32 )
            return;
        client._viewTick = static_cast<float32>( viewTick ) / 256.0f;
        // 새 것부터 실려 있다 — 이미 쓴 틱 · 이미 가진 틱은 건너뛴다.
        for ( uint32 index = 0; index < count && index <= latestTick; ++index )
        {
            const uint32  tick = latestTick - index;
            vector<uint8> inputBuffer( static_cast<size_t>( MathUtil::min<uint64>( 255, reader.readVarUint() ) ) );
            if ( inputBuffer.empty() == false && reader.readBytes( inputBuffer.data(), static_cast<int32>( inputBuffer.size() ) ) == false )
                return;
            if ( client._bHasInput && tick <= client._lastProcessedInputTick )
                continue;
            const auto inputIter = std::lower_bound( client._listInput.begin(), client._listInput.end(), tick,
                                                     []( const InputEntry& entry, uint32 value )
            { return entry._tick < value; } );
            if ( inputIter != client._listInput.end() && inputIter->_tick == tick )
                continue;
            client._listInput.insert( inputIter, InputEntry{ std::move( inputBuffer ), tick } );
        }
        while ( static_cast<int32>( client._listInput.size() ) > _settings._inputBufferSize )
            client._listInput.pop_front();
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

    uint32 ReplicationServer::getAckedTick( int32 connectionId ) const
    {
        return connectionId >= 0 && connectionId < static_cast<int32>( _listClient.size() ) ? _listClient[static_cast<size_t>( connectionId )]._ackedTick : 0;
    }
} // namespace sw
