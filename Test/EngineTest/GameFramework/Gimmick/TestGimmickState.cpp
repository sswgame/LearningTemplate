// 기믹 회로 상태 묶음(고정 배치형) — 롤백 저장 · 불러오기, 다른 기계(같은 이름의 오브젝트)로 따라가기, 바이트 손상 거절, 네트워크 스냅샷 엔티티(조각) 왕복.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickStateBundle.h"
#include "GameFramework/Base/World/Spline/SplineComponent.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/GimmickSnapshotUtil.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/NetSnapshot.h"

#include "TestFramework/Harness/TestTick.h"
#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct GimmickStateTestInternal
    {
        /** @brief 곡선 위를 오가는 무버 회로 하나를 가진 발판 오브젝트를 만들고 플레이를 시작합니다. 두 매니저에 같은 이름으로 만들면 같은 장면이다. */
        static GimmickCircuitComponent* spawnPlatform( GameObjectManager& manager, const utf8* pName )
        {
            GameObject* pPlatform = manager.createGameObject( hashed_string( pName ) );
            pPlatform->addComponent<SceneComponent>();
            SplineComponent* pSpline = pPlatform->addComponent<SplineComponent>();
            manager.flushSceneTransforms();
            pSpline->setControlPoints( {
                                           float3{ 0.0f, 0.0f,  0.0f},
                                           float3{10.0f, 0.0f,  0.0f},
                                           float3{10.0f, 0.0f, 10.0f}
            },
                                       SplineType::Linear, false );
            GimmickCircuitComponent* pCircuit = pPlatform->addComponent<GimmickCircuitComponent>();
            pCircuit->addNode( "move", "Mover", "speed=5; mode=PingPong" );
            if ( pCircuit->rebuild() == false )
                return nullptr;
            return pCircuit;
        }

        static float32 getX( const GameObject* pObject ) { return pObject->getPrimarySceneComponent()->getWorldPosition()._x; }

        /** @brief 노드 @p nodeCount 개, 노드마다 실수 슬롯 @p slotCount 개의 상태입니다. 슬롯 값은 원본 비트로 쓰이므로 크기가 노드 수에 비례합니다. */
        static GimmickCircuitState makeState( uint32 nodeCount, uint32 slotCount )
        {
            GimmickCircuitState state;
            state._stepIndex = 42;
            for ( uint32 node = 0; node < nodeCount; ++node )
            {
                GimmickNodeState nodeState;
                nodeState._id   = node + 1;
                nodeState._kind = 7;
                nodeState._listFloatState.assign( slotCount, 0.25f + static_cast<float32>( node ) );
                state._listNode.push_back( nodeState );
            }
            return state;
        }

        /** @brief 상태의 고정 배치형 바이트 수입니다. */
        static size_t measureFixedBytes( const GimmickCircuitState& state )
        {
            BitWriter writer;
            return FixedLayout::write( state, writer ) ? static_cast<size_t>( writer.getByteCount() ) : 0;
        }
    };
} // namespace

/**
 * @brief [GimmickStateTest] 씬의 회로 상태를 모아 바이트로 쓰고 다시 읽으면 같은 묶음이고, 같은 씬은 늘 같은 바이트이며, 손상된 바이트는 거절하고 묶음을 바꾸지 않는다
 */
