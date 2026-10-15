// 기믹 회로 상태 묶음 — 롤백 저장 · 불러오기, 다른 기계(같은 이름의 오브젝트)로 따라가기, 바이트 손상 거절, 네트워크 스냅샷 엔티티(조각) 왕복.
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/BitStream.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Gimmick/GimmickCircuitComponent.h"
#include "GameFramework/Base/Gameplay/Gimmick/GimmickStateBundle.h"
#include "GameFramework/Base/World/Spline/SplineComponent.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/GimmickSnapshotUtil.h"
#include "GameFramework/Kits/Feature/Network/NetClientServer/NetSnapshot.h"

#include "TestFramework/TestFramework.h"
#include "TestFramework/TestTick.h"

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
    SW_EXPECT_TRUE( bundle._listEntry[0]._bytes == pCircuit->getStateBytes() );

    vector<uint8> bytes;
    bundle.write( bytes );
    vector<uint8>      again;
    GimmickStateBundle second;
    second.collect( manager );
    second.write( again );
    SW_EXPECT_TRUE( bytes == again ); // 같은 씬 · 같은 상태 — 같은 바이트

    GimmickStateBundle loaded;
    SW_ASSERT_TRUE( loaded.read( bytes ) );
    SW_EXPECT_EQUAL( bundle.computeHash(), loaded.computeHash() );
    SW_ASSERT_NOT_NULL( loaded.findEntry( GimmickStateBundle::makeOwnerKey( hashed_string( "Platform" ) ) ) );

    // 손상 — 머리 · 잘림 · 남는 바이트 · 길이 거짓. 어느 것도 이미 읽어 둔 묶음을 바꾸지 않는다.
    const uint64  hashBefore = loaded.computeHash();
    vector<uint8> wrongMagic = bytes;
    wrongMagic[0] ^= 0x5Au;
    vector<uint8> truncated( bytes.begin(), bytes.end() - 3 );
    vector<uint8> trailing = bytes;
    trailing.push_back( 0 );
    vector<uint8> hugeLength = bytes;
    hugeLength[12 + 8]       = 0xFF; // 첫 항목 길이의 하위 바이트를 키운다
    hugeLength[12 + 9]       = 0xFF;
    SW_EXPECT_FALSE( loaded.read( wrongMagic ) );
    SW_EXPECT_FALSE( loaded.read( truncated ) );
    SW_EXPECT_FALSE( loaded.read( trailing ) );
    SW_EXPECT_FALSE( loaded.read( hugeLength ) );
    SW_EXPECT_FALSE( loaded.read( vector<uint8>{} ) );
    SW_EXPECT_EQUAL( hashBefore, loaded.computeHash() );
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
    saved.write( savedBytes ); // 게임의 IRollbackGame::saveState 가 담는 바이트

    test::updateFrames( manager, 60 );
    manager.flushSceneTransforms();
    SW_EXPECT_NOT_EQUAL( savedHash, pCircuit->getCircuit().computeStateHash() );
    SW_EXPECT_TRUE( MathUtil::abs( GimmickStateTestInternal::getX( pPlatform ) - savedX ) > 1.0f );

    GimmickStateBundle restored; // loadState: 바이트 → 묶음 → 씬
    SW_ASSERT_TRUE( restored.read( savedBytes ) );
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
    bundle.write( wire );
    GimmickStateBundle received;
    SW_ASSERT_TRUE( received.read( wire ) );
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

    // 모르는 이름의 항목과 구조가 다른 회로의 항목은 적용하지 않고 센다.
    GimmickStateBundle stranger = received;
    stranger._listEntry.push_back( GimmickStateEntry{
        vector<uint8>{ 1, 2, 3 },
        GimmickStateBundle::makeOwnerKey( hashed_string( "Nobody" ) )
    } );
    stranger._listEntry[0]._bytes.push_back( 0 ); // 크기가 달라 회로가 거절한다
    SW_EXPECT_EQUAL( 0u, stranger.apply( replica, &skipped ) );
    SW_EXPECT_EQUAL( 2u, skipped );
    authority.endPlay();
    replica.endPlay();
}

/**
 * @brief [GimmickStateTest] 스냅샷 엔티티 — 상태가 255 바이트를 넘는 회로는 조각으로 나뉘어 싣고, 되살리면 같은 바이트이며, 조각이 빠진 회로만 건너뛴다
 */
SW_TEST_CASE( GimmickStateTest, SnapshotChunksLargeCircuitsAndSkipsBrokenOnes )
{
    GimmickStateBundle bundle;
    GimmickStateEntry  small;
    small._ownerKey = 11;
    small._bytes.assign( 40, 0x21 );
    GimmickStateEntry large;
    large._ownerKey = 22;
    for ( uint32 index = 0; index < 600; ++index )
    {
        large._bytes.push_back( static_cast<uint8>( index * 7u ) );
    }
    GimmickStateEntry huge;
    huge._ownerKey = 33;
    huge._bytes.assign( GimmickSnapshotUtil::kMaxCircuitBytes + 1, 0x01 );
    bundle._listEntry = { small, large, huge };

    constexpr uint32 kFirstEntityID = 5000;
    constexpr uint32 kTypeID        = 77;
    NetSnapshot      snapshot;
    snapshot._tick = 9;
    snapshot._listEntity.push_back( NetEntityState{
        vector<uint8>{ 1, 2, 3 },
        3, 1
    } ); // 다른 엔티티는 섞여도 된다
    const uint32 notSent = GimmickSnapshotUtil::appendEntities( snapshot, bundle, kFirstEntityID, kTypeID );
    SW_EXPECT_EQUAL( 1u, notSent );                                      // huge
    SW_EXPECT_EQUAL( size_t( 1 + 1 + 3 ), snapshot._listEntity.size() ); // 다른 엔티티 + small 1 조각 + large 3 조각
    for ( const NetEntityState& entity : snapshot._listEntity )
    {
        SW_EXPECT_TRUE( entity._buffer.size() <= static_cast<size_t>( NetSnapshot::kMaxEntityBytes ) );
    }
    SW_EXPECT_TRUE( snapshot.findEntity( kFirstEntityID + 1 * GimmickSnapshotUtil::kMaxChunksPerCircuit + 2 ) != nullptr ); // 회로 1 (large) 의 셋째 조각

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
    SW_EXPECT_TRUE( extracted._listEntry[0]._ownerKey == 11 && extracted._listEntry[0]._bytes == small._bytes );
    SW_EXPECT_TRUE( extracted._listEntry[1]._ownerKey == 22 && extracted._listEntry[1]._bytes == large._bytes );

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
}
