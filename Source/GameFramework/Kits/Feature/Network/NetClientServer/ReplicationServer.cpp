#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/ReplicationServer.h"

#include "Core/Algorithm/CompareUtil.h"
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Character/Hit/MeleeSweepHistory.h"

#include "GameFramework/Runtime/Character/Control/Intent/ControlIntent.h"
#include "GameFramework/Runtime/Foundation/GameData/ConfigRangeUtil.h"

#include <algorithm>

namespace sw
{
    ReplicationServer::ReplicationServer()
        : _listClient{}
        , _world{}
        , _listPendingEntity{}
        , _settings{}
        , _pHost{ nullptr }
        , _pPolicy{ nullptr }
        , _defaultPolicy{}
        , _parallel{}
        , _snapshotScratch{}
        , _listAwakeIndex{}
        , _listAllIndex{}
        , _listWokenScratch{}
        , _listConnectionScratch{}
        , _listClientScratch{}
        , _pendingEntityCount{ 0 }
        , _lastRevision{ 0 }
        , _oversizedEntityCount{ 0 }
        , _pRangeConnection{ nullptr }
        , _ppRangeClient{ nullptr }
    {
    }

    void IReplicationPolicy::collectRelevantEntities( int32 connectionID, const vector<NetEntityState>& listWorldEntity, const vector<uint32>& listCandidate,
                                                      vector<NetRelevantEntity>& outList ) const
    {
        outList.clear();
        for ( const uint32 worldIndex : listCandidate )
        {
            float32 priority{ 0.0f };
            if ( evaluateEntity( connectionID, listWorldEntity[worldIndex], priority ) )
                outList.push_back( NetRelevantEntity{ worldIndex, priority } );
        }
    }

