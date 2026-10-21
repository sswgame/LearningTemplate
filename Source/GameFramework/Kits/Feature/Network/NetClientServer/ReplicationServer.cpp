#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Message/NetSendBudget.h"

#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Foundation/Utility/ConfigRangeUtil.h"

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

    bool ReplicationServer::initialize( INetPeers* pHost, const ReplicationServerConfig& settings, const IReplicationPolicy* pPolicy )
    {
        // 실패하면 호스트 없는 서버로 남기고 지난 설정을 그대로 둔다. 값을 범위 안으로 맞춰 돌지 않는다.
        _pHost                  = nullptr;
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR( "ReplicationServer: refused to start - settings '%#'", rangeError.c_str() );
            return false;
        }
        _pHost    = pHost;
        _settings = settings;
        _pPolicy  = pPolicy != nullptr ? pPolicy : &_defaultPolicy;
        _listClient.clear();
        _world            = NetSnapshot{};
        _worldEntityCount = 0;
        return true;
    }

    ReplicationServer::ClientState& ReplicationServer::acquireClient( int32 connectionID )
    {
        if ( connectionID >= static_cast<int32>( _listClient.size() ) )
            _listClient.resize( static_cast<size_t>( connectionID + 1 ) );
        ClientState& client = _listClient[static_cast<size_t>( connectionID )];
        if ( client._bActive == SW_FALSE )
        {
            client          = ClientState{};
            client._bActive = SW_TRUE;
            client._listSent.initialize( _settings._historySize ); // 범위(2 이상 · 1 이상)는 `initialize` 가 봤다
            client._input.initialize( _settings._inputBufferSize, NetClientServerMessage::kInputFormat, NetInputWindowMode::FollowNewest );
        }
        return client;
    }

    void ReplicationServer::resetClient( int32 connectionID )
    {
        if ( connectionID >= 0 && connectionID < static_cast<int32>( _listClient.size() ) )
            _listClient[static_cast<size_t>( connectionID )] = ClientState{};
    }

    void ReplicationServer::onConnectionOpened( int32 connectionID )
    {
        resetClient( connectionID );
    }

    void ReplicationServer::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        resetClient( connectionID );
    }

    uint16 ReplicationServer::getMessageKindMask() const
    {
        return static_cast<uint16>( ( 1u << ( NetClientServerMessage::kSnapshotAck - NetKitMessageRange::kClientServer ) ) |
                                    ( 1u << ( NetClientServerMessage::kInput - NetKitMessageRange::kClientServer ) ) );
    }

    void ReplicationServer::beginTick( uint32 tick )
    {
        _world._tick      = tick;
        _worldEntityCount = 0; // 항목은 지우지 않는다. `setEntity` 가 덮어쓰고 `endTick` 이 남는 항목을 자르므로 버퍼 용량을 재사용한다
    }

    void ReplicationServer::setEntity( uint32 entityID, uint32 typeID, const vector<uint8>& buffer )
    {
        if ( static_cast<int32>( buffer.size() ) > NetSnapshot::kMaxEntityBytes )
        {
            // 월드에는 넣는다. 빼면 클라이언트에서 엔티티가 사라지기 때문이다. 델타가 싣지 않으므로 클라이언트는 마지막으로 받은 상태를 유지한다.
            if ( _oversizedEntityCount == 0 )
                SW_LOG_WARNING( "ReplicationServer: entity %# has %# state bytes, more than the snapshot limit %# - it is not replicated (further ones are only counted)",
                                entityID, static_cast<int32>( buffer.size() ), NetSnapshot::kMaxEntityBytes );
            ++_oversizedEntityCount;
        }
        if ( _worldEntityCount == _world._listEntity.size() )
            _world._listEntity.emplace_back();
        NetEntityState& entity = _world._listEntity[_worldEntityCount++];
        entity._entityID       = entityID;
        entity._typeID         = typeID;
        entity._buffer         = buffer; // 복사 대입으로 항목의 용량을 재사용한다
    }

    void ReplicationServer::endTick()
    {
        _world._listEntity.resize( _worldEntityCount );
        _world.sortEntities();
    }

    void ReplicationServer::setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold )
    {
        _parallel.setTaskManager( pTaskManager, serialThreshold );
    }

    void ReplicationServer::sendSnapshots()
    {
        if ( _pHost == nullptr )
            return;
        // 이번 틱의 예산은 설정값과 연결 상한에서 나온 할당량 중 작은 값이다. 호스트 락은 여기서 한 번만 잡고, 워커는 이 값만 읽는다.
        _snapshotBudgetBytes = NetSendBudget::computeTickBudget( _settings._snapshotBudgetBytes, _pHost->getMaxBytesPerSecond(), static_cast<float64>( _settings._tickInterval ) );
        // 클라이언트 상태는 나눠 실행하기 전에 모두 확보한다. `acquireClient` 는 목록을 키울 수 있기 때문이다. 나눠 실행하는 동안에는 아무도 목록을 건드리지 않는다.
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

    void ReplicationServer::sendSnapshot( int32 connectionID, ClientState& client, SnapshotScratch& scratch )
    {
        // 이 클라이언트에 관련 있는 엔티티만 넣는다. 엔티티 버퍼는 제자리에 덮어써서 용량을 남긴다.
        NetSnapshot& filtered           = scratch._filtered;
        filtered._tick                  = _world._tick;
        filtered._ownerState            = client._ownerState;                  // 복사 대입으로 스크래치 버퍼의 용량을 재사용한다
        filtered._firstMissingInputTick = client._input.getFirstMissingTick(); // 여러 워커가 읽기만 한다. 입력은 전송 전에 게임 스레드가 넣었다
        NetPrioritizer& prioritizer     = client._prioritizer;
        prioritizer.beginAccumulate();
        size_t relevantCount = 0;
        for ( const NetEntityState& entity : std::as_const( _world._listEntity ) ) // 여러 워커가 함께 읽는다
        {
            if ( _pPolicy->isRelevant( connectionID, entity ) == false )
                continue;
            if ( relevantCount < filtered._listEntity.size() )
                filtered._listEntity[relevantCount] = entity;
            else
                filtered._listEntity.push_back( entity );
            ++relevantCount;
            prioritizer.accumulate( entity._entityID, _pPolicy->computePriority( connectionID, entity ), 1.0f ); // 스냅샷 하나에 한 번 누적한다
        }
        filtered._listEntity.resize( relevantCount );
        prioritizer.removeUntouched(); // 더는 관련 없는 엔티티는 잊는다. 다시 관련되면 0부터 쌓는다
        resolveUnconfirmedSends( client, filtered );
        // 쌓인 우선도가 큰 것부터 처리한다. 관련 엔티티는 id 순으로 정렬돼 있으므로 이분 탐색으로 찾는다.
        vector<uint32>& listOrderEntity = scratch._listOrderEntity;
        prioritizer.collectOrder( listOrderEntity );
        vector<int32>& listOrder = scratch._listOrder;
        listOrder.resize( listOrderEntity.size() );
        for ( size_t index = 0; index < listOrderEntity.size(); ++index )
        {
            listOrder[index] = static_cast<int32>( filtered.findEntity( listOrderEntity[index] ) - filtered._listEntity.data() );
        }

        // 기준은 클라이언트가 확인한 틱에 우리가 보낸 복원 스냅샷이다. 너무 오래돼 덮어써졌으면 기준 없이 보낸다.
        const NetSnapshot* pBaseline = nullptr;
        if ( client._bHasAck )
        {
            const NetSnapshot* pCandidate = client._listSent.find( client._ackedTick );
            const bool         bRecent    = _world._tick - client._ackedTick < static_cast<uint32>( client._listSent.getCapacity() );
            if ( pCandidate != nullptr && bRecent )
                pBaseline = pCandidate;
        }
        BitWriter& writer = scratch._messageWriter.begin( NetClientServerMessage::kSnapshot );
        // 보낸 복원 스냅샷은 그 틱의 슬롯에 바로 쓴다. 같은 틱을 두 번 보내서 기준 슬롯과 겹치면 사본을 거친다. 슬롯의 이전 버퍼는 남는다.
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
        // 받는 쪽이 이미 지금 상태인 엔티티는 확정으로 보고 우선도를 0으로 되돌린다. 이번에 실은 엔티티도 확인을 기다리는 전송으로 두고 0으로 되돌린다.
        // 못 실은 엔티티는 우선도가 쌓인 채로 남아 다음 스냅샷에서 앞선다.
        for ( size_t index = 0; index < filtered._listEntity.size(); ++index )
        {
            const uint32 entityID = filtered._listEntity[index]._entityID;
            if ( scratch._listCurrent[index] == NetSnapshot::kEntityAlreadyCurrent )
                prioritizer.markSent( entityID );
            else if ( scratch._listCurrent[index] == NetSnapshot::kEntityWritten )
                prioritizer.markSentUnconfirmed( entityID, _world._tick );
        }
        (void)scratch._messageWriter.send( *_pHost, connectionID, NetChannelType::UnreliableSequenced ); // 여러 스레드가 동시에 호출해도 NetHost 가 보호한다
    }

    void ReplicationServer::resolveUnconfirmedSends( ClientState& client, const NetSnapshot& filtered ) const
    {
        // 여러 워커가 클라이언트마다 따로 부른다. 이 클라이언트의 상태만 쓰고 월드는 읽기만 한다.
        NetPrioritizer&    prioritizer = client._prioritizer;
        const NetSnapshot* pAcked      = client._bHasAck == SW_TRUE ? client._listSent.find( client._ackedTick ) : nullptr;
        const uint32       capacity    = static_cast<uint32>( client._listSent.getCapacity() );
        for ( const NetEntityState& entity : filtered._listEntity )
        {
            uint32 sentTick = 0;
            if ( prioritizer.findUnconfirmedSendTick( entity._entityID, sentTick ) == false )
                continue;
            const bool bAckedPast = client._bHasAck == SW_TRUE && client._ackedTick >= sentTick;
            const bool bExpired   = _world._tick - sentTick >= capacity; // 보낸 복원 스냅샷이 링 버퍼에서 밀려났으므로 더 기다리지 않는다
            if ( bAckedPast == false && bExpired == false )
                continue; // 아직 전송 중이다
            const NetSnapshot*    pSent      = bExpired ? nullptr : client._listSent.find( sentTick );
            const NetEntityState* pSentState = pSent != nullptr ? pSent->findEntity( entity._entityID ) : nullptr;
            const NetEntityState* pHave      = pAcked != nullptr ? pAcked->findEntity( entity._entityID ) : nullptr;
            const bool            bDelivered = pSentState != nullptr && pHave != nullptr && pHave->_typeID == pSentState->_typeID && pHave->_buffer == pSentState->_buffer;
            prioritizer.resolveSend( entity._entityID, bDelivered );
        }
    }

    NetHandleResult ReplicationServer::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        if ( context._connectionID < 0 )
            return NetHandleResult::Malformed;
        ClientState& client = acquireClient( context._connectionID );
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
        // 이미 꺼낸 틱과 이미 가진 틱은 버퍼에 넣지 않고 넘긴다. 길이나 개수가 상한을 넘거나 모자라면 하나도 넣지 않고 손상된 메시지로 본다.
        // 보던 틱은 항목마다의 스탬프로 온다 — 꺼낼 때(`popInput`) 그 입력의 값으로 정한다.
        return client._input.read( reader );
    }

    bool ReplicationServer::popInput( int32 connectionID, uint32 tick, vector<uint8>& outInputBuffer, bool& outbExact )
    {
        outbExact = false;
        if ( connectionID < 0 || connectionID >= static_cast<int32>( _listClient.size() ) )
            return false;
        ClientState& client = _listClient[static_cast<size_t>( connectionID )];
        // 그 틱의 입력이 있으면 그것을 쓴다. 없으면 지난번에 꺼낸 틱 뒤로 늦게 도착한 입력 중 가장 새로운 것을 쓴다. 반복할 값을 새 값으로 바꾸는 것이다.
        const NetInputEntry* pEntry = client._input.findLatestAtOrBefore( tick, client._input.getWindowFirst() );
        if ( pEntry != nullptr )
        {
            client._lastInput = pEntry->_bytes;
            client._bHasInput = SW_TRUE;
            outbExact         = pEntry->_tick == tick;
            // 서버 클램프: 클라이언트가 보던 틱은 지금 꺼내는 틱보다 앞일 수 없고, `_maxViewLagTicks` 보다 오래 되감지 않는다(랙 보정 남용을 막는다).
            const float32 claimedViewTick = static_cast<float32>( pEntry->_stamp ) / static_cast<float32>( NetClientServerMessage::kViewTickStampScale );
            const float32 newestViewTick  = static_cast<float32>( tick );
            const float32 oldestViewTick  = MathUtil::max( 0.0f, newestViewTick - static_cast<float32>( _settings._maxViewLagTicks ) );
            client._viewTick              = MathUtil::clamp( claimedViewTick, oldestViewTick, newestViewTick );
        }
        // 이 틱까지는 다 썼다. 더 받지 않고, 확인 틱이 넘어가므로 클라이언트도 다시 싣지 않는다.
        client._input.setWindow( tick + 1u, NetInputReceiveBuffer::kNoWindowEnd );
        if ( client._bHasInput == SW_FALSE )
            return false;
        outInputBuffer = client._lastInput;
        return true;
    }

    bool ReplicationServer::popIntent( int32 connectionID, uint32 tick, ControlIntent& outIntent, bool& outbExact )
    {
        vector<uint8> listInput;
        if ( popInput( connectionID, tick, listInput, outbExact ) == false )
            return false;
        BitReader     reader( listInput.data(), static_cast<int32>( listInput.size() ) );
        ControlIntent received;
        if ( received.read( reader ) == false )
        {
            SW_LOG_WARNING( "ReplicationServer: connection %# sent a malformed control intent for tick %# (%# byte(s))", connectionID, tick,
                            static_cast<int32>( listInput.size() ) );
            return false;
        }
        // 되풀이한 입력이면 한 번 누름이 두 번 되지 않게 발동 비트를 지운다(`RemoteControllerComponent` 와 같다).
        if ( outbExact == false )
            received._buttonTriggered = 0;
        outIntent = received;
        return true;
    }

    bool ReplicationServer::setOwnerState( int32 connectionID, uint32 inputTick, const vector<uint8>& buffer )
    {
        if ( static_cast<int32>( buffer.size() ) > NetSnapshot::kMaxEntityBytes )
        {
            SW_LOG_ERROR( "ReplicationServer: owner state of connection %# has %# bytes, more than the snapshot limit %# - not set", connectionID,
                          static_cast<int32>( buffer.size() ), NetSnapshot::kMaxEntityBytes );
            return false;
        }
        if ( connectionID < 0 || connectionID >= static_cast<int32>( _listClient.size() ) )
            return false; // 입력을 꺼낸 적 없는 연결이다. 입력 없이 만든 상태는 예측이 맞출 틱이 없다
        NetOwnerState& ownerState = _listClient[static_cast<size_t>( connectionID )]._ownerState;
        ownerState._buffer        = buffer;
        ownerState._inputTick     = inputTick;
        ownerState._bPresent      = SW_TRUE;
        return true;
    }

    float32 ReplicationServer::getClientViewTick( int32 connectionID ) const
    {
        return connectionID >= 0 && connectionID < static_cast<int32>( _listClient.size() ) ? _listClient[static_cast<size_t>( connectionID )]._viewTick : 0.0f;
    }

    uint64 ReplicationServer::getOversizedEntityCount() const
    {
        return _oversizedEntityCount;
    }

    uint32 ReplicationServer::getAckedTick( int32 connectionID ) const
    {
        return connectionID >= 0 && connectionID < static_cast<int32>( _listClient.size() ) ? _listClient[static_cast<size_t>( connectionID )]._ackedTick : 0;
    }
} // namespace sw
