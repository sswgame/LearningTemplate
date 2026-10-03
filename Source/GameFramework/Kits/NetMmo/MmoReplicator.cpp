#include "pch.h"

#include "GameFramework/Kits/NetMmo/MmoReplicator.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/NetHost.h"
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
                writer.writeVarUint( entity._listState.size() );
                if ( entity._listState.empty() == false )
                    writer.writeBytes( entity._listState.data(), static_cast<int32>( entity._listState.size() ) );
            }

            [[nodiscard]] static bool readEntity( BitReader& reader, MmoEntity& outEntity, bool bWithType )
            {
                outEntity._entityId = static_cast<uint32>( reader.readVarUint() );
                if ( bWithType )
                    outEntity._typeId = static_cast<uint32>( reader.readVarUint() );
                outEntity._position._x = reader.readFloat();
                outEntity._position._y = reader.readFloat();
                outEntity._position._z = reader.readFloat();
                const uint64 size      = reader.readVarUint();
                if ( reader.hasOverflowed() || size > 512 )
                    return false;
                outEntity._listState.resize( static_cast<size_t>( size ) );
                return size == 0 || reader.readBytes( outEntity._listState.data(), static_cast<int32>( size ) );
            }

            static int32 computeEntityBytes( const MmoEntity& entity ) { return 5 + 12 + 2 + static_cast<int32>( entity._listState.size() ); }
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
    }

    void MmoReplicator::setEntity( const MmoEntity& entity )
    {
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

    int32 MmoReplicator::getVisibleCount( int32 connectionId ) const
    {
        return connectionId >= 0 && connectionId < static_cast<int32>( _listObserver.size() )
                 ? static_cast<int32>( _listObserver[static_cast<size_t>( connectionId )]._mapVisible.size() )
                 : 0;
    }

    void MmoReplicator::update( float32 deltaTime )
    {
        for ( size_t index = 0; index < _listObserver.size(); ++index )
        {
            if ( _listObserver[index]._bActive )
                updateObserver( static_cast<int32>( index ), _listObserver[index], deltaTime );
        }
    }

    void MmoReplicator::updateObserver( int32 connectionId, Observer& observer, float32 deltaTime )
    {
        float3 center{};
        if ( _grid.findPosition( observer._entityId, center ) == false )
            return;

        // 1) 나감 — 사라졌거나 나가는 반경 밖(늘 보이기는 빼고).
        vector<uint32> listLeave;
        for ( const auto& visible : observer._mapVisible )
        {
            const auto entityIter = _mapEntity.find( visible.first );
            if ( entityIter == _mapEntity.end() )
            {
                listLeave.push_back( visible.first );
                continue;
            }
            const bool bAlways = _pPolicy->isAlwaysRelevant( connectionId, entityIter->second );
            if ( bAlways == false && MmoReplicatorInternal::computeFlatDistance( center, entityIter->second._position ) > _settings._leaveRadius )
                listLeave.push_back( visible.first );
        }
        std::sort( listLeave.begin(), listLeave.end() );
        if ( listLeave.empty() == false )
        {
            BitWriter writer;
            writer.writeBits( NetMmoMessage::kLeave, 8 );
            writer.writeVarUint( listLeave.size() );
            for ( const uint32 entityId : listLeave )
            {
                writer.writeVarUint( entityId );
                observer._mapVisible.erase( entityId );
            }
            (void)_pHost->sendMessage( connectionId, NetChannelType::ReliableOrdered, writer.getBytes() );
        }

        // 2) 들어옴 — 들어오는 반경 안(가까운 것부터, 틱마다 상한).
        vector<uint32> listNear;
        _grid.queryRadius( center, _settings._enterRadius, listNear );
        for ( const auto& entity : _mapEntity )
        {
            if ( _pPolicy->isAlwaysRelevant( connectionId, entity.second ) )
                listNear.push_back( entity.first );
        }
        std::sort( listNear.begin(), listNear.end(), [&]( uint32 lhs, uint32 rhs )
        {
            const float32 lhsDistance = MmoReplicatorInternal::computeFlatDistance( center, _mapEntity[lhs]._position );
            const float32 rhsDistance = MmoReplicatorInternal::computeFlatDistance( center, _mapEntity[rhs]._position );
            return lhsDistance != rhsDistance ? lhsDistance < rhsDistance : lhs < rhs;
        } );
        int32 enteredCount = 0;
        for ( const uint32 entityId : listNear )
        {
            if ( enteredCount >= _settings._maxEnterPerTick || observer._mapVisible.find( entityId ) != observer._mapVisible.end() )
                continue;
            const auto entityIter = _mapEntity.find( entityId );
            if ( entityIter == _mapEntity.end() )
                continue;
            BitWriter writer;
            writer.writeBits( NetMmoMessage::kEnter, 8 );
            MmoReplicatorInternal::writeEntity( writer, entityIter->second, true );
            if ( _pHost->sendMessage( connectionId, NetChannelType::ReliableOrdered, writer.getBytes() ) == false )
                break; // 신뢰 창이 찼다 — 다음 틱에
            VisibleEntry& entry  = observer._mapVisible[entityId];
            entry._listSentState = entityIter->second._listState;
            entry._accumulated   = 0.0f;
            ++enteredCount;
        }

        // 3) 갱신 — 우선도를 쌓고 예산 안에서 큰 것부터.
        vector<std::pair<float32, uint32>> listCandidate;
        for ( auto& visible : observer._mapVisible )
        {
            const MmoEntity& entity   = _mapEntity[visible.first];
            const float32    distance = MmoReplicatorInternal::computeFlatDistance( center, entity._position );
            const bool       bChanged = entity._listState != visible.second._listSentState;
            visible.second._accumulated += _pPolicy->computePriority( connectionId, entity, distance ) * deltaTime * ( bChanged ? _settings._changedBoost : 1.0f );
            listCandidate.emplace_back( visible.second._accumulated, visible.first );
        }
        std::sort( listCandidate.begin(), listCandidate.end(),
                   []( const std::pair<float32, uint32>& lhs, const std::pair<float32, uint32>& rhs )
        { return lhs.first != rhs.first ? lhs.first > rhs.first : lhs.second < rhs.second; } );
        BitWriter writer;
        writer.writeBits( NetMmoMessage::kUpdate, 8 );
        vector<uint32> listSent;
        int32          usedBytes = 1;
        for ( const auto& candidate : listCandidate )
        {
            const MmoEntity& entity = _mapEntity[candidate.second];
            const int32      bytes  = MmoReplicatorInternal::computeEntityBytes( entity );
            if ( usedBytes + bytes > _settings._updateBudgetBytes )
                continue;
            writer.writeBool( true );
            MmoReplicatorInternal::writeEntity( writer, entity, false );
            usedBytes += bytes;
            listSent.push_back( candidate.second );
        }
        writer.writeBool( false );
        if ( listSent.empty() )
            return;
        (void)_pHost->sendMessage( connectionId, NetChannelType::Unreliable, writer.getBytes() );
        for ( const uint32 entityId : listSent )
        {
            VisibleEntry& entry  = observer._mapVisible[entityId];
            entry._accumulated   = 0.0f;
            entry._listSentState = _mapEntity[entityId]._listState;
        }
        _sentUpdateCount += listSent.size();
    }

    // ------------------------------------------------------------------------------
    // MmoClientView
    // ------------------------------------------------------------------------------
    bool MmoClientView::handleMessage( const vector<uint8>& buffer )
    {
        if ( buffer.empty() || NetMessageRange::isInRange( buffer[0], NetMessageRange::kMmo ) == false )
            return false;
        BitReader reader( buffer.data() + 1, static_cast<int32>( buffer.size() ) - 1 );
        if ( buffer[0] == NetMmoMessage::kEnter )
        {
            MmoEntity entity;
            if ( MmoReplicatorInternal::readEntity( reader, entity, true ) )
            {
                const uint32 entityId = entity._entityId;
                _mapEntity[entityId]  = std::move( entity );
                _listEvent.push_back( MmoClientEvent{ entityId, MmoClientEvent::Kind::Entered } );
            }
        }
        else if ( buffer[0] == NetMmoMessage::kLeave )
        {
            const uint64 count = reader.readVarUint();
            for ( uint64 index = 0; index < count && reader.hasOverflowed() == false; ++index )
            {
                const uint32 entityId = static_cast<uint32>( reader.readVarUint() );
                if ( _mapEntity.erase( entityId ) > 0 )
                    _listEvent.push_back( MmoClientEvent{ entityId, MmoClientEvent::Kind::Left } );
            }
        }
        else if ( buffer[0] == NetMmoMessage::kUpdate )
        {
            while ( reader.readBool() )
            {
                MmoEntity update;
                if ( MmoReplicatorInternal::readEntity( reader, update, false ) == false )
                    break;
                const auto entityIter = _mapEntity.find( update._entityId );
                if ( entityIter == _mapEntity.end() )
                    continue; // 들어옴보다 먼저 왔거나 이미 나갔다 — 버린다
                entityIter->second._position  = update._position;
                entityIter->second._listState = std::move( update._listState );
                _listEvent.push_back( MmoClientEvent{ update._entityId, MmoClientEvent::Kind::Updated } );
            }
        }
        return true;
    }

    void MmoClientView::drainEvents( vector<MmoClientEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    const MmoEntity* MmoClientView::findEntity( uint32 entityId ) const
    {
        const auto entityIter = _mapEntity.find( entityId );
        return entityIter != _mapEntity.end() ? &entityIter->second : nullptr;
    }
} // namespace sw