    bool ReplicationServer::initialize( INetPeers* pHost, const ReplicationServerConfig& settings, IReplicationPolicy* pPolicy )
    {
        // 실패하면 호스트 없는 서버로 남기고 지난 설정을 그대로 둔다. 값을 범위 안으로 맞춰 돌지 않는다.
        _pHost                  = nullptr;
        const string rangeError = ConfigRangeUtil::describeRangeErrors( settings );
        if ( rangeError.empty() == false )
        {
            SW_LOG_ERROR_ID( "ReplicationServer.ReplicationServerRefusedStartSettings", "ReplicationServer: refused to start - settings '%#'", rangeError.c_str() );
            return false;
        }
        _pHost    = pHost;
        _settings = settings;
        _pPolicy  = pPolicy != nullptr ? pPolicy : &_defaultPolicy;
        _listClient.clear();
        _world = NetSnapshot{};
        _listPendingEntity.clear();
        _pendingEntityCount = 0;
        _lastRevision       = 0;
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

    void ReplicationServer::onPeerEnteredWorld( int32 connectionID )
    {
        resetClient( connectionID ); // 처음부터 — 그 연결의 앞 월드에서 확인한 기준선을 잇지 않는다
    }

    void ReplicationServer::onPeerLeftWorld( int32 connectionID )
    {
        resetClient( connectionID );
    }

    void ReplicationServer::beginTick( uint32 tick )
    {
        _world._tick        = tick;
        _pendingEntityCount = 0; // 항목은 지우지 않는다. `setEntity` 가 덮어쓰고 `endTick` 이 남는 항목을 자르므로 버퍼 용량을 재사용한다
    }

    void ReplicationServer::setEntity( uint32 entityID, uint32 typeID, const vector<uint8>& buffer )
    {
        if ( static_cast<int32>( buffer.size() ) > NetSnapshot::kMaxEntityBytes )
        {
            // 월드에는 넣는다. 빼면 클라이언트에서 엔티티가 사라지기 때문이다. 델타가 싣지 않으므로 클라이언트는 마지막으로 받은 상태를 유지한다.
            if ( _oversizedEntityCount == 0 )
                SW_LOG_ERROR_ID( "ReplicationServer.ReplicationServerEntityStateBytes", "ReplicationServer: entity %# has %# state bytes, more than the snapshot limit %# - it is not replicated (a state schema defect: shrink or quantize the state; further ones are only counted)",
                                 entityID, static_cast<int32>( buffer.size() ), NetSnapshot::kMaxEntityBytes );
            ++_oversizedEntityCount;
        }
        if ( _pendingEntityCount == _listPendingEntity.size() )
            _listPendingEntity.emplace_back();
        NetEntityState& entity = _listPendingEntity[_pendingEntityCount++];
        entity._entityID       = entityID;
        entity._typeID         = typeID;
        entity._buffer         = buffer; // 복사 대입으로 항목의 용량을 재사용한다
    }

    void ReplicationServer::endTick()
    {
        _listPendingEntity.resize( _pendingEntityCount );
        VectorUtil::sort( _listPendingEntity, Ascending<&NetEntityState::_entityID>{} );
        // 지난 월드와 둘 다 id 순이라 나란히 걷는다. 종류 · 바이트가 같으면 판을 물려받고, 다르거나 새 엔티티면 새 판을 받는다.
        vector<NetEntityState>& listOld  = _world._listEntity; // 다 걸은 뒤 다음 틱의 빈 자리가 된다 — 조각 판 목록을 이어받는다
        size_t                  oldIndex = 0;
        for ( NetEntityState& entity : _listPendingEntity )
        {
            while ( oldIndex < listOld.size() && listOld[oldIndex]._entityID < entity._entityID )
                ++oldIndex;
            NetEntityState* const pOld       = oldIndex < listOld.size() && listOld[oldIndex]._entityID == entity._entityID ? &listOld[oldIndex] : nullptr;
            const bool            bUnchanged = pOld != nullptr && pOld->_typeID == entity._typeID && pOld->_buffer == entity._buffer;
            entity._revision                 = bUnchanged ? pOld->_revision : ++_lastRevision;
            entity._wakeTick                 = bUnchanged ? pOld->_wakeTick : _world._tick; // 판이 오르면 깨어난다 — 깨우는 호출이 따로 없다
            NetSnapshot::updateChunkRevision( entity, pOld );
        }
        _world._listEntity.swap( _listPendingEntity );
    }

    void ReplicationServer::setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold )
    {
        _parallel.setTaskManager( pTaskManager, serialThreshold );
    }

