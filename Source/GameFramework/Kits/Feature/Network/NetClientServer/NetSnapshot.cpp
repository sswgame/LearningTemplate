#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/NetSnapshot.h"

#include "Core/Algorithm/CompareUtil.h"
#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Serialization/BitStream.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct NetSnapshotInternal
        {
            /** @brief id 오름차순 목록에서 @p entityID 를 찾는 `lower_bound` 비교자입니다. */
            static bool isEntityBefore( const NetEntityState& entity, uint32 entityID ) { return entity._entityID < entityID; }
            static bool isSentEntityBefore( const NetSentEntity& entity, uint32 entityID ) { return entity._entityID < entityID; }

            /**
             * @brief 사라진 엔티티 id 를 예산이 허락하는 만큼 앞에서부터 씁니다. 개수 필드는 가장 큰 값으로 잡습니다.
             * @return 실은 개수입니다. 나머지는 다음 델타가 다시 고릅니다.
             */
            static size_t writeRemovals( BitWriter& writer, NetSendBudget& budget, const vector<uint32>& listRemoved )
            {
                budget.reserveBits( BitMath::computeVarUintBits( listRemoved.size() ) );
                size_t removedCount = 0;
                while ( removedCount < listRemoved.size() && budget.tryReserveBits( BitMath::computeVarUintBits( listRemoved[removedCount] ) ) )
                {
                    ++removedCount;
                }
                writer.writeVarUint( removedCount );
                for ( size_t index = 0; index < removedCount; ++index )
                {
                    writer.writeVarUint( listRemoved[index] );
                }
                return removedCount;
            }

            /** @brief 조각 @p chunkIndex 의 바이트 수입니다. */
            static size_t getChunkSize( size_t byteCount, size_t chunkIndex )
            {
                return MathUtil::min( NetSnapshot::kDeltaChunkBytes, byteCount - chunkIndex * NetSnapshot::kDeltaChunkBytes );
            }

            /** @brief 받는 쪽 판이 @p baselineRevision 일 때 조각 델타의 비트 수입니다(마스크 + 바뀐 조각). 조각으로 이을 수 없으면(모양이 바뀌었다 · 조각 판이 없다) 비어 있습니다. */
            static optional<int32> computeChunkDeltaBits( const NetEntityState& entity, uint64 baselineRevision )
            {
                const size_t chunkCount = NetSnapshot::computeChunkCount( entity._buffer.size() );
                if ( baselineRevision < entity._shapeRevision || entity._listChunkRevision.size() != chunkCount || chunkCount == 0 )
                    return nullopt;
                size_t bits = chunkCount;
                for ( size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
                {
                    if ( entity._listChunkRevision[chunkIndex] > baselineRevision )
                        bits += getChunkSize( entity._buffer.size(), chunkIndex ) * kBitsPerByte;
                }
                return static_cast<int32>( bits );
            }

            static void writeChunkDelta( BitWriter& writer, const NetEntityState& entity, uint64 baselineRevision )
            {
                const size_t chunkCount = entity._listChunkRevision.size();
                for ( size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
                {
                    writer.writeBool( entity._listChunkRevision[chunkIndex] > baselineRevision );
                }
                for ( size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
                {
                    if ( entity._listChunkRevision[chunkIndex] > baselineRevision )
                        writer.writeBytes( entity._buffer.data() + chunkIndex * NetSnapshot::kDeltaChunkBytes, static_cast<int32>( getChunkSize( entity._buffer.size(), chunkIndex ) ) );
                }
            }

            /** @brief 기준 바이트 @p inoutBuffer 위에 바뀐 조각을 덮습니다. 조각 수는 기준의 바이트 수가 정한다(받은 수를 믿지 않는다). */
            [[nodiscard]] static bool readChunkDelta( BitReader& reader, vector<uint8>& inoutBuffer )
            {
                const size_t chunkCount = NetSnapshot::computeChunkCount( inoutBuffer.size() );
                if ( chunkCount == 0 )
                    return false; // 빈 상태는 조각으로 바뀔 것이 없다 — 깨진 데이터다
                uint64 arrMask[kMaskWordCount]{};
                for ( size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
                {
                    if ( reader.readBool() )
                        arrMask[chunkIndex / kBitsPerMaskWord] |= uint64{ 1 } << ( chunkIndex % kBitsPerMaskWord );
                }
                for ( size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
                {
                    if ( ( arrMask[chunkIndex / kBitsPerMaskWord] & ( uint64{ 1 } << ( chunkIndex % kBitsPerMaskWord ) ) ) == 0 )
                        continue;
                    if ( reader.readBytes( inoutBuffer.data() + chunkIndex * NetSnapshot::kDeltaChunkBytes, static_cast<int32>( getChunkSize( inoutBuffer.size(), chunkIndex ) ) ) == false )
                        return false;
                }
                return reader.hasOverflowed() == false;
            }

        private:
            static constexpr size_t kBitsPerByte     = 8;
            static constexpr size_t kBitsPerMaskWord = sizeof( uint64 ) * kBitsPerByte;
            /// 상태 바이트 상한(`kMaxEntityBytes`)의 조각을 모두 담는다 — 복원본에는 상한을 넘는 엔티티가 없다(`readBlob` 이 막는다).
            static constexpr size_t kMaskWordCount = ( NetSnapshot::kMaxEntityBytes / NetSnapshot::kDeltaChunkBytes + kBitsPerMaskWord ) / kBitsPerMaskWord;
        };
    } // namespace
} // namespace sw

namespace sw
{
    const NetEntityState* NetSnapshot::findEntity( uint32 entityID ) const
    {
        const auto entityIter = std::lower_bound( _listEntity.begin(), _listEntity.end(), entityID, &NetSnapshotInternal::isEntityBefore );
        return entityIter != _listEntity.end() && entityIter->_entityID == entityID ? &*entityIter : nullptr;
    }

    void NetSnapshot::sortEntities()
    {
        VectorUtil::sort( _listEntity, Ascending<&NetEntityState::_entityID>{} );
    }

    const NetSentEntity* NetSentSnapshot::findEntity( uint32 entityID ) const
    {
        const auto entityIter = std::lower_bound( _listEntity.begin(), _listEntity.end(), entityID, &NetSnapshotInternal::isSentEntityBefore );
        return entityIter != _listEntity.end() && entityIter->_entityID == entityID ? &*entityIter : nullptr;
    }

    void NetSnapshot::updateChunkRevision( NetEntityState& inoutEntity, NetEntityState* pPrevious )
    {
        const size_t byteCount  = inoutEntity._buffer.size();
        const size_t chunkCount = computeChunkCount( byteCount );
        const bool   bSameShape = pPrevious != nullptr && pPrevious->_typeID == inoutEntity._typeID && pPrevious->_buffer.size() == byteCount &&
                                pPrevious->_listChunkRevision.size() == chunkCount;
        if ( bSameShape == false )
        {
            inoutEntity._shapeRevision = inoutEntity._revision;
            inoutEntity._listChunkRevision.assign( chunkCount, inoutEntity._revision );
            return;
        }
        inoutEntity._shapeRevision = pPrevious->_shapeRevision;
        inoutEntity._listChunkRevision.swap( pPrevious->_listChunkRevision );
        if ( inoutEntity._revision == pPrevious->_revision )
            return;
        for ( size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex )
        {
            const size_t offset = chunkIndex * kDeltaChunkBytes;
            if ( Memory::compare( inoutEntity._buffer.data() + offset, pPrevious->_buffer.data() + offset, NetSnapshotInternal::getChunkSize( byteCount, chunkIndex ) ) != 0 )
                inoutEntity._listChunkRevision[chunkIndex] = inoutEntity._revision;
        }
    }

    void NetSnapshot::writeDelta( BitWriter& writer, const NetDeltaSource& source, const NetSentSnapshot* pBaseline, int32 maxBytes, NetSentSnapshot& outWritten,
                                  vector<uint8>& outListCurrent )
    {
        const vector<NetEntityState>& listWorld    = source._listWorld;
        const vector<uint32>&         listRelevant = source._listRelevant;
        const NetOwnerState&          ownerState   = source._ownerState;
        outListCurrent.assign( listRelevant.size(), kEntityNotSent );
        SW_ASSERT( pBaseline != &outWritten );
        SW_ASSERT( static_cast<int32>( ownerState._buffer.size() ) <= kMaxEntityBytes ); // `ReplicationServer::setOwnerState` 가 넘는 상태를 거절한다
        outWritten._tick = source._tick;
        writer.writeVarUint( source._tick );
        writer.writeVarUint( pBaseline != nullptr ? pBaseline->_tick + 1u : 0u ); // 0이면 기준이 없다
        // 소유자 상태는 엔티티 예산 순서 밖에서 늘 통째로 싣는다. 아래 예산이 이미 쓴 비트로 함께 센다.
        writer.writeBool( ownerState._bPresent == SW_TRUE );
        if ( ownerState._bPresent == SW_TRUE )
        {
            writer.writeVarUint( ownerState._inputTick );
            writer.writeBlob( ownerState._buffer.data(), static_cast<int32>( ownerState._buffer.size() ) );
        }
        writer.writeVarUint( source._firstMissingInputTick );
        // 예산은 메시지 전체에 적용한다. 앞서 쓴 종류 바이트와 헤더, 끝 표시 1비트를 먼저 센다.
        NetSendBudget budget( maxBytes );
        budget.reserveBits( writer.getBitCount() + 1 );

        // 사라진 엔티티는 기준에는 있고 받는 쪽이 가질 목록에는 없는 것이다 — 둘 다 id 오름차순이라 나란히 걷는다.
        // 개수 필드는 가장 큰 값으로 잡고 들어가는 만큼만 싣는다. 나머지는 다음 델타가 다시 고른다.
        vector<uint32> listRemoved; // 사라진 엔티티가 있는 틱에만 할당한다
        if ( pBaseline != nullptr )
        {
            size_t relevantCursor = 0;
            for ( const NetSentEntity& old : pBaseline->_listEntity )
            {
                while ( relevantCursor < listRelevant.size() && listWorld[listRelevant[relevantCursor]]._entityID < old._entityID )
                    ++relevantCursor;
                const bool bStillRelevant = relevantCursor < listRelevant.size() && listWorld[listRelevant[relevantCursor]]._entityID == old._entityID;
                if ( bStillRelevant == false )
                    listRemoved.push_back( old._entityID );
            }
        }
        const size_t removedCount = NetSnapshotInternal::writeRemovals( writer, budget, listRemoved );

        // 기록의 시작은 기준에서 지금도 있는 엔티티와, 사라졌지만 이번에 못 실은 엔티티이다. 못 실은 엔티티는 받는 쪽이 아직 가지고 있다.
        // 항목은 16 바이트 값이라 덮어써도 용량만 다시 쓴다(틱마다 할당하지 않는다).
        vector<NetSentEntity>& listWritten = outWritten._listEntity;
        listWritten.clear();
        if ( pBaseline != nullptr )
        {
            for ( const NetSentEntity& old : pBaseline->_listEntity )
            {
                const bool bRemovalSent = std::binary_search( listRemoved.begin(), listRemoved.begin() + static_cast<ptrdiff_t>( removedCount ), old._entityID );
                if ( bRemovalSent == false )
                    listWritten.push_back( old );
            }
        }
        const size_t baselineCount = listWritten.size(); // [0, baselineCount) 는 기준의 순서(id 오름차순)이고 판도 기준의 것이다
        bool         bAppended     = false;

        // 판이 바뀐 엔티티는 예산이 넘치면 건너뛰고, 실은 것만 기록에 반영한다. 나머지는 기준 값으로 남는다.
        for ( const int32 orderIndex : source._listOrder )
        {
            const size_t          relevantIndex = static_cast<size_t>( orderIndex );
            const NetEntityState& entity        = listWorld[listRelevant[relevantIndex]];
            // 붙이면 저장 공간이 옮겨질 수 있으므로 시작 위치는 매번 다시 읽는다. 엔티티는 순서에 한 번만 나오므로 찾은 항목의 판은 아직 기준의 판이다.
            NetSentEntity* const pBegin      = listWritten.data();
            NetSentEntity* const pEnd        = pBegin + baselineCount;
            NetSentEntity* const pFound      = std::lower_bound( pBegin, pEnd, entity._entityID, &NetSnapshotInternal::isSentEntityBefore );
            const bool           bInBaseline = pFound != pEnd && pFound->_entityID == entity._entityID;
            if ( bInBaseline && pFound->_revision == entity._revision )
            {
                outListCurrent[relevantIndex] = kEntityAlreadyCurrent;
                continue;
            }
            const int32 size = static_cast<int32>( entity._buffer.size() );
            if ( size > kMaxEntityBytes )
                continue; // 받는 쪽이 읽지 않는 크기이므로 싣지도, 기록에 넣지도 않는다
            // 받는 쪽이 기준 판을 가졌으면 그 뒤에 바뀐 조각만, 아니면(새 엔티티 · 모양이 바뀜) 통째로 싣는다. 조각이 더 크면 통째다.
            const int32           fullBits   = BitMath::computeVarUintBits( entity._typeID ) + BitMath::computeBlobBits( size );
            const optional<int32> chunkBits  = bInBaseline ? NetSnapshotInternal::computeChunkDeltaBits( entity, pFound->_revision ) : optional<int32>{};
            const bool            bChunks    = chunkBits.has_value() && *chunkBits < fullBits;
            const int32           entityBits = 1 + BitMath::computeVarUintBits( entity._entityID ) + 1 + ( bChunks ? *chunkBits : fullBits );
            if ( budget.tryReserveBits( entityBits ) == false )
                continue; // 뒤에 오는 작은 엔티티는 들어갈 수 있다
            writer.writeBool( true );
            writer.writeVarUint( entity._entityID );
            writer.writeBool( bChunks );
            if ( bChunks )
            {
                NetSnapshotInternal::writeChunkDelta( writer, entity, pFound->_revision );
            }
            else
            {
                writer.writeVarUint( entity._typeID );
                writer.writeBlob( entity._buffer.data(), size );
            }
            outListCurrent[relevantIndex] = kEntityWritten;
            // 기준에 있던 엔티티는 그 항목의 판을 바꾸고, 새 엔티티는 뒤에 붙인 뒤 끝에서 한 번 정렬한다.
            if ( bInBaseline )
            {
                pFound->_revision = entity._revision;
            }
            else
            {
                listWritten.push_back( NetSentEntity{ entity._revision, entity._entityID } );
                bAppended = true;
            }
        }
        writer.writeBool( false );
        if ( bAppended )
            VectorUtil::sort( listWritten, Ascending<&NetSentEntity::_entityID>{} );
    }

    bool NetSnapshot::readDelta( BitReader& reader, const NetSnapshot* pBaseline, NetSnapshot& outSnapshot )
    {
        SW_ASSERT( pBaseline != &outSnapshot );
        outSnapshot._tick           = reader.readVarUint32();
        const uint32   baselineCode = reader.readVarUint32();
        NetOwnerState& ownerState   = outSnapshot._ownerState;
        ownerState._bPresent        = reader.readBool() ? SW_TRUE : SW_FALSE;
        if ( ownerState._bPresent == SW_TRUE )
        {
            ownerState._inputTick = reader.readVarUint32();
            if ( reader.readBlob( ownerState._buffer, kMaxEntityBytes ) == false )
                return false;
        }
        else
        {
            ownerState._inputTick = 0;
            ownerState._buffer.clear();
        }
        outSnapshot._firstMissingInputTick = reader.readVarUint32();
        if ( baselineCode != 0 && ( pBaseline == nullptr || pBaseline->_tick != baselineCode - 1u ) )
            return false; // 기준이 없으므로 서버가 곧 다른 기준으로 다시 보낸다
        // 기준에서 시작한다. 벡터 복사 대입은 이미 있는 원소에 대입하므로 @p outSnapshot 의 엔티티 버퍼 용량을 재사용한다.
        if ( baselineCode != 0 )
            outSnapshot._listEntity = pBaseline->_listEntity;
        else
            outSnapshot._listEntity.clear();
        const uint64 removedCount = reader.readVarUint();
        if ( reader.hasOverflowed() || removedCount > 65535 )
            return false;
        // 사라진 id 를 모아 한 번에 지운다. 찾을 때는 이분 탐색을 쓴다.
        if ( removedCount > 0 )
        {
            vector<uint32> listRemoved( static_cast<size_t>( removedCount ) );
            for ( uint32& entityID : listRemoved )
            {
                entityID = reader.readVarUint32();
            }
            if ( reader.hasOverflowed() )
                return false;
            VectorUtil::sort( listRemoved );
            VectorUtil::removeIf( outSnapshot._listEntity, [&listRemoved]( const NetEntityState& entity )
            { return std::binary_search( listRemoved.begin(), listRemoved.end(), entity._entityID ); } );
        }
        // 기준에 있던 엔티티는 그 항목의 버퍼에 바로 읽고(id 순이므로 이분 탐색), 새 엔티티는 뒤에 붙인 뒤 끝에서 한 번 정렬한다.
        const size_t baselineCount = outSnapshot._listEntity.size(); // [0, baselineCount) 는 id 오름차순이다
        while ( reader.readBool() )
        {
            const uint32          entityID    = reader.readVarUint32();
            const bool            bChunks     = reader.readBool();
            NetEntityState* const pBegin      = outSnapshot._listEntity.data();
            NetEntityState* const pEnd        = pBegin + baselineCount;
            NetEntityState*       pEntity     = std::lower_bound( pBegin, pEnd, entityID, &NetSnapshotInternal::isEntityBefore );
            const bool            bInBaseline = pEntity != pEnd && pEntity->_entityID == entityID;
            if ( bChunks )
            {
                if ( bInBaseline == false || NetSnapshotInternal::readChunkDelta( reader, pEntity->_buffer ) == false )
                    return false; // 이을 기준이 없는 조각은 깨진 데이터다
                continue;
            }
            const uint32 typeID = reader.readVarUint32();
            if ( bInBaseline == false )
                pEntity = &outSnapshot._listEntity.emplace_back();
            pEntity->_entityID = entityID;
            pEntity->_typeID   = typeID;
            if ( reader.readBlob( pEntity->_buffer, kMaxEntityBytes ) == false )
                return false;
            if ( reader.hasOverflowed() )
                return false;
        }
        if ( reader.hasOverflowed() )
            return false;
        outSnapshot.sortEntities();
        return true;
    }
} // namespace sw