SW_TEST_CASE( GimmickStateTest, BundleCollectsWritesAndRejectsCorruptBytes )
{
    GameObjectManager        manager;
    GimmickCircuitComponent* pCircuit = GimmickStateTestInternal::spawnPlatform( manager, "Platform" );
    SW_ASSERT_NOT_NULL( pCircuit );
    manager.beginPlay();
    test::updateFrames( manager, 90 );

    GimmickStateBundle bundle;
    bundle.collect( manager );
    SW_ASSERT_EQUAL( size_t( 1 ), bundle._listEntry.size() );
    SW_EXPECT_EQUAL( GimmickStateBundle::makeOwnerKey( hashed_string( "Platform" ) ), bundle._listEntry[0]._ownerKey );
    SW_EXPECT_TRUE( FixedLayout::equals( bundle._listEntry[0]._state, pCircuit->getState() ) );

    vector<uint8> bytes;
    SW_ASSERT_TRUE( bundle.write( bytes ) );
    vector<uint8>      again;
    GimmickStateBundle second;
    second.collect( manager );
    SW_ASSERT_TRUE( second.write( again ) );
    SW_EXPECT_TRUE( bytes == again ); // 같은 씬 · 같은 상태 — 같은 바이트

    GimmickStateBundle loaded;
    SW_ASSERT_TRUE( loaded.read( bytes ) == FixedReadResult::Ok );
    SW_EXPECT_TRUE( FixedLayout::equals( bundle, loaded ) );
    SW_EXPECT_EQUAL( bundle.computeHash(), loaded.computeHash() );
    SW_ASSERT_NOT_NULL( loaded.findEntry( GimmickStateBundle::makeOwnerKey( hashed_string( "Platform" ) ) ) );

    // 손상 — 다른 빌드의 스키마 해시, 바이트를 하나씩 줄인 모든 앞부분, 남는 바이트, 빈 입력. 어느 것도 이미 읽어 둔 묶음을 바꾸지 않는다.
    const uint64  hashBefore = loaded.computeHash();
    vector<uint8> otherBuild = bytes;
    otherBuild[0] ^= 0x5Au;
    SW_EXPECT_TRUE( loaded.read( otherBuild ) == FixedReadResult::SchemaMismatch );
    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        const vector<uint8> prefix( bytes.begin(), bytes.begin() + static_cast<ptrdiff_t>( length ) );
        SW_EXPECT_TRUE( loaded.read( prefix ) != FixedReadResult::Ok );
    }
    vector<uint8> trailing = bytes;
    trailing.push_back( 0xFF );
    SW_EXPECT_TRUE( loaded.read( trailing ) != FixedReadResult::Ok );
    SW_EXPECT_EQUAL( hashBefore, loaded.computeHash() );

    // 항목 수 상한(MaxCount)을 넘는 묶음은 쓰지 않는다.
    GimmickStateBundle tooMany;
    tooMany._listEntry.resize( GimmickStateBundle::kMaxEntryCount + 1 );
    vector<uint8> tooManyBytes;
    SW_EXPECT_FALSE( tooMany.write( tooManyBytes ) );
    SW_EXPECT_TRUE( tooManyBytes.empty() );
    manager.endPlay();
}

/**
 * @brief [GimmickStateTest] 롤백 — 어느 프레임의 묶음을 저장해 두었다가 더 진행한 뒤 적용하면 회로 상태와 액추에이터 자세가 그 프레임으로 돌아간다
 */
SW_TEST_CASE( GimmickStateTest, ApplyRewindsCircuitAndActuatorPose )
{
    GameObjectManager        manager;
    GimmickCircuitComponent* pCircuit = GimmickStateTestInternal::spawnPlatform( manager, "Platform" );
    SW_ASSERT_NOT_NULL( pCircuit );
    GameObject* pPlatform = pCircuit->getOwner();
    manager.beginPlay();
    test::updateFrames( manager, 30 );
    manager.flushSceneTransforms();
    const float32 savedX    = GimmickStateTestInternal::getX( pPlatform );
    const uint64  savedHash = pCircuit->getCircuit().computeStateHash();

    GimmickStateBundle saved;
    saved.collect( manager );
    vector<uint8> savedBytes;
    SW_ASSERT_TRUE( saved.write( savedBytes ) ); // 게임의 IRollbackGame::saveState 가 담는 바이트

    test::updateFrames( manager, 60 );
    manager.flushSceneTransforms();
    SW_EXPECT_NOT_EQUAL( savedHash, pCircuit->getCircuit().computeStateHash() );
    SW_EXPECT_TRUE( MathUtil::abs( GimmickStateTestInternal::getX( pPlatform ) - savedX ) > 1.0f );

    GimmickStateBundle restored; // loadState: 바이트 → 묶음 → 씬
    SW_ASSERT_TRUE( restored.read( savedBytes ) == FixedReadResult::Ok );
    uint32       skipped = 99;
    const uint32 applied = restored.apply( manager, &skipped );
    SW_EXPECT_EQUAL( 1u, applied );
    SW_EXPECT_EQUAL( 0u, skipped );
    SW_EXPECT_EQUAL( savedHash, pCircuit->getCircuit().computeStateHash() );
    manager.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( savedX, GimmickStateTestInternal::getX( pPlatform ), 0.05f );
    manager.endPlay();
}

/**
 * @brief [GimmickStateTest] 다른 기계 — 이름이 같은 오브젝트의 회로가 권위 쪽 묶음을 받아 같은 상태가 되고, 모르는 이름이나 구조가 다른 회로는 건너뛴 수로 센다
 */
