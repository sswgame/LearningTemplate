#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/GimmickSnapshotUtil.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        /** @brief 스냅샷에서 읽은 조각 하나입니다. */
        struct GimmickChunk
        {
            const NetEntityState* _pEntity{ nullptr };
            uint64                _ownerKey{ 0 };
            uint8                 _index{ 0 };
            uint8                 _count{ 0 };
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 GimmickSnapshotUtil::appendEntities( NetSnapshot& inoutSnapshot, const GimmickStateBundle& bundle, uint32 firstEntityID, uint32 typeID )
    {
        uint32 notSent = 0;
        for ( size_t circuitIndex = 0; circuitIndex < bundle._listEntry.size(); ++circuitIndex )
        {
            const GimmickStateEntry& entry = bundle._listEntry[circuitIndex];
            if ( circuitIndex >= kMaxCircuitCount )
            {
                ++notSent;
                continue;
            }
            BitWriter           writer;
            const bool          bWritten = FixedLayout::write( entry._state, writer );
            const vector<uint8> bytes( writer.getBytes().begin(), writer.getBytes().begin() + writer.getByteCount() );
            if ( bWritten == false || bytes.size() > kMaxCircuitBytes )
            {
                ++notSent;
                continue;
            }
            const uint32 chunkCount = MathUtil::max( 1u, static_cast<uint32>( ( bytes.size() + kChunkPayloadBytes - 1 ) / kChunkPayloadBytes ) );
            for ( uint32 chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
            {
                const size_t   offset  = static_cast<size_t>( chunkIndex ) * kChunkPayloadBytes;
                const size_t   payload = MathUtil::min( static_cast<size_t>( kChunkPayloadBytes ), bytes.size() - offset );
                NetEntityState state;
                state._entityID = firstEntityID + static_cast<uint32>( circuitIndex ) * kMaxChunksPerCircuit + chunkIndex;
                state._typeID   = typeID;
                LittleEndianUtil::appendUint64( state._buffer, entry._ownerKey );
                state._buffer.push_back( static_cast<uint8>( chunkIndex ) );
                state._buffer.push_back( static_cast<uint8>( chunkCount ) );
                state._buffer.insert( state._buffer.end(), bytes.begin() + static_cast<ptrdiff_t>( offset ), bytes.begin() + static_cast<ptrdiff_t>( offset + payload ) );
                inoutSnapshot._listEntity.push_back( std::move( state ) );
            }
        }
        inoutSnapshot.sortEntities();
        return notSent;
    }

    uint32 GimmickSnapshotUtil::extractBundle( const NetSnapshot& snapshot, uint32 firstEntityID, uint32 typeID, GimmickStateBundle& outBundle )
    {
        outBundle._listEntry.clear();
        const uint64 rangeEnd = static_cast<uint64>( firstEntityID ) + static_cast<uint64>( kMaxCircuitCount ) * kMaxChunksPerCircuit;

        vector<GimmickChunk> listChunk;
        for ( const NetEntityState& entity : snapshot._listEntity )
        {
            const bool bInRange    = firstEntityID <= entity._entityID && entity._entityID < rangeEnd;
            const bool bSkipEntity = bInRange == false || entity._typeID != typeID || entity._buffer.size() < kChunkHeaderBytes;
            if ( bSkipEntity )
                continue;
            GimmickChunk chunk;
            chunk._pEntity  = &entity;
            chunk._ownerKey = LittleEndianUtil::readUint64( entity._buffer.data() );
            chunk._index    = entity._buffer[8];
            chunk._count    = entity._buffer[9];
            listChunk.push_back( chunk );
        }
        std::sort( listChunk.begin(), listChunk.end(), []( const GimmickChunk& lhs, const GimmickChunk& rhs )
        { return lhs._ownerKey != rhs._ownerKey ? lhs._ownerKey < rhs._ownerKey : lhs._index < rhs._index; } );

        uint32 skipped = 0;
        for ( size_t begin = 0; begin < listChunk.size(); )
        {
            size_t end = begin;
            while ( end < listChunk.size() && listChunk[end]._ownerKey == listChunk[begin]._ownerKey )
            {
                ++end;
            }
            // 조각이 0 .. count-1 이 하나씩 모두 있어야 회로 하나다.
            const uint8 chunkCount = listChunk[begin]._count;
            bool        bComplete  = chunkCount >= 1 && chunkCount <= kMaxChunksPerCircuit && end - begin == chunkCount;
            for ( size_t index = begin; index < end && bComplete; ++index )
            {
                bComplete = listChunk[index]._count == chunkCount && listChunk[index]._index == index - begin;
            }
            bool bRead = false;
            if ( bComplete )
            {
                vector<uint8> bytes;
                for ( size_t index = begin; index < end; ++index )
                {
                    const vector<uint8>& buffer = listChunk[index]._pEntity->_buffer;
                    bytes.insert( bytes.end(), buffer.begin() + kChunkHeaderBytes, buffer.end() );
                }
                GimmickStateEntry entry;
                entry._ownerKey = listChunk[begin]._ownerKey;
                bRead           = FixedLayout::read( bytes.data(), static_cast<int32>( bytes.size() ), entry._state ) == FixedReadResult::Ok;
                if ( bRead )
                    outBundle._listEntry.push_back( std::move( entry ) );
            }
            if ( bRead == false )
                ++skipped;
            begin = end;
        }
        return skipped;
    }
} // namespace sw
