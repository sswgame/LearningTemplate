#include "pch.h"

#include "GameFramework/Kits/Network/NetClientServer/NetSnapshot.h"

#include "Core/Network/BitStream.h"
#include "Core/Network/NetSendBudget.h"

#include <algorithm>

namespace sw
{
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
        // 예산은 메시지 전체 — 앞서 쓴 종류 바이트 · 머리, 끝 표시 1 비트를 먼저 센다.
        NetSendBudget budget( maxBytes );
        budget.reserveBits( writer.getBitCount() + 1 );

        // 사라진 것 — 기준에는 있고 지금은 없다. 개수 칸은 가장 큰 값으로 잡고, 들어가는 만큼만 싣는다(나머지는 다음 델타가 다시 고른다).
        vector<uint32> listRemoved;
        if ( pBaseline != nullptr )
        {
            for ( const NetEntityState& old : pBaseline->_listEntity )
            {
                if ( findEntity( old._entityId ) == nullptr )
                    listRemoved.push_back( old._entityId );
            }
        }
        budget.reserveBits( BitMath::computeVarUintBits( listRemoved.size() ) );
        size_t removedCount = 0;
        while ( removedCount < listRemoved.size() && budget.tryReserveBits( BitMath::computeVarUintBits( listRemoved[removedCount] ) ) )
            ++removedCount;
        writer.writeVarUint( removedCount );
        for ( size_t index = 0; index < removedCount; ++index )
            writer.writeVarUint( listRemoved[index] );

        // 재구성의 시작 = 기준에서 지금도 있는 것 + 사라졌지만 이번에 못 실은 것(받는 쪽은 아직 가지고 있다).
        if ( pBaseline != nullptr )
        {
            for ( const NetEntityState& old : pBaseline->_listEntity )
            {
                const bool bRemovalSent = std::binary_search( listRemoved.begin(), listRemoved.begin() + static_cast<ptrdiff_t>( removedCount ), old._entityId );
                if ( bRemovalSent == false )
                    outWritten._listEntity.push_back( old );
            }
        }

        // 바뀐 것 — 넘치면 건너뛰고 실은 것만 재구성에 반영한다(나머지는 기준 값으로 남는다).
        const size_t entityCount = pListOrder != nullptr ? pListOrder->size() : _listEntity.size();
        for ( size_t orderIndex = 0; orderIndex < entityCount; ++orderIndex )
        {
            const NetEntityState& entity = _listEntity[pListOrder != nullptr ? static_cast<size_t>( ( *pListOrder )[orderIndex] ) : orderIndex];
            const NetEntityState* pOld   = pBaseline != nullptr ? pBaseline->findEntity( entity._entityId ) : nullptr;
            if ( pOld != nullptr && pOld->_typeId == entity._typeId && pOld->_buffer == entity._buffer )
                continue;
            const int32 size = static_cast<int32>( entity._buffer.size() );
            if ( size > kMaxEntityBytes )
                continue; // 받는 쪽이 읽지 않는 크기 — 싣지도 재구성에 넣지도 않는다
            const int32 entityBits = 1 + BitMath::computeVarUintBits( entity._entityId ) + BitMath::computeVarUintBits( entity._typeId ) + BitMath::computeBlobBits( size );
            if ( budget.tryReserveBits( entityBits ) == false )
                continue; // 작은 다음 것은 들어갈 수 있다
            writer.writeBool( true );
            writer.writeVarUint( entity._entityId );
            writer.writeVarUint( entity._typeId );
            writer.writeBlob( entity._buffer.data(), size );
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
            if ( reader.readBlob( entity._buffer, kMaxEntityBytes ) == false )
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