    void ReplicationServer::sendSnapshots()
    {
        if ( _pHost == nullptr )
            return;
        // 클라이언트 상태는 나눠 실행하기 전에 모두 확보한다. `acquireClient` 는 목록을 키울 수 있기 때문이다. 나눠 실행하는 동안에는 아무도 목록을 건드리지 않는다.
        _pHost->collectInWorld( _listConnectionScratch ); // 씬을 읽는 중이거나 다른 씬에 있는 연결에는 보내지 않는다
        _listClientScratch.resize( _listConnectionScratch.size() );
        bool bNeedsAllIndex = false; // 첫 스냅샷을 받는 연결이 있으면 휴면 엔티티까지 후보로 둔다
        for ( size_t index = 0; index < _listConnectionScratch.size(); ++index )
        {
            bNeedsAllIndex = acquireClient( _listConnectionScratch[index] )._bFullyEvaluated == SW_FALSE || bNeedsAllIndex;
        }
        for ( size_t index = 0; index < _listConnectionScratch.size(); ++index )
        {
            const int32  connectionID = _listConnectionScratch[index];
            ClientState& client       = _listClient[static_cast<size_t>( connectionID )];
            // 이번 틱의 예산은 설정값과 그 연결의 지금 송신 속도(`NetSendThrottle` 이 조절한다)에서 나온 할당량 중 작은 값이다. 호스트 락은 나눠 돌기 전에 여기서만 잡는다.
            NetConnectionStats stats;
            const int32        bytesPerSecond = _pHost->getConnectionStats( connectionID, stats ) ? static_cast<int32>( stats._sendBytesPerSecond ) : 0;
            client._snapshotBudgetBytes       = NetSendBudget::computeTickBudget( _settings._snapshotBudgetBytes, bytesPerSecond, static_cast<float64>( _settings._tickInterval ) );
            _listClientScratch[index]         = &client;
        }
        _pPolicy->prepareSnapshots( _world._listEntity ); // 연결이 함께 쓰는 정책 표 — 나눠 돌기 전에 게임 스레드에서 한 번
        prepareCandidates( bNeedsAllIndex );
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
        const NetSnapshot& world = _world; // 여러 워커가 함께 읽는다 — const 로 잡아 쓰기 접근(경합 검사가 쓰기로 센다)을 막는다
        // 기준은 클라이언트가 확인한 틱에 우리가 보낸 기록이다. 너무 오래돼 덮어써졌으면 기준 없이 보낸다.
        const NetSentSnapshot* pBaseline = nullptr;
        if ( client._bHasAck )
        {
            const NetSentSnapshot* pCandidate = client._listSent.find( client._ackedTick );
            const bool             bRecent    = world._tick - client._ackedTick < static_cast<uint32>( client._listSent.getCapacity() );
            if ( pCandidate != nullptr && bRecent )
                pBaseline = pCandidate;
        }

        // 정책에는 후보(깨어 있는 엔티티, 첫 스냅샷이면 전부)만 묻는다. 차례는 월드 차례(id 순)다.
        vector<NetRelevantEntity>& listEvaluated = scratch._listRelevant;
        const vector<uint32>&      listCandidate = client._bFullyEvaluated == SW_TRUE ? _listAwakeIndex : _listAllIndex;
        _pPolicy->collectRelevantEntities( connectionID, world._listEntity, listCandidate, listEvaluated );
        // 지난 스냅샷에서 관련 있던 휴면 엔티티는 묻지 않고 그 판단(우선도)을 이어 간다. 둘 다 id 순이라 월드 쪽 자리는 앞으로만 간다.
        vector<NetRelevantEntity>& listMerged = scratch._listMerged;
        listMerged.clear();
        size_t evaluatedCursor = 0;
        auto   worldIt         = world._listEntity.begin();
        for ( const RelevantRecord& record : client._listLastRelevant )
        {
            worldIt = std::lower_bound( worldIt, world._listEntity.end(), record._entityID, []( const NetEntityState& entity, uint32 id )
            { return entity._entityID < id; } );
            if ( worldIt == world._listEntity.end() )
                break;
            if ( worldIt->_entityID != record._entityID || isEntityDormant( *worldIt ) == false )
                continue; // 사라졌거나, 깨어 있어 후보로 다시 물었다
            const uint32 worldIndex = static_cast<uint32>( worldIt - world._listEntity.begin() );
            while ( evaluatedCursor < listEvaluated.size() && listEvaluated[evaluatedCursor]._worldIndex < worldIndex )
                listMerged.push_back( listEvaluated[evaluatedCursor++] );
            listMerged.push_back( NetRelevantEntity{ worldIndex, record._priority } );
        }
        while ( evaluatedCursor < listEvaluated.size() )
            listMerged.push_back( listEvaluated[evaluatedCursor++] );
        client._bFullyEvaluated = SW_TRUE;

        // 받는 쪽이 이미 지금 판을 가진 휴면 엔티티는 기준에서 그대로 둔다 — 받는 쪽이 가질 목록(사라짐 판정)에는 들지만 우선도 · 델타 후보에서는 빠진다.
        NetPrioritizer& prioritizer = client._prioritizer;
        prioritizer.beginAccumulate();
        vector<uint32>& listRelevantIndex = scratch._listRelevantIndex;
        listRelevantIndex.clear();
        client._listLastRelevant.clear();
        for ( const NetRelevantEntity& relevant : listMerged )
        {
            const NetEntityState& entity = world._listEntity[relevant._worldIndex];
            listRelevantIndex.push_back( relevant._worldIndex );
            client._listLastRelevant.push_back( RelevantRecord{ entity._entityID, relevant._priority } );
            const NetSentEntity* pHave           = pBaseline != nullptr ? pBaseline->findEntity( entity._entityID ) : nullptr;
            const bool           bAlreadyCurrent = pHave != nullptr && pHave->_revision == entity._revision && isEntityDormant( entity );
            if ( bAlreadyCurrent == false )
                prioritizer.accumulate( entity._entityID, relevant._priority, 1.0f ); // 스냅샷 하나에 한 번 누적한다
        }
        prioritizer.removeUntouched(); // 더는 관련 없거나 받는 쪽에 자리 잡은 휴면 엔티티는 잊는다. 다시 후보가 되면 0부터 쌓는다
        resolveUnconfirmedSends( client, listRelevantIndex );
        // 쌓인 우선도가 큰 것부터 처리한다. 관련 엔티티는 id 순이므로 이분 탐색으로 찾는다.
        vector<uint32>& listOrderEntity = scratch._listOrderEntity;
        prioritizer.collectOrder( listOrderEntity );
        vector<int32>& listOrder = scratch._listOrder;
        listOrder.resize( listOrderEntity.size() );
        for ( size_t index = 0; index < listOrderEntity.size(); ++index )
        {
            const auto relevantIt = std::lower_bound( listRelevantIndex.begin(), listRelevantIndex.end(), listOrderEntity[index], [&world]( uint32 worldIndex, uint32 entityID )
            { return world._listEntity[worldIndex]._entityID < entityID; } );
            listOrder[index]      = static_cast<int32>( relevantIt - listRelevantIndex.begin() );
        }

        // 입력(빈틈없이 받은 다음 틱)은 전송 전에 게임 스레드가 넣었다 — 여러 워커가 읽기만 한다.
        const NetDeltaSource source{ world._listEntity, listRelevantIndex, listOrder, client._ownerState, world._tick, client._input.getFirstMissingTick() };
        BitWriter&           writer = scratch._messageWriter.beginMessage<NetClientServerMessage::Table, NetClientServerMessage::Snapshot>();
        // 보낸 기록은 그 틱의 슬롯에 바로 쓴다. 같은 틱을 두 번 보내서 기준 슬롯과 겹치면 사본을 거친다. 슬롯의 이전 버퍼는 남는다.
        NetSentSnapshot& slot = client._listSent.acquire( world._tick );
        if ( &slot == pBaseline )
        {
            NetSentSnapshot written;
            NetSnapshot::writeDelta( writer, source, pBaseline, client._snapshotBudgetBytes, written, scratch._listCurrent );
            slot = std::move( written );
        }
        else
        {
            NetSnapshot::writeDelta( writer, source, pBaseline, client._snapshotBudgetBytes, slot, scratch._listCurrent );
        }
        // 받는 쪽이 이미 지금 상태인 엔티티는 확정으로 보고 우선도를 0으로 되돌린다. 이번에 실은 엔티티도 확인을 기다리는 전송으로 두고 0으로 되돌린다.
        // 못 실은 엔티티는 우선도가 쌓인 채로 남아 다음 스냅샷에서 앞선다.
        for ( size_t index = 0; index < listRelevantIndex.size(); ++index )
        {
            const uint32 entityID = world._listEntity[listRelevantIndex[index]]._entityID;
            if ( scratch._listCurrent[index] == NetSnapshot::kEntityAlreadyCurrent )
                prioritizer.markSent( entityID );
            else if ( scratch._listCurrent[index] == NetSnapshot::kEntityWritten )
                prioritizer.markSentUnconfirmed( entityID, world._tick );
        }
        (void)scratch._messageWriter.sendMessage( *_pHost, connectionID ); // 여러 스레드가 동시에 호출해도 NetEndpoint 가 보호한다
    }

