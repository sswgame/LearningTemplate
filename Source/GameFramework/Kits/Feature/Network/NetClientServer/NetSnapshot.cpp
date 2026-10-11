#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/NetSnapshot.h"

#include "Core/Network/BitStream.h"
#include "Core/Network/Message/NetSendBudget.h"

#include <algorithm>

namespace sw
{
    const NetEntityState* NetSnapshot::findEntity( uint32 entityID ) const
    {
        const auto entityIter = std::lower_bound( _listEntity.begin(), _listEntity.end(), entityID,
                                                  []( const NetEntityState& entity, uint32 id )
        { return entity._entityID < id; } );
        return entityIter != _listEntity.end() && entityIter->_entityID == entityID ? &*entityIter : nullptr;
    }

    void NetSnapshot::sortEntities()
    {
        std::sort( _listEntity.begin(), _listEntity.end(), []( const NetEntityState& lhs, const NetEntityState& rhs )
        { return lhs._entityID < rhs._entityID; } );
    }

    void NetSnapshot::writeDelta( BitWriter& writer, const NetSnapshot* pBaseline, int32 maxBytes, NetSnapshot& outWritten, const vector<int32>* pListOrder,
                                  vector<uint8>* pOutListCurrent ) const
    {
        if ( pOutListCurrent != nullptr )
            pOutListCurrent->assign( _listEntity.size(), kEntityNotSent );
        // 복원 결과는 @p outWritten 의 엔티티 항목을 덮어쓴다. 버퍼 용량이 남으므로 틱마다 엔티티 수만큼 할당하지 않는다. 서버는 보낸 링 버퍼의 슬롯을 넘긴다.
        SW_ASSERT( pBaseline != &outWritten && this != &outWritten );
        outWritten._tick                   = _tick;
        outWritten._lastProcessedInputTick = _lastProcessedInputTick;
        outWritten._firstMissingInputTick  = _firstMissingInputTick;
        size_t     writtenCount            = 0;
        const auto appendWritten           = [&outWritten, &writtenCount]( const NetEntityState& entity )
        {
            if ( writtenCount < outWritten._listEntity.size() )
                outWritten._listEntity[writtenCount] = entity;
            else
                outWritten._listEntity.push_back( entity );
            ++writtenCount;
        };
        writer.writeVarUint( _tick );
        writer.writeVarUint( pBaseline != nullptr ? pBaseline->_tick + 1u : 0u ); // 0이면 기준이 없다
        writer.writeVarUint( _lastProcessedInputTick );
        writer.writeVarUint( _firstMissingInputTick );
        // 예산은 메시지 전체에 적용한다. 앞서 쓴 종류 바이트와 헤더, 끝 표시 1비트를 먼저 센다.
        NetSendBudget budget( maxBytes );
        budget.reserveBits( writer.getBitCount() + 1 );

        // 사라진 엔티티는 기준에는 있고 지금은 없는 것이다. 개수 필드는 가장 큰 값으로 잡고 들어가는 만큼만 싣는다. 나머지는 다음 델타가 다시 고른다.
        vector<uint32> listRemoved;
        if ( pBaseline != nullptr )
        {
            for ( const NetEntityState& old : pBaseline->_listEntity )
            {
                if ( findEntity( old._entityID ) == nullptr )
                    listRemoved.push_back( old._entityID );
            }
        }
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

        // 복원 스냅샷의 시작은 기준에서 지금도 있는 엔티티와, 사라졌지만 이번에 못 실은 엔티티이다. 못 실은 엔티티는 받는 쪽이 아직 가지고 있다.
        if ( pBaseline != nullptr )
        {
            for ( const NetEntityState& old : pBaseline->_listEntity )
            {
                const bool bRemovalSent = std::binary_search( listRemoved.begin(), listRemoved.begin() + static_cast<ptrdiff_t>( removedCount ), old._entityID );
                if ( bRemovalSent == false )
                    appendWritten( old );
            }
        }
        const size_t baselineCount = writtenCount; // [0, baselineCount) 는 기준의 순서(id 오름차순)이다

        // 바뀐 엔티티는 예산이 넘치면 건너뛰고, 실은 것만 복원 스냅샷에 반영한다. 나머지는 기준 값으로 남는다.
        const size_t entityCount = pListOrder != nullptr ? pListOrder->size() : _listEntity.size();
        for ( size_t orderIndex = 0; orderIndex < entityCount; ++orderIndex )
        {
            const size_t          entityIndex = pListOrder != nullptr ? static_cast<size_t>( ( *pListOrder )[orderIndex] ) : orderIndex;
            const NetEntityState& entity      = _listEntity[entityIndex];
            const NetEntityState* pOld        = pBaseline != nullptr ? pBaseline->findEntity( entity._entityID ) : nullptr;
            if ( pOld != nullptr && pOld->_typeID == entity._typeID && pOld->_buffer == entity._buffer )
            {
                if ( pOutListCurrent != nullptr )
                    ( *pOutListCurrent )[entityIndex] = kEntityAlreadyCurrent;
                continue;
            }
            const int32 size = static_cast<int32>( entity._buffer.size() );
            if ( size > kMaxEntityBytes )
                continue; // 받는 쪽이 읽지 않는 크기이므로 싣지도, 복원 스냅샷에 넣지도 않는다
            const int32 entityBits = 1 + BitMath::computeVarUintBits( entity._entityID ) + BitMath::computeVarUintBits( entity._typeID ) + BitMath::computeBlobBits( size );
            if ( budget.tryReserveBits( entityBits ) == false )
                continue; // 뒤에 오는 작은 엔티티는 들어갈 수 있다
            writer.writeBool( true );
            writer.writeVarUint( entity._entityID );
            writer.writeVarUint( entity._typeID );
            writer.writeBlob( entity._buffer.data(), size );
            if ( pOutListCurrent != nullptr )
                ( *pOutListCurrent )[entityIndex] = kEntityWritten;
            // 기준에 있던 엔티티는 그 항목을 덮어쓰고(id 순이므로 이분 탐색), 새 엔티티는 뒤에 붙인 뒤 끝에서 한 번 정렬한다.
            // 붙이면 저장 공간이 옮겨질 수 있으므로 시작 위치는 매번 다시 읽는다.
            NetEntityState* const pBegin = outWritten._listEntity.data();
            NetEntityState* const pEnd   = pBegin + baselineCount;
            NetEntityState* const pFound = std::lower_bound( pBegin, pEnd, entity._entityID, []( const NetEntityState& written, uint32 entityID )
            { return written._entityID < entityID; } );
            if ( pFound != pEnd && pFound->_entityID == entity._entityID )
                *pFound = entity;
            else
                appendWritten( entity );
        }
        writer.writeBool( false );
        outWritten._listEntity.resize( writtenCount ); // 이전 복원 스냅샷에서 남은 항목을 자른다
        outWritten.sortEntities();
    }