SW_TEST_CASE( GimmickStateTest, ReplicaFollowsAuthorityByObjectName )
{
    GameObjectManager        authority;
    GimmickCircuitComponent* pAuthorityCircuit = GimmickStateTestInternal::spawnPlatform( authority, "Platform" );
    SW_ASSERT_NOT_NULL( pAuthorityCircuit );
    authority.beginPlay();
    test::updateFrames( authority, 75 );

    GameObjectManager        replica; // 오브젝트 id 는 다르지만 이름이 같다
    GimmickCircuitComponent* pReplicaCircuit = GimmickStateTestInternal::spawnPlatform( replica, "Platform" );
    SW_ASSERT_NOT_NULL( pReplicaCircuit );
    replica.beginPlay();
    SW_EXPECT_NOT_EQUAL( pAuthorityCircuit->getCircuit().computeStateHash(), pReplicaCircuit->getCircuit().computeStateHash() );

    GimmickStateBundle bundle;
    bundle.collect( authority );
    vector<uint8> wire;
    SW_ASSERT_TRUE( bundle.write( wire ) );
    GimmickStateBundle received;
    SW_ASSERT_TRUE( received.read( wire ) == FixedReadResult::Ok );
    uint32       skipped = 0;
    const uint32 applied = received.apply( replica, &skipped );
    SW_EXPECT_EQUAL( 1u, applied );
    SW_EXPECT_EQUAL( 0u, skipped );
    SW_EXPECT_EQUAL( pAuthorityCircuit->getCircuit().computeStateHash(), pReplicaCircuit->getCircuit().computeStateHash() );

    authority.flushSceneTransforms();
    replica.flushSceneTransforms();
    SW_EXPECT_NEAR_EQUAL( GimmickStateTestInternal::getX( pAuthorityCircuit->getOwner() ), GimmickStateTestInternal::getX( pReplicaCircuit->getOwner() ), 0.05f );

    // 이어서 같은 걸음을 진행하면 두 기계가 같은 길을 간다(결정적).
    test::updateFrames( authority, 30 );
    test::updateFrames( replica, 30 );
    SW_EXPECT_EQUAL( pAuthorityCircuit->getCircuit().computeStateHash(), pReplicaCircuit->getCircuit().computeStateHash() );

    // 모르는 이름의 항목과 노드 집합이 다른 회로의 항목은 적용하지 않고 센다(롤백 · 넷은 같은 회로만 받는다).
    GimmickStateBundle stranger = received;
    GimmickStateEntry  nobody;
    nobody._ownerKey = GimmickStateBundle::makeOwnerKey( hashed_string( "Nobody" ) );
    stranger._listEntry.push_back( nobody );
    GimmickNodeState extraNode = stranger._listEntry[0]._state._listNode[0];
    extraNode._id              = hashed_string( "extra" ).getHash();
    stranger._listEntry[0]._state._listNode.push_back( extraNode ); // 노드 집합이 달라 회로가 거절한다
    const uint64 replicaHash = pReplicaCircuit->getCircuit().computeStateHash();
    SW_EXPECT_EQUAL( 0u, stranger.apply( replica, &skipped ) );
    SW_EXPECT_EQUAL( 2u, skipped );
    SW_EXPECT_EQUAL( replicaHash, pReplicaCircuit->getCircuit().computeStateHash() );
    authority.endPlay();
    replica.endPlay();
}

/**
 * @brief [GimmickStateTest] 스냅샷 엔티티 — 상태가 255 바이트를 넘는 회로는 조각으로 나뉘어 싣고, 복원하면 같은 상태이며, 조각이 빠졌거나 다른 빌드의 바이트인 회로만 건너뛴다
 */