    void ReplicationServer::resolveUnconfirmedSends( ClientState& client, const vector<uint32>& listRelevantIndex ) const
    {
        // 여러 워커가 클라이언트마다 따로 부른다. 이 클라이언트의 상태만 쓰고 월드는 읽기만 한다.
        NetPrioritizer&        prioritizer = client._prioritizer;
        const NetSentSnapshot* pAcked      = client._bHasAck == SW_TRUE ? client._listSent.find( client._ackedTick ) : nullptr;
        const uint32           capacity    = static_cast<uint32>( client._listSent.getCapacity() );
        for ( const uint32 worldIndex : listRelevantIndex )
        {
            const uint32 entityID = _world._listEntity[worldIndex]._entityID;
            uint32       sentTick = 0;
            if ( prioritizer.findUnconfirmedSendTick( entityID, sentTick ) == false )
                continue;
            const bool bAckedPast = client._bHasAck == SW_TRUE && client._ackedTick >= sentTick;
            const bool bExpired   = _world._tick - sentTick >= capacity; // 보낸 기록이 링 버퍼에서 밀려났으므로 더 기다리지 않는다
            if ( bAckedPast == false && bExpired == false )
                continue; // 아직 전송 중이다
            // 확인된 기록이 그때 보낸 판을 들고 있으면 닿은 것이다 — 바이트가 아니라 판으로 비교한다.
            const NetSentSnapshot* pSent      = bExpired ? nullptr : client._listSent.find( sentTick );
            const NetSentEntity*   pSentState = pSent != nullptr ? pSent->findEntity( entityID ) : nullptr;
            const NetSentEntity*   pHave      = pAcked != nullptr ? pAcked->findEntity( entityID ) : nullptr;
            const bool             bDelivered = pSentState != nullptr && pHave != nullptr && pHave->_revision == pSentState->_revision;
            prioritizer.resolveSend( entityID, bDelivered );
        }
    }

