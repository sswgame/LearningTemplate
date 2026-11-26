#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/GimmickSnapshotUtil.h"

// 기믹 회로 상태 ↔ 스냅샷 엔티티 — 작은 회로는 조각 하나, 큰 회로는 여러 조각으로 실리고 모아 읽으면 같은 상태다. 조각이 빠진 회로 · 다른 종류 번호의 엔티티는 건너뛴다.

using namespace sw;

namespace
{
    struct GimmickSnapshotUtilTestInternal
    {
        static constexpr uint32 kFirstEntityID = 1000;
        static constexpr uint32 kTypeID        = 7;
        static constexpr uint32 kOtherTypeID   = 8;

        /** @brief 노드 @p nodeCount 개짜리 회로 상태입니다. 노드마다 값이 달라 바이트가 조각 경계를 넘는다. */
        static GimmickStateEntry makeEntry( uint64 ownerKey, uint32 stepIndex, uint32 nodeCount )
        {
            GimmickStateEntry entry;
            entry._ownerKey         = ownerKey;
            entry._state._stepIndex = stepIndex;
            for ( uint32 index = 0; index < nodeCount; ++index )
            {
                GimmickNodeState node;
                node._id          = 0x1000u + index;
                node._kind        = 0x2000u + index;
                node._outputBits  = index;
                node._sensorValue = static_cast<float32>( index ) * 0.5f;
                node._listIntState.push_back( static_cast<int32>( index ) );
                entry._state._listNode.push_back( node );
            }
            return entry;
        }

        static size_t countEntitiesInRange( const NetSnapshot& snapshot, uint32 circuitIndex )
        {
            const uint32 begin = kFirstEntityID + circuitIndex * GimmickSnapshotUtil::kMaxChunksPerCircuit;
            size_t       count = 0;
            for ( const NetEntityState& entity : snapshot._listEntity )
            {
                if ( begin <= entity._entityID && entity._entityID < begin + GimmickSnapshotUtil::kMaxChunksPerCircuit )
                    ++count;
            }
            return count;
        }
    };
} // namespace

SW_TEST_CASE( GimmickSnapshotUtilTest, SmallAndChunkedCircuitsRoundTrip )
{
    using Internal = GimmickSnapshotUtilTestInternal;
    GimmickStateBundle bundle;
    bundle._listEntry.push_back( Internal::makeEntry( 11, 3, 1 ) );
    bundle._listEntry.push_back( Internal::makeEntry( 22, 9, 40 ) ); // 노드 40 개 — 조각 하나(245 바이트)를 넘는다

    NetSnapshot snapshot;
    SW_EXPECT_EQUAL( 0u, GimmickSnapshotUtil::appendEntities( snapshot, bundle, Internal::kFirstEntityID, Internal::kTypeID ) );
    SW_EXPECT_EQUAL( size_t{ 1 }, Internal::countEntitiesInRange( snapshot, 0 ) );
    SW_EXPECT_TRUE( Internal::countEntitiesInRange( snapshot, 1 ) > 1 );

    GimmickStateBundle received;
    SW_EXPECT_EQUAL( 0u, GimmickSnapshotUtil::extractBundle( snapshot, Internal::kFirstEntityID, Internal::kTypeID, received ) );
    SW_REQUIRE_EQUAL( size_t{ 2 }, received._listEntry.size() );
    SW_EXPECT_EQUAL( uint64{ 11 }, received._listEntry[0]._ownerKey );
    SW_EXPECT_EQUAL( 3u, received._listEntry[0]._state._stepIndex );
    SW_EXPECT_EQUAL( uint64{ 22 }, received._listEntry[1]._ownerKey );
    SW_EXPECT_EQUAL( 9u, received._listEntry[1]._state._stepIndex );
    SW_REQUIRE_EQUAL( size_t{ 40 }, received._listEntry[1]._state._listNode.size() );
    SW_EXPECT_EQUAL( 0x1000u + 39u, received._listEntry[1]._state._listNode[39]._id );
    SW_EXPECT_EQUAL( 39u, received._listEntry[1]._state._listNode[39]._outputBits );
}

SW_TEST_CASE( GimmickSnapshotUtilTest, MissingChunkSkipsOnlyThatCircuit )
{
    using Internal = GimmickSnapshotUtilTestInternal;
    GimmickStateBundle bundle;
    bundle._listEntry.push_back( Internal::makeEntry( 11, 3, 1 ) );
    bundle._listEntry.push_back( Internal::makeEntry( 22, 9, 40 ) );
    NetSnapshot snapshot;
    SW_REQUIRE_EQUAL( 0u, GimmickSnapshotUtil::appendEntities( snapshot, bundle, Internal::kFirstEntityID, Internal::kTypeID ) );

    // 둘째 회로의 첫 조각을 뺀다 — 델타가 못 실은 조각과 같다.
    const uint32 droppedID = Internal::kFirstEntityID + GimmickSnapshotUtil::kMaxChunksPerCircuit;
    for ( size_t index = 0; index < snapshot._listEntity.size(); ++index )
    {
        if ( snapshot._listEntity[index]._entityID == droppedID )
        {
            snapshot._listEntity.erase( snapshot._listEntity.begin() + static_cast<ptrdiff_t>( index ) );
            break;
        }
    }

    GimmickStateBundle received;
    SW_EXPECT_EQUAL( 1u, GimmickSnapshotUtil::extractBundle( snapshot, Internal::kFirstEntityID, Internal::kTypeID, received ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, received._listEntry.size() );
    SW_EXPECT_EQUAL( uint64{ 11 }, received._listEntry[0]._ownerKey );
}

SW_TEST_CASE( GimmickSnapshotUtilTest, OtherTypeAndOutOfRangeEntitiesAreIgnored )
{
    using Internal = GimmickSnapshotUtilTestInternal;
    GimmickStateBundle bundle;
    bundle._listEntry.push_back( Internal::makeEntry( 11, 3, 1 ) );
    NetSnapshot snapshot;
    SW_REQUIRE_EQUAL( 0u, GimmickSnapshotUtil::appendEntities( snapshot, bundle, Internal::kFirstEntityID, Internal::kOtherTypeID ) );
    NetEntityState outside;
    outside._entityID = Internal::kFirstEntityID - 1;
    outside._typeID   = Internal::kTypeID;
    outside._buffer.assign( GimmickSnapshotUtil::kChunkHeaderBytes + 4, 0 );
    snapshot._listEntity.push_back( outside );
    snapshot.sortEntities();

    GimmickStateBundle received;
    SW_EXPECT_EQUAL( 0u, GimmickSnapshotUtil::extractBundle( snapshot, Internal::kFirstEntityID, Internal::kTypeID, received ) );
    SW_EXPECT_TRUE( received._listEntry.empty() );
}