    bool NetSnapshot::readDelta( BitReader& reader, const NetSnapshot* pBaseline, NetSnapshot& outSnapshot )
    {
        SW_ASSERT( pBaseline != &outSnapshot );
        outSnapshot._tick                   = static_cast<uint32>( reader.readVarUint() );
        const uint32 baselineCode           = static_cast<uint32>( reader.readVarUint() );
        outSnapshot._lastProcessedInputTick = static_cast<uint32>( reader.readVarUint() );
        outSnapshot._firstMissingInputTick  = static_cast<uint32>( reader.readVarUint() );
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
                entityID = static_cast<uint32>( reader.readVarUint() );
            }
            if ( reader.hasOverflowed() )
                return false;
            std::sort( listRemoved.begin(), listRemoved.end() );
            outSnapshot._listEntity.erase( std::remove_if( outSnapshot._listEntity.begin(), outSnapshot._listEntity.end(),
                                                           [&listRemoved]( const NetEntityState& entity )
            { return std::binary_search( listRemoved.begin(), listRemoved.end(), entity._entityID ); } ),
                                           outSnapshot._listEntity.end() );
        }
        // 기준에 있던 엔티티는 그 항목의 버퍼에 바로 읽고(id 순이므로 이분 탐색), 새 엔티티는 뒤에 붙인 뒤 끝에서 한 번 정렬한다.
        const size_t baselineCount = outSnapshot._listEntity.size(); // [0, baselineCount) 는 id 오름차순이다
        while ( reader.readBool() )
        {
            const uint32          entityID = static_cast<uint32>( reader.readVarUint() );
            const uint32          typeID   = static_cast<uint32>( reader.readVarUint() );
            NetEntityState* const pBegin   = outSnapshot._listEntity.data();
            NetEntityState* const pEnd     = pBegin + baselineCount;
            NetEntityState*       pEntity  = std::lower_bound( pBegin, pEnd, entityID, []( const NetEntityState& existing, uint32 id )
                   { return existing._entityID < id; } );
            if ( pEntity == pEnd || pEntity->_entityID != entityID )
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