    NetHandleResult ReplicationServer::receive( NetClientServerMessage::SnapshotAck, const NetMessageContext& context, const NetSnapshotAckBody& body )
    {
        ClientState& client = acquireClient( context._connectionID );
        const uint32 tick   = body._tick;
        if ( ( client._bHasAck == SW_FALSE || tick > client._ackedTick ) && tick <= _world._tick )
        {
            client._ackedTick = tick;
            client._bHasAck   = SW_TRUE;
        }
        return NetHandleResult::Handled;
    }

    NetHandleResult ReplicationServer::receive( NetClientServerMessage::Input, const NetMessageContext& context, BitReader& body )
    {
        ClientState& client = acquireClient( context._connectionID );
        return handleInput( client, body ) ? NetHandleResult::Handled : NetHandleResult::Malformed;
    }

    bool ReplicationServer::handleInput( ClientState& client, BitReader& reader )
    {
        // 이미 꺼낸 틱과 이미 가진 틱은 버퍼에 넣지 않고 넘긴다. 길이나 개수가 상한을 넘거나 모자라면 하나도 넣지 않고 손상된 메시지로 본다.
        // 보던 틱은 항목마다의 스탬프로 온다 — 꺼낼 때(`popInput`) 그 입력의 값으로 정한다.
        return client._input.read( reader ); // 번들은 메시지 끝이다 — 남는 바이트가 있으면 하나도 넣지 않는다
    }

