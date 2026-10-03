#include "pch.h"

#include "GameFramework/Kits/NetClientServer/NetSnapshot.h"

#include "Core/Network/BitStream.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct NetSnapshotInternal
        {
            static constexpr int32 kMaxEntityBytes = 255;
        };
    } // namespace

    const NetEntityState* NetSnapshot::findEntity( uint32 entityId ) const
    {
        const auto entityIter = std::lower_bound( _listEntity.begin(), _listEntity.end(), entityId,
                                                  []( const NetEntityState& entity, uint32 id )
        { return entity._entityId < id; } );
        return entityIter != _listEntity.end() && entityIter->_entityId == entityId ? &*entityIter : nullptr;
    }

    void NetSnapshot::sortEntities()
    {
        std::sort( _listEntity.begin(), _listEntity.end(), []( const NetEntityState& lhs, const NetEntityState& rhs )
        { return lhs._entityId < rhs._entityId; } );
    }

    void NetSnapshot::writeDelta( BitWriter& writer, const NetSnapshot* pBaseline, int32 maxBytes, NetSnapshot& outWritten, const vector<int32>* pListOrder ) const
    {
        outWritten                         = NetSnapshot{};
        outWritten._tick                   = _tick;
        outWritten._lastProcessedInputTick = _lastProcessedInputTick;
        writer.writeVarUint( _tick );
        writer.writeVarUint( pBaseline != nullptr ? pBaseline->_tick + 1u : 0u ); // 0 = 기준 없음
        writer.writeVarUint( _lastProcessedInputTick );

        // 사라진 것 — 기준에는 있고 지금은 없다.
        vector<uint32> listRemoved;
        if ( pBaseline != nullptr )
        {
            for ( const NetEntityState& old : pBaseline->_listEntity )
            {
                if ( findEntity( old._entityId ) == nullptr )
                    listRemoved.push_back( old._entityId );
            }
        }
        writer.writeVarUint( listRemoved.size() );
        for ( const uint32 entityId : listRemoved )
            writer.writeVarUint( entityId );

        // 바뀐 것 — 넘치면 멈추고 실은 것만 재구성에 반영한다(나머지는 기준 값으로 남는다).
        if ( pBaseline != nullptr )
        {
            for ( const NetEntityState& old : pBaseline->_listEntity )
            {
                if ( findEntity( old._entityId ) != nullptr )
                    outWritten._listEntity.push_back( old );
            }
        }
        const size_t entityCount = pListOrder != nullptr ? pListOrder->size() : _listEntity.size();
        for ( size_t orderIndex = 0; orderIndex < entityCount; ++orderIndex )
        {
            const NetEntityState& entity = _listEntity[pListOrder != nullptr ? static_cast<size_t>( ( *pListOrder )[orderIndex] ) : orderIndex];
            const NetEntityState* pOld   = pBaseline != nullptr ? pBaseline->findEntity( entity._entityId ) : nullptr;
            if ( pOld != nullptr && pOld->_typeId == entity._typeId && pOld->_buffer == entity._buffer )
                continue;
            const int32 entityBits = 8 * ( 3 + 2 + 1 + static_cast<int32>( entity._buffer.size() ) ) + 1;
            if ( writer.getBitCount() + entityBits + 1 > maxBytes * 8 )
                continue; // 작은 다음 것은 들어갈 수 있다
            writer.writeBool( true );
            writer.writeVarUint( entity._entityId );
            writer.writeVarUint( entity._typeId );
            const int32 size = static_cast<int32>( entity._buffer.size() > static_cast<size_t>( NetSnapshotInternal::kMaxEntityBytes ) ? NetSnapshotInternal::kMaxEntityBytes : entity._buffer.size() );
            writer.writeBits( static_cast<uint32>( size ), 8 );
            writer.writeBytes( entity._buffer.data(), size );
            bool bReplaced = false;
            for ( NetEntityState& written : outWritten._listEntity )
            {
                if ( written._entityId == entity._entityId )
                {
                    written   = entity;
                    bReplaced = true;
                }
            }
            if ( bReplaced == false )
                outWritten._listEntity.push_back( entity );
        }
        writer.writeBool( false );
        outWritten.sortEntities();
    }

    bool NetSnapshot::readDelta( BitReader& reader, const NetSnapshot* pBaseline, NetSnapshot& outSnapshot )
    {
        outSnapshot                         = NetSnapshot{};
        outSnapshot._tick                   = static_cast<uint32>( reader.readVarUint() );
        const uint32 baselineCode           = static_cast<uint32>( reader.readVarUint() );
        outSnapshot._lastProcessedInputTick = static_cast<uint32>( reader.readVarUint() );
        if ( baselineCode != 0 && ( pBaseline == nullptr || pBaseline->_tick != baselineCode - 1u ) )
            return false; // 기준이 없다 — 서버가 곧 다른 기준으로 보낸다
        if ( baselineCode != 0 )
            outSnapshot._listEntity = pBaseline->_listEntity;
        const uint64 removedCount = reader.readVarUint();
        if ( reader.hasOverflowed() || removedCount > 65535 )
            return false;
        for ( uint64 index = 0; index < removedCount; ++index )
        {
            const uint32 entityId = static_cast<uint32>( reader.readVarUint() );
            outSnapshot._listEntity.erase( std::remove_if( outSnapshot._listEntity.begin(), outSnapshot._listEntity.end(),
                                                           [entityId]( const NetEntityState& entity )
            { return entity._entityId == entityId; } ),
                                           outSnapshot._listEntity.end() );
        }
        while ( reader.readBool() )
        {
            NetEntityState entity;
            entity._entityId = static_cast<uint32>( reader.readVarUint() );
            entity._typeId   = static_cast<uint32>( reader.readVarUint() );
            const int32 size = static_cast<int32>( reader.readBits( 8 ) );
            entity._buffer.resize( static_cast<size_t>( size ) );
            if ( size > 0 && reader.readBytes( entity._buffer.data(), size ) == false )
                return false;
            bool bReplaced = false;
            for ( NetEntityState& existing : outSnapshot._listEntity )
            {
                if ( existing._entityId == entity._entityId )
                {
                    existing  = entity;
                    bReplaced = true;
                }
            }
            if ( bReplaced == false )
                outSnapshot._listEntity.push_back( std::move( entity ) );
            if ( reader.hasOverflowed() )
                return false;
        }
        if ( reader.hasOverflowed() )
            return false;
        outSnapshot.sortEntities();
        return true;
    }
} // namespace sw