SW_TEST_CASE( GimmickStateTest, SnapshotChunksLargeCircuitsAndSkipsBrokenOnes )
{
    using Internal = GimmickStateTestInternal;
    GimmickStateBundle bundle;
    GimmickStateEntry  small;
    small._ownerKey = 11;
    small._state    = Internal::makeState( 1, 2 );
    GimmickStateEntry large;
    large._ownerKey = 22;
    large._state    = Internal::makeState( 4, GimmickNodeState::kMaxSlotCount ); // 노드마다 실수 32 개 — 500 바이트를 넘는다
    GimmickStateEntry huge;
    huge._ownerKey    = 33;
    huge._state       = Internal::makeState( 40, GimmickNodeState::kMaxSlotCount ); // 조각 상한(kMaxCircuitBytes)을 넘는다
    bundle._listEntry = { small, large, huge };

    const size_t smallBytes = Internal::measureFixedBytes( small._state );
    const size_t largeBytes = Internal::measureFixedBytes( large._state );
    SW_ASSERT_TRUE( 0 < smallBytes && smallBytes <= GimmickSnapshotUtil::kChunkPayloadBytes );
    SW_ASSERT_TRUE( largeBytes > 2u * GimmickSnapshotUtil::kChunkPayloadBytes && largeBytes <= GimmickSnapshotUtil::kMaxCircuitBytes );
    SW_ASSERT_TRUE( Internal::measureFixedBytes( huge._state ) > GimmickSnapshotUtil::kMaxCircuitBytes );
    const size_t largeChunks = ( largeBytes + GimmickSnapshotUtil::kChunkPayloadBytes - 1 ) / GimmickSnapshotUtil::kChunkPayloadBytes;

    constexpr uint32 kFirstEntityID = 5000;
    constexpr uint32 kTypeID        = 77;
    NetSnapshot      snapshot;
    snapshot._tick = 9;
    snapshot._listEntity.push_back( NetEntityState{
        vector<uint8>{ 1, 2, 3 },
        3, 1
    } ); // 다른 엔티티는 섞여도 된다
    const uint32 notSent = GimmickSnapshotUtil::appendEntities( snapshot, bundle, kFirstEntityID, kTypeID );
    SW_EXPECT_EQUAL( 1u, notSent );                                                // huge
    SW_EXPECT_EQUAL( size_t( 1 + 1 ) + largeChunks, snapshot._listEntity.size() ); // 다른 엔티티 + small 1 조각 + large 조각
    for ( const NetEntityState& entity : snapshot._listEntity )
    {
        SW_EXPECT_TRUE( entity._buffer.size() <= static_cast<size_t>( NetSnapshot::kMaxEntityBytes ) );
    }
    SW_EXPECT_TRUE( snapshot.findEntity( kFirstEntityID + 1 * GimmickSnapshotUtil::kMaxChunksPerCircuit + static_cast<uint32>( largeChunks ) - 1 ) != nullptr );

    // 델타 직렬화를 거쳐도 같다.
    BitWriter   writer;
    NetSnapshot written;
    snapshot.writeDelta( writer, nullptr, 4000, written );
    BitReader   reader( writer.getBytes().data(), writer.getByteCount() );
    NetSnapshot decoded;
    SW_ASSERT_TRUE( NetSnapshot::readDelta( reader, nullptr, decoded ) );

    GimmickStateBundle extracted;
    SW_EXPECT_EQUAL( 0u, GimmickSnapshotUtil::extractBundle( decoded, kFirstEntityID, kTypeID, extracted ) );
    SW_ASSERT_EQUAL( size_t( 2 ), extracted._listEntry.size() );
    SW_EXPECT_TRUE( extracted._listEntry[0]._ownerKey == 11 && FixedLayout::equals( extracted._listEntry[0]._state, small._state ) );
    SW_EXPECT_TRUE( extracted._listEntry[1]._ownerKey == 22 && FixedLayout::equals( extracted._listEntry[1]._state, large._state ) );

    // 종류 번호가 다르면 읽지 않는다.
    GimmickStateBundle otherType;
    SW_EXPECT_EQUAL( 0u, GimmickSnapshotUtil::extractBundle( decoded, kFirstEntityID, kTypeID + 1, otherType ) );
    SW_EXPECT_TRUE( otherType._listEntry.empty() );

    // large 의 가운데 조각이 빠지면 그 회로만 건너뛴다(델타가 아직 못 실은 조각이다).
    NetSnapshot broken = decoded;
    for ( auto entityIt = broken._listEntity.begin(); entityIt != broken._listEntity.end(); ++entityIt )
    {
        if ( entityIt->_entityID == kFirstEntityID + 1 * GimmickSnapshotUtil::kMaxChunksPerCircuit + 1 )
        {
            broken._listEntity.erase( entityIt );
            break;
        }
    }
    GimmickStateBundle partial;
    SW_EXPECT_EQUAL( 1u, GimmickSnapshotUtil::extractBundle( broken, kFirstEntityID, kTypeID, partial ) );
    SW_ASSERT_EQUAL( size_t( 1 ), partial._listEntry.size() );
    SW_EXPECT_TRUE( partial._listEntry[0]._ownerKey == 11 );

    // 다른 빌드가 쓴 조각(스키마 해시가 다름)은 그 회로만 건너뛴다.
    NetSnapshot otherBuild = decoded;
    for ( NetEntityState& entity : otherBuild._listEntity )
    {
        if ( entity._entityID == kFirstEntityID )
            entity._buffer[GimmickSnapshotUtil::kChunkHeaderBytes] ^= 0x5Au; // small 의 첫 조각, 해시 첫 바이트
    }
    GimmickStateBundle mixed;
    SW_EXPECT_EQUAL( 1u, GimmickSnapshotUtil::extractBundle( otherBuild, kFirstEntityID, kTypeID, mixed ) );
    SW_ASSERT_EQUAL( size_t( 1 ), mixed._listEntry.size() );
    SW_EXPECT_TRUE( mixed._listEntry[0]._ownerKey == 22 );
}