    optional<uint32> ReplicationServer::findOldestInputTick( int32 connectionID ) const
    {
        if ( connectionID < 0 || connectionID >= static_cast<int32>( _listClient.size() ) )
            return nullopt;
        return _listClient[static_cast<size_t>( connectionID )]._input.findOldestTick();
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
        }
        // 아직 받은 입력이 없으면 쓴 틱도 없다 — 여기서 창을 밀면 호출자가 같은 틱을 다시 물을 때 첫 입력을 영영 꺼내지 못한다.
        if ( client._bHasInput == SW_FALSE )
            return false;
        // 이 틱까지는 다 썼다. 더 받지 않고, 확인 틱이 넘어가므로 클라이언트도 다시 싣지 않는다.
        client._input.setWindow( tick + 1u, NetInputReceiveBuffer::kNoWindowEnd );
        outInputBuffer = client._lastInput;
        return true;
    }

    bool ReplicationServer::popIntent( int32 connectionID, uint32 tick, ControlIntent& outIntent, bool& outbExact, vector<MeleeHitClaim>& outListClaim, uint32* pOutPawnEntityID )
    {
        outListClaim.clear();
        vector<uint8> listInput;
        if ( popInput( connectionID, tick, listInput, outbExact ) == false )
            return false;
        BitReader     reader( listInput.data(), static_cast<int32>( listInput.size() ) );
        ControlIntent received;
        if ( received.read( reader ) == false )
        {
            SW_LOG_WARNING_ID( "ReplicationServer.ReplicationServerConnectionSentMalformed", "ReplicationServer: connection %# sent a malformed control intent for tick %# (%# byte(s))", connectionID, tick,
                               static_cast<int32>( listInput.size() ) );
            return false;
        }
        const uint32          claimCount = reader.readVarUint32();
        bool                  bClaimsOk  = reader.hasOverflowed() == false && claimCount <= static_cast<uint32>( NetClientServerMessage::kMaxClaimsPerIntent );
        vector<MeleeHitClaim> listClaim;
        for ( uint32 claimIndex = 0; bClaimsOk && claimIndex < claimCount; ++claimIndex )
        {
            MeleeHitClaim claim;
            bClaimsOk = MeleeHitClaim::read( reader, claim );
            if ( bClaimsOk )
                listClaim.push_back( claim );
        }
        const uint32 pawnEntityID = bClaimsOk ? reader.readVarUint32() : 0u;
        bClaimsOk                 = bClaimsOk && reader.hasOverflowed() == false;
        if ( bClaimsOk == false )
        {
            SW_LOG_WARNING_ID( "ReplicationServer.ReplicationServerConnectionSentMalformedClaims", "ReplicationServer: connection %# sent malformed melee claims for tick %# (%# claim(s))", connectionID, tick, claimCount );
            return false;
        }
        // 되풀이한 입력이면 한 번 누름 · 한 번 맞음이 두 번 되지 않게 발동 칸과 주장을 지운다.
        if ( outbExact == false )
        {
            received._listTrigger.clear();
            listClaim.clear();
        }
        // 서버 클램프: 보던 시각은 서버의 지금 틱보다 앞일 수 없고, `_maxViewLagTicks` 보다 오래 되감지 않으며, 이 연결이 앞서 본 시각보다 뒤로 가지 않는다(랙 보정 남용을 막는다 — Valve `sv_maxunlag`).
        // 기준은 꺼내는 입력 틱이 아니라 서버 틱이다 — 두 시계는 다르다. 입력 틱으로 자르면 늦게 붙은 클라이언트의 보던 시각이 틱 0 근처로 잘린다.
        ClientState&  client      = _listClient[static_cast<size_t>( connectionID )];
        const float64 presentTick = static_cast<float64>( _world._tick );
        const float64 rewindLimit = MathUtil::max( 0.0, presentTick - static_cast<float64>( _settings._maxViewLagTicks ) );
        received.clampViewTicks( MathUtil::min( presentTick, MathUtil::max( rewindLimit, client._viewTick ) ), presentTick );
        client._viewTick = received._viewTick.getServerTick( presentTick );
        outIntent        = received;
        outListClaim     = std::move( listClaim );
        if ( pOutPawnEntityID != nullptr )
            *pOutPawnEntityID = pawnEntityID;
        return true;
    }

    void ReplicationServer::accumulateInputBudget( float32 elapsedSeconds )
    {
        if ( elapsedSeconds <= 0.0f )
            return;
        const float32 gained  = elapsedSeconds / _settings._tickInterval * ( 1.0f + _settings._inputRateTolerance );
        const float32 ceiling = static_cast<float32>( _settings._maxInputCatchUpTicks );
        for ( ClientState& client : _listClient )
        {
            if ( client._bActive == SW_TRUE )
                client._inputBudget = MathUtil::min( ceiling, client._inputBudget + gained );
        }
    }

    optional<uint32> ReplicationServer::popNextIntent( int32 connectionID, ControlIntent& outIntent, uint32& outPawnEntityID, vector<MeleeHitClaim>& outListClaim )
    {
        const optional<uint32> oldestTick = findOldestInputTick( connectionID );
        if ( oldestTick.has_value() == false )
            return nullopt;
        ClientState& client = _listClient[static_cast<size_t>( connectionID )];
        if ( client._inputBudget < 1.0f )
            return nullopt;
        client._inputBudget -= 1.0f;
        bool bExact = false;
        if ( popIntent( connectionID, *oldestTick, outIntent, bExact, outListClaim, &outPawnEntityID ) == false )
            return nullopt; // 깨진 입력 — 그 틱은 지나갔다(`popInput` 이 창을 밀었다)
        return oldestTick;
    }

    bool ReplicationServer::setOwnerState( int32 connectionID, uint32 inputTick, const vector<uint8>& buffer )
    {
        if ( static_cast<int32>( buffer.size() ) > NetSnapshot::kMaxEntityBytes )
        {
            SW_LOG_ERROR_ID( "ReplicationServer.ReplicationServerOwnerStateConnection", "ReplicationServer: owner state of connection %# has %# bytes, more than the snapshot limit %# - not set", connectionID,
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

    uint64 ReplicationServer::getOversizedEntityCount() const
    {
        return _oversizedEntityCount;
    }

    bool ReplicationServer::isEntityDormant( const NetEntityState& entity ) const
    {
        const uint32 tick = _world._tick;
        return tick >= entity._wakeTick && tick - entity._wakeTick >= static_cast<uint32>( _settings._dormancyTicks );
    }

    void ReplicationServer::prepareCandidates( bool bNeedsAllIndex )
    {
        // 정책이 관련 여부가 바뀌었다고 알린 엔티티를 깨운다(관찰자가 다가가 휴면 가구가 보이게 되는 등). 월드는 id 순이다.
        vector<NetEntityState>& listWorld = _world._listEntity;
        _listWokenScratch.clear();
        _pPolicy->collectWokenEntities( _listWokenScratch );
        for ( const uint32 entityID : _listWokenScratch )
        {
            const auto entityIt = std::lower_bound( listWorld.begin(), listWorld.end(), entityID, []( const NetEntityState& entity, uint32 id )
            { return entity._entityID < id; } );
            if ( entityIt != listWorld.end() && entityIt->_entityID == entityID )
                entityIt->_wakeTick = _world._tick;
        }
        // 연결마다 묻는 후보는 깨어 있는 엔티티다 — 연결당 평가가 월드 크기가 아니라 깨어 있는 수에 비례한다.
        _listAwakeIndex.clear();
        _listAllIndex.clear();
        for ( size_t worldIndex = 0; worldIndex < listWorld.size(); ++worldIndex )
        {
            if ( isEntityDormant( listWorld[worldIndex] ) == false )
                _listAwakeIndex.push_back( static_cast<uint32>( worldIndex ) );
            if ( bNeedsAllIndex )
                _listAllIndex.push_back( static_cast<uint32>( worldIndex ) );
        }
    }

    uint32 ReplicationServer::getAckedTick( int32 connectionID ) const
    {
        return connectionID >= 0 && connectionID < static_cast<int32>( _listClient.size() ) ? _listClient[static_cast<size_t>( connectionID )]._ackedTick : 0;
    }
} // namespace sw
