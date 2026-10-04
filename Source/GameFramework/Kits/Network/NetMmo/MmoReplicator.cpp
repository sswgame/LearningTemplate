#include "pch.h"

#include "GameFramework/Kits/Network/NetMmo/MmoReplicator.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetSendBudget.h"
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

            static void writeEntity( BitWriter& writer, const MmoEntity& entity, bool bWithType )
            {
                writer.writeVarUint( entity._entityId );
                if ( bWithType )
                    writer.writeVarUint( entity._typeId );
                writer.writeFloat( entity._position._x );
                writer.writeFloat( entity._position._y );
                writer.writeFloat( entity._position._z );
                writer.writeBlob( entity._listState.data(), static_cast<int32>( entity._listState.size() ) );
            }

            [[nodiscard]] static bool readEntity( BitReader& reader, MmoEntity& outEntity, bool bWithType )
            {
                outEntity._entityId = static_cast<uint32>( reader.readVarUint() );
                if ( bWithType )
                    outEntity._typeId = static_cast<uint32>( reader.readVarUint() );
                outEntity._position._x = reader.readFloat();
                outEntity._position._y = reader.readFloat();
                outEntity._position._z = reader.readFloat();
                return reader.readBlob( outEntity._listState, NetMmoMessage::kMaxStateBytes ) && reader.hasOverflowed() == false;
            }

            /** @brief 갱신 묶음에서 엔티티 하나(타입 없음)가 쓰는 비트입니다 — `writeEntity( …, false )` 와 같다. */
            static int32 computeUpdateBits( const MmoEntity& entity )
            {
                return BitMath::computeVarUintBits( entity._entityId ) + 3 * 32 + BitMath::computeBlobBits( static_cast<int32>( entity._listState.size() ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // InterestGrid
    // ------------------------------------------------------------------------------
    void InterestGrid::initialize( float32 cellSize )
    {
        _cellSize = MathUtil::max( 1.0f, cellSize );
        _mapCell.clear();
        _mapPosition.clear();
    }

    int32 InterestGrid::computeCellCoord( float32 value ) const { return static_cast<int32>( MathUtil::floor( value / _cellSize ) ); }

    void InterestGrid::setPosition( uint32 entityId, const float3& position )
    {
        const auto  positionIter = _mapPosition.find( entityId );
        const int64 newKey       = makeCellKey( computeCellCoord( position._x ), computeCellCoord( position._z ) );
        if ( positionIter != _mapPosition.end() )
        {
            const int64 oldKey   = makeCellKey( computeCellCoord( positionIter->second._x ), computeCellCoord( positionIter->second._z ) );
            positionIter->second = position;
            if ( oldKey == newKey )
                return;
            vector<uint32>& listOld = _mapCell[oldKey];
            listOld.erase( std::remove( listOld.begin(), listOld.end(), entityId ), listOld.end() );
        }
        else
        {
            _mapPosition[entityId] = position;
        }
        _mapCell[newKey].push_back( entityId );
    }

    void InterestGrid::remove( uint32 entityId )
    {
        const auto positionIter = _mapPosition.find( entityId );
        if ( positionIter == _mapPosition.end() )
            return;
        vector<uint32>& listCell = _mapCell[makeCellKey( computeCellCoord( positionIter->second._x ), computeCellCoord( positionIter->second._z ) )];
        listCell.erase( std::remove( listCell.begin(), listCell.end(), entityId ), listCell.end() );
        _mapPosition.erase( positionIter );
    }

    bool InterestGrid::findPosition( uint32 entityId, float3& outPosition ) const
    {
        const auto positionIter = _mapPosition.find( entityId );
        if ( positionIter == _mapPosition.end() )
            return false;
        outPosition = positionIter->second;
        return true;
    }

    void InterestGrid::queryRadius( const float3& center, float32 radius, vector<uint32>& outListEntity ) const
    {
        outListEntity.clear();
        const int32 minX = computeCellCoord( center._x - radius );
        const int32 maxX = computeCellCoord( center._x + radius );
        const int32 minZ = computeCellCoord( center._z - radius );
        const int32 maxZ = computeCellCoord( center._z + radius );
        for ( int32 z = minZ; z <= maxZ; ++z )
        {
            for ( int32 x = minX; x <= maxX; ++x )
            {
                const auto cellIter = _mapCell.find( makeCellKey( x, z ) );
                if ( cellIter == _mapCell.end() )
                    continue;
                for ( const uint32 entityId : cellIter->second )
                {
                    const float3& position = _mapPosition.find( entityId )->second;
                    if ( MmoReplicatorInternal::computeFlatDistance( center, position ) <= radius )
                        outListEntity.push_back( entityId );
                }
            }
        }
    }

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
        _grid.initialize( settings._cellSize );
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
                SW_LOG_WARNING( "MmoReplicator: entity %# has %# state bytes, more than the limit %# - ignored (further ones are only counted)", entity._entityId,
                                static_cast<int32>( entity._listState.size() ), NetMmoMessage::kMaxStateBytes );
            ++_oversizedEntityCount;
            return;
        }
        _mapEntity[entity._entityId] = entity;
        _grid.setPosition( entity._entityId, entity._position );
    }

    void MmoReplicator::removeEntity( uint32 entityId )
    {
        _mapEntity.erase( entityId );
        _grid.remove( entityId );
    }

    void MmoReplicator::setObserver( int32 connectionId, uint32 entityId )
    {
        if ( connectionId < 0 )
            return;
        if ( connectionId >= static_cast<int32>( _listObserver.size() ) )
            _listObserver.resize( static_cast<size_t>( connectionId + 1 ) );
        Observer& observer = _listObserver[static_cast<size_t>( connectionId )];
        if ( observer._bActive == SW_FALSE )
            observer = Observer{};
        observer._entityId = entityId;
        observer._bActive  = SW_TRUE;
    }

    void MmoReplicator::removeObserver( int32 connectionId )
    {
        if ( connectionId >= 0 && connectionId < static_cast<int32>( _listObserver.size() ) )
            _listObserver[static_cast<size_t>( connectionId )] = Observer{};
    }

    NetHandleResult MmoReplicator::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        (void)context;
        (void)body;
        return NetHandleResult::Handled; // 마스크가 0 이라 오지 않는다
    }

    void MmoReplicator::onConnectionClosed( int32 connectionId, NetDisconnectReason reason )
    {
        (void)reason;
        removeObserver( connectionId );
    }

    int32 MmoReplicator::getVisibleCount( int32 connectionId ) const
    {
        return connectionId >= 0 && connectionId < static_cast<int32>( _listObserver.size() )
                 ? static_cast<int32>( _listObserver[static_cast<size_t>( connectionId )]._mapVisible.size() )
                 : 0;
    }

    void MmoReplicator::setTaskManager( TaskManager* pTaskManager, uint32 serialThreshold ) { _parallel.setTaskManager( pTaskManager, serialThreshold ); }

    void MmoReplicator::update( float32 deltaTime )
    {
        if ( _pHost == nullptr )
            return;
        _tickDeltaTime = deltaTime;
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

    const MmoEntity& MmoReplicator::getEntity( uint32 entityId ) const
    {
        // 나눈 본문에서 부른다 — `operator[]` 는 없으면 넣으므로(쓰기) 쓰지 않는다. 보이는 엔티티는 나감 단계가 사라진 것을 이미 뺐다.
        const auto entityIter = _mapEntity.find( entityId );
        SW_ASSERT( entityIter != _mapEntity.end() );
        return entityIter->second;
    }

    void MmoReplicator::updateObserver( int32 connectionId, Observer& observer, float32 deltaTime, ObserverScratch& scratch )
    {
        float3 center{};
        if ( _grid.findPosition( observer._entityId, center ) == false )
            return;

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
            const bool bAlways = bAlwaysPolicy && _pPolicy->isAlwaysRelevant( connectionId, entityIter->second );
            if ( bAlways == false && MmoReplicatorInternal::computeFlatDistance( center, entityIter->second._position ) > _settings._leaveRadius )
                listLeave.push_back( visible.first );
        }
        std::sort( listLeave.begin(), listLeave.end() );
        const size_t pendingLeaveIndex = sendLeaves( connectionId, observer, scratch );

        // 2) 들어옴 — 들어오는 반경 안(가까운 것부터, 틱마다 상한).
        vector<uint32>& listNear = scratch._listNear;
        listNear.clear();
        _grid.queryRadius( center, _settings._enterRadius, listNear );
        if ( bAlwaysPolicy )
        {
            for ( const auto& entity : _mapEntity )
            {
                if ( _pPolicy->isAlwaysRelevant( connectionId, entity.second ) )
                    listNear.push_back( entity.first );
            }
        }
        // 거리는 한 번씩만 재고 (거리, id) 로 정렬한다(비교마다 해시를 찾지 않게). 이미 보이는 것은 뺀다.
        vector<std::pair<float32, uint32>>& listRank = scratch._listRank;
        listRank.clear();
        for ( const uint32 entityId : listNear )
        {
            if ( observer._mapVisible.find( entityId ) != observer._mapVisible.end() )
                continue;
            const auto entityIter = _mapEntity.find( entityId );
            if ( entityIter != _mapEntity.end() )
                listRank.emplace_back( MmoReplicatorInternal::computeFlatDistance( center, entityIter->second._position ), entityId );
        }
        std::sort( listRank.begin(), listRank.end() );
        listRank.erase( std::unique( listRank.begin(), listRank.end() ), listRank.end() ); // 반경 안 + 늘 보이기 겹침
        int32 enteredCount = 0;
        for ( const auto& ranked : listRank )
        {
            const uint32 entityId = ranked.second;
            if ( enteredCount >= _settings._maxEnterPerTick )
                break; // 가까운 것부터 — 나머지는 다음 틱에
            const auto entityIter = _mapEntity.find( entityId );
            if ( entityIter == _mapEntity.end() )
                continue;
            BitWriter& writer = scratch._messageWriter.begin( NetMmoMessage::kEnter );
            writer.writeVarUint( _tick );
            MmoReplicatorInternal::writeEntity( writer, entityIter->second, true );
            if ( _pHost->sendMessage( connectionId, NetChannelType::ReliableOrdered, writer.getBytes() ) == false )
                break; // 신뢰 창이 찼다 — 다음 틱에
            VisibleEntry& entry  = observer._mapVisible[entityId];
            entry._listSentState = entityIter->second._listState;
            ++enteredCount;
        }

        // 3) 갱신 — 우선도를 쌓고 예산 안에서 큰 것부터. 나감을 아직 못 보낸 것(신뢰 창이 찼다)은 건너뛴다 — 사라진 엔티티일 수 있다.
        NetPrioritizer& prioritizer = observer._prioritizer;
        for ( const auto& visible : observer._mapVisible )
        {
            if ( std::binary_search( listLeave.begin() + static_cast<ptrdiff_t>( pendingLeaveIndex ), listLeave.end(), visible.first ) )
                continue;
            const MmoEntity& entity   = getEntity( visible.first );
            const float32    distance = MmoReplicatorInternal::computeFlatDistance( center, entity._position );
            const bool       bChanged = entity._listState != visible.second._listSentState;
            prioritizer.accumulate( visible.first, _pPolicy->computePriority( connectionId, entity, distance ) * ( bChanged ? _settings._changedBoost : 1.0f ), deltaTime );
        }
        vector<uint32>& listOrder = scratch._listOrder;
        prioritizer.collectOrder( listOrder );
        BitWriter& writer = scratch._messageWriter.begin( NetMmoMessage::kUpdate );
        writer.writeVarUint( _tick );
        NetSendBudget budget( _settings._updateBudgetBytes );
        budget.reserveBits( writer.getBitCount() + 1 ); // 머리 + 끝 표시
        vector<uint32>& listSent = scratch._listSent;
        listSent.clear();
        for ( const uint32 entityId : listOrder )
        {
            if ( std::binary_search( listLeave.begin() + static_cast<ptrdiff_t>( pendingLeaveIndex ), listLeave.end(), entityId ) )
                continue;
            const MmoEntity& entity = getEntity( entityId );
            if ( budget.tryReserveBits( 1 + MmoReplicatorInternal::computeUpdateBits( entity ) ) == false )
                continue;
            writer.writeBool( true );
            MmoReplicatorInternal::writeEntity( writer, entity, false );
            listSent.push_back( entityId );
        }
        writer.writeBool( false );
        if ( listSent.empty() )
            return;
        (void)_pHost->sendMessage( connectionId, NetChannelType::Unreliable, writer.getBytes() );
        for ( const uint32 entityId : listSent )
        {
            prioritizer.markSent( entityId );
            observer._mapVisible[entityId]._listSentState = getEntity( entityId )._listState;
        }
        scratch._sentUpdateCount += listSent.size();
    }

    size_t MmoReplicator::sendLeaves( int32 connectionId, Observer& observer, ObserverScratch& scratch )
    {
        // 메시지 상한 안에서 쪼갠다 — 한 메시지에 다 넣으면 1024 B 를 넘어 버려지고, 보이는 목록에서는 이미 빠져 클라이언트에 유령이 남는다.
        const vector<uint32>& listLeave = scratch._listLeave;
        size_t                sentCount = 0;
        while ( sentCount < listLeave.size() )
        {
            BitWriter&    writer = scratch._messageWriter.begin( NetMmoMessage::kLeave );
            NetSendBudget budget( NetConnection::kMaxMessageSize );
            budget.reserveBits( writer.getBitCount() + BitMath::computeVarUintBits( listLeave.size() - sentCount ) );
            size_t count = 0;
            while ( sentCount + count < listLeave.size() && budget.tryReserveBits( BitMath::computeVarUintBits( listLeave[sentCount + count] ) ) )
                ++count;
            writer.writeVarUint( count );
            for ( size_t index = 0; index < count; ++index )
                writer.writeVarUint( listLeave[sentCount + index] );
            if ( _pHost->sendMessage( connectionId, NetChannelType::ReliableOrdered, writer.getBytes() ) == false )
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
    void MmoClientView::onConnectionOpened( int32 connectionId )
    {
        (void)connectionId;
        _mapEntity.clear();
    }

    NetHandleResult MmoClientView::handleNetMessage( const NetMessageContext& context, BitReader& body )
    {
        BitReader& reader = body;
        if ( context._kind == NetMmoMessage::kEnter )
        {
            ClientEntity entry;
            entry._tick = static_cast<uint32>( reader.readVarUint() );
            if ( MmoReplicatorInternal::readEntity( reader, entry._entity, true ) == false )
                return NetHandleResult::Malformed;
            const uint32 entityId = entry._entity._entityId;
            _mapEntity[entityId]  = std::move( entry );
            _eventBuffer.push( MmoClientEvent{ entityId, MmoClientEvent::Kind::Entered } );
        }
        else if ( context._kind == NetMmoMessage::kLeave )
        {
            const uint64 count = reader.readVarUint();
            for ( uint64 index = 0; index < count && reader.hasOverflowed() == false; ++index )
            {
                const uint32 entityId = static_cast<uint32>( reader.readVarUint() );
                if ( reader.hasOverflowed() == false && _mapEntity.erase( entityId ) > 0 )
                    _eventBuffer.push( MmoClientEvent{ entityId, MmoClientEvent::Kind::Left } );
            }
            if ( reader.hasOverflowed() )
                return NetHandleResult::Malformed;
        }
        else
        {
            const uint32 tick = static_cast<uint32>( reader.readVarUint() );
            while ( reader.readBool() )
            {
                MmoEntity update;
                if ( MmoReplicatorInternal::readEntity( reader, update, false ) == false )
                    return NetHandleResult::Malformed;
                const auto entityIter = _mapEntity.find( update._entityId );
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
                _eventBuffer.push( MmoClientEvent{ update._entityId, MmoClientEvent::Kind::Updated } );
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

    const MmoEntity* MmoClientView::findEntity( uint32 entityId ) const
    {
        const auto entityIter = _mapEntity.find( entityId );
        return entityIter != _mapEntity.end() ? &entityIter->second._entity : nullptr;
    }
} // namespace sw
