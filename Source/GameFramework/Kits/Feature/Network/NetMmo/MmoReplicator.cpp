#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetMmo/MmoReplicator.h"

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
        struct MmoReplicatorInternal
        {
            static float32 computeFlatDistance( const float3& lhs, const float3& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dz = lhs._z - rhs._z;
                return MathUtil::sqrt( dx * dx + dz * dz );
            }

            /**
             * @brief 엔티티 id 를 공간 해시의 키로 바꿉니다. 격자는 키를 비교 · 해시만 하므로 세대는 늘 1 입니다(세대 0 은 무효 핸들이라 격자가 받지 않는다).
             */
            static SlotHandle makeGridKey( uint32 entityID ) { return SlotHandle::make( entityID, 1u ); }

            static void writeEntity( BitWriter& writer, const MmoEntity& entity, bool bWithType )
            {
                writer.writeVarUint( entity._entityID );
                if ( bWithType )
                    writer.writeVarUint( entity._typeID );
                writer.writeFloat( entity._position._x );
                writer.writeFloat( entity._position._y );
                writer.writeFloat( entity._position._z );
                writer.writeBlob( entity._listState.data(), static_cast<int32>( entity._listState.size() ) );
            }

            [[nodiscard]] static bool readEntity( BitReader& reader, MmoEntity& outEntity, bool bWithType )
            {
                outEntity._entityID = static_cast<uint32>( reader.readVarUint() );
                if ( bWithType )
                    outEntity._typeID = static_cast<uint32>( reader.readVarUint() );
                outEntity._position._x = reader.readFloat();
                outEntity._position._y = reader.readFloat();
                outEntity._position._z = reader.readFloat();
                return reader.readBlob( outEntity._listState, NetMmoMessage::kMaxStateBytes ) && reader.hasOverflowed() == false;
            }

            /** @brief 묶음에서 엔티티 하나가 쓰는 비트입니다 — `writeEntity( …, bWithType )` 와 같다(들어옴은 타입까지, 갱신은 없이). */
            static int32 computeEntityBits( const MmoEntity& entity, bool bWithType )
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
    // MmoReplicator
    // ------------------------------------------------------------------------------
    MmoReplicator::MmoReplicator()
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

    void MmoReplicator::initialize( NetHost* pHost, const MmoReplicatorSettings& settings, const IInterestPolicy* pPolicy )
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

    void MmoReplicator::setEntity( const MmoEntity& entity )
    {
        if ( static_cast<int32>( entity._listState.size() ) > NetMmoMessage::kMaxStateBytes )
        {
            if ( _oversizedEntityCount == 0 )
                SW_LOG_WARNING( "MmoReplicator: entity %# has %# state bytes, more than the limit %# - ignored (further ones are only counted)", entity._entityID,
                                static_cast<int32>( entity._listState.size() ), NetMmoMessage::kMaxStateBytes );
            ++_oversizedEntityCount;
            return;
        }
        _mapEntity[entity._entityID] = entity;
        _grid.update( MmoReplicatorInternal::makeGridKey( entity._entityID ), entity._position._x, entity._position._z, entity._position._x,
                      entity._position._z );
    }

    void MmoReplicator::removeEntity( uint32 entityID )
    {
        _mapEntity.erase( entityID );
        _grid.remove( MmoReplicatorInternal::makeGridKey( entityID ) );
    }

    void MmoReplicator::setObserver( int32 connectionID, uint32 entityID )
    {
        if ( connectionID < 0 )
            return;
        if ( connectionID >= static_cast<int32>( _listObserver.size() ) )
            _listObserver.resize( static_cast<size_t>( connectionID + 1 ) );
        Observer& observer = _listObserver[static_cast<size_t>( connectionID )];
        if ( observer._bActive == SW_FALSE )
        {
            observer = Observer{};
            observer._ackedUpdate.initialize( static_cast<int32>( NetMmoMessage::kMaxUnconfirmedTicks ) );
        }
        observer._entityID = entityID;
        observer._bActive  = SW_TRUE;
    }

    void MmoReplicator::removeObserver( int32 connectionID )
    {
        if ( connectionID >= 0 && connectionID < static_cast<int32>( _listObserver.size() ) )
            _listObserver[static_cast<size_t>( connectionID )] = Observer{};
    }

    NetHandleResult MmoReplicator::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        // 메시지 펌프(게임 스레드)에서 불린다 — `update` 의 나눈 본문과 겹치지 않는다(같은 스레드에서 차례로).
        if ( context._kind != NetMmoMessage::kUpdateAck || context._connectionID < 0 || context._connectionID >= static_cast<int32>( _listObserver.size() ) )
            return NetHandleResult::Handled;
        const uint32 newestTick = static_cast<uint32>( body.readVarUint() );
        const uint32 bits       = body.readUint32();
        if ( body.hasOverflowed() || newestTick > _tick )
            return NetHandleResult::Malformed;
        Observer& observer = _listObserver[static_cast<size_t>( context._connectionID )];
        if ( observer._bActive == SW_FALSE )
            return NetHandleResult::Handled;
        // 순서가 뒤바뀐 확인도 받은 틱은 받은 것이다 — 가장 새 틱은 앞으로만 가고, 그보다 표 크기 이상 옛 틱은 적지 않는다(고리의 새 틱 자리를 덮는다).
        if ( observer._bHasAck == SW_FALSE || newestTick > observer._newestAckTick )
            observer._newestAckTick = newestTick;
        observer._bHasAck       = SW_TRUE;
        const uint32 span       = NetMmoMessage::kMaxUnconfirmedTicks - 1;
        const uint32 oldestTick = observer._newestAckTick >= span ? observer._newestAckTick - span : 0u;
        if ( oldestTick <= newestTick )
            observer._ackedUpdate.acquire( newestTick ) = SW_TRUE;
        for ( uint32 index = 0; index < NetMmoMessage::kAckWindowTicks && index + 1 <= newestTick; ++index )
        {
            const uint32 tick = newestTick - 1 - index;
            if ( ( bits & ( 1u << index ) ) != 0 && oldestTick <= tick )
                observer._ackedUpdate.acquire( tick ) = SW_TRUE;
        }
        return NetHandleResult::Handled;
    }

    void MmoReplicator::onConnectionClosed( int32 connectionID, NetDisconnectReason reason )
    {
        (void)reason;
        removeObserver( connectionID );
    }

    int32 MmoReplicator::getVisibleCount( int32 connectionID ) const
    {
        return connectionID >= 0 && connectionID < static_cast<int32>( _listObserver.size() )
                 ? static_cast<int32>( _listObserver[static_cast<size_t>( connectionID )]._mapVisible.size() )
                 : 0;
    }

    void MmoReplicator::setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold ) { _parallel.setTaskManager( pTaskManager, serialThreshold ); }

    void MmoReplicator::update( float32 deltaTime )
    {
        if ( _pHost == nullptr )
            return;
        _tickDeltaTime         = deltaTime;
        _tickUpdateBudgetBytes = NetSendBudget::computeTickBudget( _settings._updateBudgetBytes, _pHost->getMaxBytesPerSecond(), static_cast<float64>( deltaTime ) );
        ++_tick;
        _observerScratch.prepare( _parallel );
        _pRangeObserver = _listObserver.data();
        _parallel.run( static_cast<uint32>( _listObserver.size() ), SW_DELEGATE_METHOD( ParallelBlockDelegate, &MmoReplicator::updateObserverRange, this ) );
        for ( ObserverScratch& scratch : _observerScratch.getSlots() )
        {
            _sentUpdateCount += scratch._sentUpdateCount;
            scratch._sentUpdateCount = 0;
        }
    }

    void MmoReplicator::updateObserverRange( uint32 start, uint32 end )
    {
        ObserverScratch& scratch = _observerScratch.acquire( _parallel );
        for ( uint32 index = start; index < end; ++index )
        {
            Observer& observer = _pRangeObserver[index];
            if ( observer._bActive )
                updateObserver( static_cast<int32>( index ), observer, _tickDeltaTime, scratch );
        }
    }

    const MmoEntity& MmoReplicator::getEntity( uint32 entityID ) const
    {
        // 나눈 본문에서 부른다 — `operator[]` 는 없으면 넣으므로(쓰기) 쓰지 않는다. 보이는 엔티티는 나감 단계가 사라진 것을 이미 뺐다.
        const auto entityIter = _mapEntity.find( entityID );
        SW_ASSERT( entityIter != _mapEntity.end() );
        return entityIter->second;
    }

    void MmoReplicator::updateObserver( int32 connectionID, Observer& observer, float32 deltaTime, ObserverScratch& scratch )
    {
        // 관찰자의 자리는 엔티티 표에서 — 격자와 표는 setEntity · removeEntity 가 함께 바꾼다.
        const auto observerIter = _mapEntity.find( observer._entityID );
        if ( observerIter == _mapEntity.end() )
            return;
        const float3 center = observerIter->second._position;

        const bool bAlwaysPolicy = _pPolicy->hasAlwaysRelevant();

        // 1) 나감 — 사라졌거나 나가는 반경 밖(늘 보이기는 빼고).
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
            if ( bAlways == false && MmoReplicatorInternal::computeFlatDistance( center, entityIter->second._position ) > _settings._leaveRadius )
                listLeave.push_back( visible.first );
        }
        std::sort( listLeave.begin(), listLeave.end() );
        const size_t pendingLeaveIndex = sendLeaves( connectionID, observer, scratch );

        // 2) 들어옴 — 들어오는 반경 안(가까운 것부터, 틱마다 상한).
        vector<SlotHandle>& listNear = scratch._listNear;
        _grid.queryCircle( center._x, center._z, _settings._enterRadius, listNear );
        if ( bAlwaysPolicy )
        {
            for ( const auto& entity : _mapEntity )
            {
                if ( _pPolicy->isAlwaysRelevant( connectionID, entity.second ) )
                    listNear.push_back( MmoReplicatorInternal::makeGridKey( entity.first ) );
            }
        }
        // 거리는 한 번씩만 재고 (거리, id) 로 정렬한다(비교마다 해시를 찾지 않게). 이미 보이는 것은 뺀다.
        vector<std::pair<float32, uint32>>& listRank = scratch._listRank;
        listRank.clear();
        for ( const SlotHandle nearKey : listNear )
        {
            const uint32 entityID = nearKey.index();
            if ( observer._mapVisible.find( entityID ) != observer._mapVisible.end() )
                continue;
            const auto entityIter = _mapEntity.find( entityID );
            if ( entityIter != _mapEntity.end() )
                listRank.emplace_back( MmoReplicatorInternal::computeFlatDistance( center, entityIter->second._position ), entityID );
        }
        std::sort( listRank.begin(), listRank.end() );
        listRank.erase( std::unique( listRank.begin(), listRank.end() ), listRank.end() ); // 반경 안 + 늘 보이기 겹침
        // 한 신뢰 메시지에 묶는다 — 엔티티마다 창 한 칸 · 머리를 쓰지 않는다(Core 가 64 KB 까지 조각으로 나른다). 보내기가 받아들여야 보이는 목록에 넣는다.
        vector<uint32>& listEnter = scratch._listEnter;
        listEnter.clear();
        {
            BitWriter& writer = scratch._messageWriter.begin( NetMmoMessage::kEnter );
            writer.writeVarUint( _tick );
            NetSendBudget budget( NetConnection::kMaxReliableMessageSize, NetConnection::kMaxReliableMessageSize );
            budget.reserveBits( writer.getBitCount() + 1 ); // 머리 + 끝 표시
            for ( const auto& ranked : listRank )
            {
                if ( static_cast<int32>( listEnter.size() ) >= _settings._maxEnterPerTick )
                    break; // 가까운 것부터 — 나머지는 다음 틱에
                const auto entityIter = _mapEntity.find( ranked.second );
                if ( entityIter == _mapEntity.end() )
                    continue;
                if ( budget.tryReserveBits( 1 + MmoReplicatorInternal::computeEntityBits( entityIter->second, true ) ) == false )
                    break; // 64 KB — 나머지는 다음 틱에
                writer.writeBool( true );
                MmoReplicatorInternal::writeEntity( writer, entityIter->second, true );
                listEnter.push_back( ranked.second );
            }
            writer.writeBool( false );
            if ( listEnter.empty() == false && _pHost->sendMessage( connectionID, NetChannelType::ReliableOrdered, writer.getBytes() ) )
            {
                for ( const uint32 entityID : listEnter )
                {
                    VisibleEntry& entry  = observer._mapVisible[entityID];
                    entry._listSentState = getEntity( entityID )._listState; // 신뢰 — 확인된 것으로 둔다
                    entry._bInFlight     = SW_FALSE;
                }
            }
            // 보내기가 거절했으면(신뢰 창이 찼다) 아무것도 보이는 목록에 넣지 않는다 — 다음 틱에 다시 고른다.
        }

        // 3) 갱신 — 우선도를 쌓고 예산 안에서 큰 것부터. 나감을 아직 못 보낸 것(신뢰 창이 찼다)은 건너뛴다 — 사라진 엔티티일 수 있다.
        resolveInFlightUpdates( observer );
        NetPrioritizer& prioritizer = observer._prioritizer;
        for ( const auto& visible : observer._mapVisible )
        {
            if ( std::binary_search( listLeave.begin() + static_cast<ptrdiff_t>( pendingLeaveIndex ), listLeave.end(), visible.first ) )
                continue;
            const MmoEntity&    entity   = getEntity( visible.first );
            const float32       distance = MmoReplicatorInternal::computeFlatDistance( center, entity._position );
            const VisibleEntry& entry    = visible.second;
            // 오가는 갱신이 있으면 그것과 견준다 — 확인을 기다리는 동안 같은 상태를 "바뀜" 으로 가속하지 않는다. 잃으면 확인된 상태(옛것)와 견주게 된다.
            const vector<uint8>& reference = entry._bInFlight == SW_TRUE ? entry._listInFlightState : entry._listSentState;
            const bool           bChanged  = entity._listState != reference;
            prioritizer.accumulate( visible.first, _pPolicy->computePriority( connectionID, entity, distance ) * ( bChanged ? _settings._changedBoost : 1.0f ), deltaTime );
        }
        vector<uint32>& listOrder = scratch._listOrder;
        prioritizer.collectOrder( listOrder );
        BitWriter& writer = scratch._messageWriter.begin( NetMmoMessage::kUpdate );
        writer.writeVarUint( _tick );
        NetSendBudget budget( _tickUpdateBudgetBytes );
        budget.reserveBits( writer.getBitCount() + 1 ); // 머리 + 끝 표시
        vector<uint32>& listSent = scratch._listSent;
        listSent.clear();
        for ( const uint32 entityID : listOrder )
        {
            if ( std::binary_search( listLeave.begin() + static_cast<ptrdiff_t>( pendingLeaveIndex ), listLeave.end(), entityID ) )
                continue;
            const MmoEntity& entity = getEntity( entityID );
            if ( budget.tryReserveBits( 1 + MmoReplicatorInternal::computeEntityBits( entity, false ) ) == false )
                continue;
            writer.writeBool( true );
            MmoReplicatorInternal::writeEntity( writer, entity, false );
            listSent.push_back( entityID );
        }
        writer.writeBool( false );
        if ( listSent.empty() )
            return;
        (void)_pHost->sendMessage( connectionID, NetChannelType::Unreliable, writer.getBytes() );
        for ( const uint32 entityID : listSent )
        {
            // 확인될 때까지는 보낸 상태를 따로 둔다 — 잃으면 확인된 상태(`_listSentState`)가 옛것이라 "바뀜" 가속이 다시 걸린다.
            prioritizer.markSentUnconfirmed( entityID, _tick );
            VisibleEntry& entry      = observer._mapVisible[entityID];
            entry._listInFlightState = getEntity( entityID )._listState;
            entry._inFlightTick      = _tick;
            entry._bInFlight         = SW_TRUE;
        }
        scratch._sentUpdateCount += listSent.size();
    }

    void MmoReplicator::resolveInFlightUpdates( Observer& observer ) const
    {
        // 엔티티마다 독립 판정이라 맵 순회 순서와 상관없이 결과(우선도 · 상태)가 같다.
        for ( auto& visible : observer._mapVisible )
        {
            VisibleEntry& entry = visible.second;
            if ( entry._bInFlight == SW_FALSE )
                continue;
            const bool bAckedPast = observer._bHasAck == SW_TRUE && observer._newestAckTick >= entry._inFlightTick;
            const bool bExpired   = _tick - entry._inFlightTick >= NetMmoMessage::kMaxUnconfirmedTicks;
            if ( bAckedPast == false && bExpired == false )
                continue; // 아직 오가는 중
            const bool bDelivered = bExpired == false && observer._ackedUpdate.find( entry._inFlightTick ) != nullptr;
            if ( bDelivered )
                entry._listSentState.swap( entry._listInFlightState );
            entry._listInFlightState.clear();
            entry._bInFlight = SW_FALSE;
            observer._prioritizer.resolveSend( visible.first, bDelivered );
        }
    }

    size_t MmoReplicator::sendLeaves( int32 connectionID, Observer& observer, ObserverScratch& scratch )
    {
        // 조각나지 않는 크기(1 KB)로 쪼갠다 — 묶어도 창 몫(조각 수)은 같고, 나눠 두면 창이 찼을 때 앞 묶음이라도 나간다. 보낸 것만 보이는 목록에서 뺀다(유령이 남지 않게).
        const vector<uint32>& listLeave = scratch._listLeave;
        size_t                sentCount = 0;
        while ( sentCount < listLeave.size() )
        {
            BitWriter&    writer = scratch._messageWriter.begin( NetMmoMessage::kLeave );
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
                break; // 신뢰 창이 찼다 — 남은 것은 보이는 채로 두고 다음 틱에
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
    // MmoClientView
    // ------------------------------------------------------------------------------
    void MmoClientView::onConnectionOpened( int32 connectionID )
    {
        (void)connectionID;
        _mapEntity.clear();
        _bHasUpdate   = SW_FALSE;
        _receivedBits = 0;
    }

    void MmoClientView::markUpdateReceived( uint32 tick )
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
            // 앞의 가장 새 틱도 이제 비트 하나다(shift - 1 자리).
            if ( shift > NetMmoMessage::kAckWindowTicks )
                _receivedBits = 0u;
            else if ( shift == NetMmoMessage::kAckWindowTicks )
                _receivedBits = 1u << ( shift - 1u ); // 32 칸 밀기는 정의되지 않는다 — 앞 비트는 모두 창 밖이다
            else
                _receivedBits = ( _receivedBits << shift ) | ( 1u << ( shift - 1u ) );
            _newestUpdateTick = tick;
            return;
        }
        const uint32 age = _newestUpdateTick - tick; // 0 = 같은 틱(중복)
        if ( 1u <= age && age <= NetMmoMessage::kAckWindowTicks )
            _receivedBits |= 1u << ( age - 1u );
    }

    void MmoClientView::sendAck( NetHost& host, int32 connectionID )
    {
        if ( _bHasUpdate == SW_FALSE )
            return;
        BitWriter& writer = _ackWriter.begin( NetMmoMessage::kUpdateAck );
        writer.writeVarUint( _newestUpdateTick );
        writer.writeUint32( _receivedBits );
        (void)_ackWriter.send( host, connectionID, NetChannelType::Unreliable ); // 잃어도 다음 틱 확인이 같은 창을 다시 싣는다
    }

    NetHandleResult MmoClientView::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        BitReader& reader = body;
        if ( context._kind == NetMmoMessage::kEnter )
        {
            const uint32 tick = static_cast<uint32>( reader.readVarUint() );
            while ( reader.readBool() )
            {
                ClientEntity entry;
                entry._tick = tick;
                if ( MmoReplicatorInternal::readEntity( reader, entry._entity, true ) == false )
                    return NetHandleResult::Malformed;
                const uint32 entityID = entry._entity._entityID;
                _mapEntity[entityID]  = std::move( entry );
                _eventBuffer.push( MmoClientEvent{ entityID, MmoClientEvent::Kind::Entered } );
            }
            if ( reader.hasOverflowed() )
                return NetHandleResult::Malformed;
        }
        else if ( context._kind == NetMmoMessage::kLeave )
        {
            const uint64 count = reader.readVarUint();
            for ( uint64 index = 0; index < count && reader.hasOverflowed() == false; ++index )
            {
                const uint32 entityID = static_cast<uint32>( reader.readVarUint() );
                if ( reader.hasOverflowed() == false && _mapEntity.erase( entityID ) > 0 )
                    _eventBuffer.push( MmoClientEvent{ entityID, MmoClientEvent::Kind::Left } );
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
                MmoEntity update;
                if ( MmoReplicatorInternal::readEntity( reader, update, false ) == false )
                    return NetHandleResult::Malformed;
                const auto entityIter = _mapEntity.find( update._entityID );
                if ( entityIter == _mapEntity.end() )
                    continue; // 들어옴보다 먼저 왔거나 이미 나갔다 — 버린다
                if ( tick < entityIter->second._tick )
                {
                    ++_staleUpdateCount; // 순서가 뒤바뀐 옛 갱신 — 더 새 상태를 덮지 않는다
                    continue;
                }
                entityIter->second._tick              = tick;
                entityIter->second._entity._position  = update._position;
                entityIter->second._entity._listState = std::move( update._listState );
                _eventBuffer.push( MmoClientEvent{ update._entityID, MmoClientEvent::Kind::Updated } );
            }
            if ( reader.hasOverflowed() )
                return NetHandleResult::Malformed;
        }
        return NetHandleResult::Handled;
    }

    void MmoClientView::drainEvents( vector<MmoClientEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    const MmoEntity* MmoClientView::findEntity( uint32 entityID ) const
    {
        const auto entityIter = _mapEntity.find( entityID );
        return entityIter != _mapEntity.end() ? &entityIter->second._entity : nullptr;
    }
} // namespace sw
