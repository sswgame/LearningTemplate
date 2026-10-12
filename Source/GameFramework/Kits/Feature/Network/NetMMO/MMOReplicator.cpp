#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetMMO/MMOReplicator.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Network/NetTypes.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct MMOReplicatorInternal
        {
            /**
             * @brief 엔티티 id 를 공간 해시의 키로 바꿉니다.
             * @details 격자는 키를 비교하고 해시하기만 하므로 세대는 항상 1이다. 세대 0은 무효 핸들이라 격자가 받지 않는다.
             */
            static SlotHandle makeGridKey( uint32 entityID ) { return SlotHandle::make( entityID, 1u ); }

            static void writeEntity( BitWriter& writer, const MMOEntity& entity, bool bWithType )
            {
                writer.writeVarUint( entity._entityID );
                if ( bWithType )
                    writer.writeVarUint( entity._typeID );
                writer.writeFloat( entity._position._x );
                writer.writeFloat( entity._position._y );
                writer.writeFloat( entity._position._z );
                writer.writeBlob( entity._listState.data(), static_cast<int32>( entity._listState.size() ) );
            }

            [[nodiscard]] static bool readEntity( BitReader& reader, MMOEntity& outEntity, bool bWithType )
            {
                outEntity._entityID = static_cast<uint32>( reader.readVarUint() );
                if ( bWithType )
                    outEntity._typeID = static_cast<uint32>( reader.readVarUint() );
                outEntity._position._x = reader.readFloat();
                outEntity._position._y = reader.readFloat();
                outEntity._position._z = reader.readFloat();
                return reader.readBlob( outEntity._listState, NetMMOMessage::kMaxStateBytes ) && reader.hasOverflowed() == false;
            }

            /** @brief 메시지에서 엔티티 하나가 쓰는 비트 수입니다. `writeEntity( …, bWithType )` 와 같으며, 새로 보임은 타입까지 쓰고 갱신은 타입 없이 씁니다. */
            static int32 computeEntityBits( const MMOEntity& entity, bool bWithType )
            {
                const int32 typeBits = bWithType ? BitMath::computeVarUintBits( entity._typeID ) : 0;
                return BitMath::computeVarUintBits( entity._entityID ) + typeBits + 3 * 32 + BitMath::computeBlobBits( static_cast<int32>( entity._listState.size() ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // MMOReplicator
    // ------------------------------------------------------------------------------
    MMOReplicator::MMOReplicator()
        : _mapEntity{}
        , _listObserver{}
        , _grid{}
        , _settings{}
        , _defaultPolicy{}
        , _pHost{ nullptr }
        , _pPolicy{ nullptr }
        , _sentUpdateCount{ 0 }
        , _oversizedEntityCount{ 0 }
        , _parallel{}
        , _observerScratch{}
        , _tickDeltaTime{ 0.0f }
        , _tickUpdateBudgetBytes{ 0 }
        , _tick{ 0 }
        , _pRangeObserver{ nullptr }
    {
    }

    void MMOReplicator::initialize( NetHost* pHost, const MMOReplicatorSettings& settings, const IInterestPolicy* pPolicy )
    {
        _pHost                 = pHost;
        _settings              = settings;
        _settings._leaveRadius = MathUtil::max( _settings._leaveRadius, _settings._enterRadius );
        _pPolicy               = pPolicy != nullptr ? pPolicy : &_defaultPolicy;
        _grid                  = SpatialHashGrid2D{ settings._cellSize };
        _mapEntity.clear();
        _listObserver.clear();
        _sentUpdateCount = 0;
        _tick            = 0;
    }

    void MMOReplicator::setEntity( const MMOEntity& entity )
    {
        if ( static_cast<int32>( entity._listState.size() ) > NetMMOMessage::kMaxStateBytes )
        {
            if ( _oversizedEntityCount == 0 )
                SW_LOG_WARNING( "MMOReplicator: entity %# has %# state bytes, more than the limit %# - ignored (further ones are only counted)", entity._entityID,
                                static_cast<int32>( entity._listState.size() ), NetMMOMessage::kMaxStateBytes );
            ++_oversizedEntityCount;
            return;
        }
        _mapEntity[entity._entityID] = entity;
        _grid.update( MMOReplicatorInternal::makeGridKey( entity._entityID ), entity._position._x, entity._position._z, entity._position._x,
                      entity._position._z );
    }

    void MMOReplicator::removeEntity( uint32 entityID )
    {
        _mapEntity.erase( entityID );
        _grid.remove( MMOReplicatorInternal::makeGridKey( entityID ) );
    }

    void MMOReplicator::setObserver( int32 connectionID, uint32 entityID )
    {
        if ( connectionID < 0 )
            return;
        if ( connectionID >= static_cast<int32>( _listObserver.size() ) )
            _listObserver.resize( static_cast<size_t>( connectionID + 1 ) );
        Observer& observer = _listObserver[static_cast<size_t>( connectionID )];
        if ( observer._bActive == SW_FALSE )
        {
            observer = Observer{};
            observer._ackedUpdate.initialize( static_cast<int32>( NetMMOMessage::kMaxUnconfirmedTicks ) );
        }
        observer._entityID = entityID;
        observer._bActive  = SW_TRUE;
    }

    void MMOReplicator::removeObserver( int32 connectionID )
    {
        if ( connectionID >= 0 && connectionID < static_cast<int32>( _listObserver.size() ) )
            _listObserver[static_cast<size_t>( connectionID )] = Observer{};
    }

    NetHandleResult MMOReplicator::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        // 메시지 펌프(게임 스레드)에서 불린다. 같은 스레드에서 차례로 실행되므로 `update` 의 병렬 본문과 겹치지 않는다.
        if ( context._kind != NetMMOMessage::kUpdateAck || context._connectionID < 0 || context._connectionID >= static_cast<int32>( _listObserver.size() ) )
            return NetHandleResult::Handled;
        const uint32 newestTick = static_cast<uint32>( body.readVarUint() );
        const uint32 bits       = body.readUint32();
        if ( body.hasOverflowed() || newestTick > _tick )
            return NetHandleResult::Malformed;
        Observer& observer = _listObserver[static_cast<size_t>( context._connectionID )];
        if ( observer._bActive == SW_FALSE )
            return NetHandleResult::Handled;
        // 순서가 뒤바뀐 확인도 받은 틱은 받은 것으로 센다. 가장 새 틱은 앞으로만 가고, 테이블 크기 이상 오래된 틱은 적지 않는다. 적으면 링 버퍼에서 새 틱의 슬롯을 덮어쓰게 된다.
        if ( observer._bHasAck == SW_FALSE || newestTick > observer._newestAckTick )
            observer._newestAckTick = newestTick;
        observer._bHasAck       = SW_TRUE;
        const uint32 span       = NetMMOMessage::kMaxUnconfirmedTicks - 1;
        const uint32 oldestTick = observer._newestAckTick >= span ? observer._newestAckTick - span : 0u;
        if ( oldestTick <= newestTick )
            observer._ackedUpdate.acquire( newestTick ) = SW_TRUE;
        for ( uint32 index = 0; index < NetMMOMessage::kAckWindowTicks && index + 1 <= newestTick; ++index )
        {
            const uint32 tick = newestTick - 1 - index;
            if ( ( bits & ( 1u << index ) ) != 0 && oldestTick <= tick )
                observer._ackedUpdate.acquire( tick ) = SW_TRUE;
        }
        return NetHandleResult::Handled;
    }

    void MMOReplicator::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        removeObserver( connectionID );
    }

    int32 MMOReplicator::getVisibleCount( int32 connectionID ) const
    {
        return connectionID >= 0 && connectionID < static_cast<int32>( _listObserver.size() )
                 ? static_cast<int32>( _listObserver[static_cast<size_t>( connectionID )]._mapVisible.size() )
                 : 0;
    }

    void MMOReplicator::setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold ) { _parallel.setTaskManager( pTaskManager, serialThreshold ); }

    void MMOReplicator::update( float32 deltaTime )
    {
        if ( _pHost == nullptr )
            return;
        _tickDeltaTime         = deltaTime;
        _tickUpdateBudgetBytes = NetSendBudget::computeTickBudget( _settings._updateBudgetBytes, _pHost->getMaxBytesPerSecond(), static_cast<float64>( deltaTime ) );
        ++_tick;
        _observerScratch.prepare( _parallel );
        _pRangeObserver = _listObserver.data();
        _parallel.run( static_cast<uint32>( _listObserver.size() ), SW_DELEGATE_METHOD( ParallelBlockDelegate, &MMOReplicator::updateObserverRange, this ) );
        for ( ObserverScratch& scratch : _observerScratch.getSlots() )
        {
            _sentUpdateCount += scratch._sentUpdateCount;
            scratch._sentUpdateCount = 0;
        }
    }

    void MMOReplicator::updateObserverRange( uint32 start, uint32 end )
    {
        ObserverScratch& scratch = _observerScratch.acquire( _parallel );
        for ( uint32 index = start; index < end; ++index )
        {
            Observer& observer = _pRangeObserver[index];
            if ( observer._bActive )
                updateObserver( static_cast<int32>( index ), observer, _tickDeltaTime, scratch );
        }
    }

    const MMOEntity& MMOReplicator::getEntity( uint32 entityID ) const
    {
        // 병렬 본문에서 부른다. `operator[]` 는 없으면 항목을 넣으므로(쓰기) 쓰지 않는다. 보이지 않게 된 단계가 사라진 엔티티를 이미 뺐으므로 보이는 엔티티는 항상 존재한다.
        const auto entityIter = _mapEntity.find( entityID );
        SW_ASSERT( entityIter != _mapEntity.end() );
        return entityIter->second;
    }

    void MMOReplicator::updateObserver( int32 connectionID, Observer& observer, float32 deltaTime, ObserverScratch& scratch )
    {
        // 관찰자의 위치는 엔티티 테이블에서 읽는다. 격자와 테이블은 setEntity 와 removeEntity 가 함께 갱신한다.
        const auto observerIter = _mapEntity.find( observer._entityID );
        if ( observerIter == _mapEntity.end() )
            return;
        const float3 center = observerIter->second._position;

        const bool bAlwaysPolicy = _pPolicy->hasAlwaysRelevant();

        // 1) 보이지 않게 됨: 사라졌거나 나가는 반경 밖으로 간 엔티티이다. 항상 보이는 엔티티는 제외한다.
        vector<uint32>& listLeave = scratch._listLeave;
        listLeave.clear();
        for ( const auto& visible : observer._mapVisible )
        {
            const auto entityIter = _mapEntity.find( visible.first );
            if ( entityIter == _mapEntity.end() )
            {
                listLeave.push_back( visible.first );
                continue;
            }
            const bool bAlways = bAlwaysPolicy && _pPolicy->isAlwaysRelevant( connectionID, entityIter->second );
            if ( bAlways == false && float3::getHorizontalDistance( center, entityIter->second._position ) > _settings._leaveRadius )
                listLeave.push_back( visible.first );
        }
        std::sort( listLeave.begin(), listLeave.end() );
        const size_t pendingLeaveIndex = sendLeaves( connectionID, observer, scratch );

        // 2) 새로 보임: 들어오는 반경 안의 엔티티이다. 가까운 것부터 처리하고 틱마다 상한을 둔다.
        vector<SlotHandle>& listNear = scratch._listNear;
        _grid.queryCircle( center._x, center._z, _settings._enterRadius, listNear );
        if ( bAlwaysPolicy )
        {
            for ( const auto& entity : _mapEntity )
            {
                if ( _pPolicy->isAlwaysRelevant( connectionID, entity.second ) )
                    listNear.push_back( MMOReplicatorInternal::makeGridKey( entity.first ) );
            }
        }
        // 거리는 한 번만 계산하고 (거리, id) 로 정렬한다. 비교마다 해시를 찾지 않으려는 것이다. 이미 보이는 엔티티는 뺀다.
        vector<std::pair<float32, uint32>>& listRank = scratch._listRank;
        listRank.clear();
        for ( const SlotHandle nearKey : listNear )
        {
            const uint32 entityID = nearKey.index();
            if ( observer._mapVisible.find( entityID ) != observer._mapVisible.end() )
                continue;
            const auto entityIter = _mapEntity.find( entityID );
            if ( entityIter != _mapEntity.end() )
                listRank.emplace_back( float3::getHorizontalDistance( center, entityIter->second._position ), entityID );
        }
        std::sort( listRank.begin(), listRank.end() );
        listRank.erase( std::unique( listRank.begin(), listRank.end() ), listRank.end() ); // 반경 안의 엔티티와 항상 보이는 엔티티가 겹친 항목을 제거한다
        // 신뢰 메시지 하나에 묶어 보낸다. 엔티티마다 신뢰 창 슬롯과 헤더를 쓰지 않으려는 것이며, Core 가 64 KB까지 조각으로 나눠 전송한다. 전송이 받아들여져야 보이는 목록에 넣는다.
        vector<uint32>& listEnter = scratch._listEnter;
        listEnter.clear();
        {
            BitWriter& writer = scratch._messageWriter.begin( NetMMOMessage::kEnter );
            writer.writeVarUint( _tick );
            NetSendBudget budget( NetConnection::kMaxReliableMessageSize, NetConnection::kMaxReliableMessageSize );
            budget.reserveBits( writer.getBitCount() + 1 ); // 헤더와 끝 표시
            for ( const auto& ranked : listRank )
            {
                if ( static_cast<int32>( listEnter.size() ) >= _settings._maxEnterPerTick )
                    break; // 가까운 것부터 처리했으므로 나머지는 다음 틱에 보낸다
                const auto entityIter = _mapEntity.find( ranked.second );
                if ( entityIter == _mapEntity.end() )
                    continue;
                if ( budget.tryReserveBits( 1 + MMOReplicatorInternal::computeEntityBits( entityIter->second, true ) ) == false )
                    break; // 64 KB에 도달했으므로 나머지는 다음 틱에 보낸다
                writer.writeBool( true );
                MMOReplicatorInternal::writeEntity( writer, entityIter->second, true );
                listEnter.push_back( ranked.second );
            }
            writer.writeBool( false );
            if ( listEnter.empty() == false && _pHost->sendMessage( connectionID, NetChannelType::ReliableOrdered, writer.getBytes() ) )
            {
                for ( const uint32 entityID : listEnter )
                {
                    VisibleEntry& entry  = observer._mapVisible[entityID];
                    entry._listSentState = getEntity( entityID )._listState; // 신뢰 전송이므로 확인된 것으로 둔다
                    entry._bInFlight     = SW_FALSE;
                }
            }
            // 전송이 거절됐으면(신뢰 창이 찼다) 아무것도 보이는 목록에 넣지 않고, 다음 틱에 다시 고른다.
        }

        // 3) 갱신: 우선도를 쌓고 예산 안에서 큰 것부터 보낸다. 보이지 않게 됨을 아직 못 보낸 엔티티는 건너뛴다. 신뢰 창이 찼기 때문이며, 사라진 엔티티일 수 있다.
        resolveInFlightUpdates( observer );
        NetPrioritizer& prioritizer = observer._prioritizer;
        for ( const auto& visible : observer._mapVisible )
        {
            if ( std::binary_search( listLeave.begin() + static_cast<ptrdiff_t>( pendingLeaveIndex ), listLeave.end(), visible.first ) )
                continue;
            const MMOEntity&    entity   = getEntity( visible.first );
            const float32       distance = float3::getHorizontalDistance( center, entity._position );
            const VisibleEntry& entry    = visible.second;
            // 전송 중인 갱신이 있으면 그것과 비교한다. 확인을 기다리는 동안 같은 상태를 "바뀜" 으로 가속하지 않으려는 것이다. 잃으면 확인된 상태(오래된 것)와 비교하게 된다.
            const vector<uint8>& reference = entry._bInFlight == SW_TRUE ? entry._listInFlightState : entry._listSentState;
            const bool           bChanged  = entity._listState != reference;
            prioritizer.accumulate( visible.first, _pPolicy->computePriority( connectionID, entity, distance ) * ( bChanged ? _settings._changedBoost : 1.0f ), deltaTime );
        }
        vector<uint32>& listOrder = scratch._listOrder;
        prioritizer.collectOrder( listOrder );
        BitWriter& writer = scratch._messageWriter.begin( NetMMOMessage::kUpdate );
        writer.writeVarUint( _tick );
        NetSendBudget budget( _tickUpdateBudgetBytes );
        budget.reserveBits( writer.getBitCount() + 1 ); // 헤더와 끝 표시
        vector<uint32>& listSent = scratch._listSent;
        listSent.clear();
        for ( const uint32 entityID : listOrder )
        {
            if ( std::binary_search( listLeave.begin() + static_cast<ptrdiff_t>( pendingLeaveIndex ), listLeave.end(), entityID ) )
                continue;
            const MMOEntity& entity = getEntity( entityID );
            if ( budget.tryReserveBits( 1 + MMOReplicatorInternal::computeEntityBits( entity, false ) ) == false )
                continue;
            writer.writeBool( true );
            MMOReplicatorInternal::writeEntity( writer, entity, false );
            listSent.push_back( entityID );
        }
        writer.writeBool( false );
        if ( listSent.empty() )
            return;
        (void)_pHost->sendMessage( connectionID, NetChannelType::Unreliable, writer.getBytes() );
        for ( const uint32 entityID : listSent )
        {
            // 확인될 때까지 보낸 상태를 따로 보관한다. 잃으면 확인된 상태(`_listSentState`)가 오래된 것이므로 "바뀜" 가중치가 다시 적용된다.
            prioritizer.markSentUnconfirmed( entityID, _tick );
            VisibleEntry& entry      = observer._mapVisible[entityID];
            entry._listInFlightState = getEntity( entityID )._listState;
            entry._inFlightTick      = _tick;
            entry._bInFlight         = SW_TRUE;
        }
        scratch._sentUpdateCount += listSent.size();
    }

    void MMOReplicator::resolveInFlightUpdates( Observer& observer ) const
    {
        // 엔티티마다 독립적으로 판정하므로 맵 순회 순서와 상관없이 결과(우선도와 상태)가 같다.
        for ( auto& visible : observer._mapVisible )
        {
            VisibleEntry& entry = visible.second;
            if ( entry._bInFlight == SW_FALSE )
                continue;
            const bool bAckedPast = observer._bHasAck == SW_TRUE && observer._newestAckTick >= entry._inFlightTick;
            const bool bExpired   = _tick - entry._inFlightTick >= NetMMOMessage::kMaxUnconfirmedTicks;
            if ( bAckedPast == false && bExpired == false )
                continue; // 아직 전송 중이다
            const bool bDelivered = bExpired == false && observer._ackedUpdate.find( entry._inFlightTick ) != nullptr;
            if ( bDelivered )
                entry._listSentState.swap( entry._listInFlightState );
            entry._listInFlightState.clear();
            entry._bInFlight = SW_FALSE;
            observer._prioritizer.resolveSend( visible.first, bDelivered );
        }
    }

    size_t MMOReplicator::sendLeaves( int32 connectionID, Observer& observer, ObserverScratch& scratch )
    {
        // 조각나지 않는 크기(1 KB)로 쪼갠다. 하나로 묶어도 신뢰 창에서 차지하는 슬롯 수는 같고, 나눠 두면 창이 찼을 때도 앞쪽 메시지는 나가기 때문이다.
        // 보낸 엔티티만 보이는 목록에서 뺀다. 그래야 클라이언트에 사라지지 않는 엔티티가 남지 않는다.
        const vector<uint32>& listLeave = scratch._listLeave;
        size_t                sentCount = 0;
        while ( sentCount < listLeave.size() )
        {
            BitWriter&    writer = scratch._messageWriter.begin( NetMMOMessage::kLeave );
            NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
            budget.reserveBits( writer.getBitCount() + BitMath::computeVarUintBits( listLeave.size() - sentCount ) );
            size_t count = 0;
            while ( sentCount + count < listLeave.size() && budget.tryReserveBits( BitMath::computeVarUintBits( listLeave[sentCount + count] ) ) )
            {
                ++count;
            }
            writer.writeVarUint( count );
            for ( size_t index = 0; index < count; ++index )
            {
                writer.writeVarUint( listLeave[sentCount + index] );
            }
            if ( _pHost->sendMessage( connectionID, NetChannelType::ReliableOrdered, writer.getBytes() ) == false )
                break; // 신뢰 창이 찼다. 남은 엔티티는 보이는 채로 두고 다음 틱에 보낸다
            for ( size_t index = 0; index < count; ++index )
            {
                observer._mapVisible.erase( listLeave[sentCount + index] );
                observer._prioritizer.remove( listLeave[sentCount + index] );
            }
            sentCount += count;
        }
        return sentCount;
    }

    // ------------------------------------------------------------------------------
    // MMOClientView
    // ------------------------------------------------------------------------------
    void MMOClientView::onConnectionOpened( int32 connectionID )
    {
        (void)connectionID;
        _mapEntity.clear();
        _bHasUpdate   = SW_FALSE;
        _receivedBits = 0;
    }

    void MMOClientView::markUpdateReceived( uint32 tick )
    {
        if ( _bHasUpdate == SW_FALSE )
        {
            _newestUpdateTick = tick;
            _receivedBits     = 0;
            _bHasUpdate       = SW_TRUE;
            return;
        }
        if ( tick > _newestUpdateTick )
        {
            const uint32 shift = tick - _newestUpdateTick;
            // 이전의 가장 새 틱도 이제 비트 하나로 표현한다(shift - 1 위치).
            if ( shift > NetMMOMessage::kAckWindowTicks )
                _receivedBits = 0u;
            else if ( shift == NetMMOMessage::kAckWindowTicks )
                _receivedBits = 1u << ( shift - 1u ); // 32비트 시프트는 정의되지 않은 동작이다. 앞쪽 비트는 모두 창 밖이므로 이렇게 처리한다
            else
                _receivedBits = ( _receivedBits << shift ) | ( 1u << ( shift - 1u ) );
            _newestUpdateTick = tick;
            return;
        }
        const uint32 age = _newestUpdateTick - tick; // 0이면 같은 틱(중복)이다
        if ( 1u <= age && age <= NetMMOMessage::kAckWindowTicks )
            _receivedBits |= 1u << ( age - 1u );
    }

    void MMOClientView::sendAck( NetHost& host, int32 connectionID )
    {
        if ( _bHasUpdate == SW_FALSE )
            return;
        BitWriter& writer = _ackWriter.begin( NetMMOMessage::kUpdateAck );
        writer.writeVarUint( _newestUpdateTick );
        writer.writeUint32( _receivedBits );
        (void)_ackWriter.send( host, connectionID, NetChannelType::Unreliable ); // 잃어도 다음 틱의 확인이 같은 창을 다시 싣는다
    }

    NetHandleResult MMOClientView::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        BitReader& reader = body;
        if ( context._kind == NetMMOMessage::kEnter )
        {
            const uint32 tick = static_cast<uint32>( reader.readVarUint() );
            while ( reader.readBool() )
            {
                ClientEntity entry;
                entry._tick = tick;
                if ( MMOReplicatorInternal::readEntity( reader, entry._entity, true ) == false )
                    return NetHandleResult::Malformed;
                const uint32 entityID = entry._entity._entityID;
                _mapEntity[entityID]  = std::move( entry );
                _eventBuffer.push( MMOClientEvent{ entityID, MMOClientEvent::Kind::Entered } );
            }
            if ( reader.hasOverflowed() )
                return NetHandleResult::Malformed;
        }
        else if ( context._kind == NetMMOMessage::kLeave )
        {
            const uint64 count = reader.readVarUint();
            for ( uint64 index = 0; index < count && reader.hasOverflowed() == false; ++index )
            {
                const uint32 entityID = static_cast<uint32>( reader.readVarUint() );
                if ( reader.hasOverflowed() == false && _mapEntity.erase( entityID ) > 0 )
                    _eventBuffer.push( MMOClientEvent{ entityID, MMOClientEvent::Kind::Left } );
            }
            if ( reader.hasOverflowed() )
                return NetHandleResult::Malformed;
        }
        else
        {
            const uint32 tick = static_cast<uint32>( reader.readVarUint() );
            if ( reader.hasOverflowed() )
                return NetHandleResult::Malformed;
            markUpdateReceived( tick );
            while ( reader.readBool() )
            {
                MMOEntity update;
                if ( MMOReplicatorInternal::readEntity( reader, update, false ) == false )
                    return NetHandleResult::Malformed;
                const auto entityIter = _mapEntity.find( update._entityID );
                if ( entityIter == _mapEntity.end() )
                    continue; // 새로 보임보다 먼저 왔거나 이미 보이지 않게 된 엔티티이므로 버린다
                if ( tick < entityIter->second._tick )
                {
                    ++_staleUpdateCount; // 순서가 뒤바뀐 오래된 갱신이다. 더 새로운 상태를 덮어쓰지 않게 버린다
                    continue;
                }
                entityIter->second._tick              = tick;
                entityIter->second._entity._position  = update._position;
                entityIter->second._entity._listState = std::move( update._listState );
                _eventBuffer.push( MMOClientEvent{ update._entityID, MMOClientEvent::Kind::Updated } );
            }
            if ( reader.hasOverflowed() )
                return NetHandleResult::Malformed;
        }
        return NetHandleResult::Handled;
    }

    void MMOClientView::drainEvents( vector<MMOClientEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    const MMOEntity* MMOClientView::findEntity( uint32 entityID ) const
    {
        const auto entityIter = _mapEntity.find( entityID );
        return entityIter != _mapEntity.end() ? &entityIter->second._entity : nullptr;
    }
} // namespace sw
