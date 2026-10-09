#include "pch.h"

#include "Engine/Destruction/DestructionDamage.h"
#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Destruction/DestructionState.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/MeshFracture.h"

#include "EngineTest/DestructionTestUtil.h"

#include "TestFramework/TestFramework.h"

// DestructionSnapshotTest — 구조 상태 스냅숏(늦은 참가 · 어긋남 바로잡기): 읽은 쪽의 해시가 같고, 뒤따르는 사건도 같은 결과를 내며, 다른 그래프 · 잘린 바이트는 거절한다.

namespace
{
    struct TestDestructionSnapshotInternal
    {
        /** @brief 4 × 3 × 0.3 벽을 벽돌 8 × 6 으로 쪼개고 묶음 레벨 [3, 12] 를 짓습니다. */
        static bool makeWall( sw::FractureAsset& outAsset, uint32 columnCount )
        {
            const sw::vector<sw::RHIVertex> listBox = test::DestructionTestUtil::makeBox( sw::float3{ 2.0f, 1.5f, 0.15f }, sw::float3{ 0.0f, 1.5f, 0.0f } );
            sw::FractureSettings            settings;
            settings._pattern          = sw::FracturePattern::Slices;
            settings._arrSliceCount[0] = columnCount;
            settings._arrSliceCount[1] = 6;
            settings._arrSliceCount[2] = 1;
            settings._sliceJitter      = 0.0f;
            settings._listLevelCount   = { 3, 12 };
            sw::string error;
            return sw::MeshFractureUtil::fracture( listBox, settings, outAsset, error );
        }

        static sw::vector<uint8> makeBottomAnchors( const sw::FractureAsset& asset )
        {
            sw::vector<uint8> listAnchor( asset.getPieceCount(), 0 );
            for ( uint32 leaf = 0; leaf < asset.getPieceCount(); ++leaf )
            {
                listAnchor[leaf] = asset._listPiece[leaf]._boundsMin._y < 0.01f ? 1 : 0;
            }
            return listAnchor;
        }

        static sw::DestructionProfile makeProfile()
        {
            sw::DestructionProfile profile;
            profile._listStrainThreshold = { 50.0f, 80.0f, 30.0f };
            profile._linkStrength        = 100.0f;
            profile._supportStrength     = 1.0e9f;
            return profile;
        }

        static sw::DestructionDamageEvent makeHit( const sw::float3& position, float32 strain, float32 radius, sw::DestructionDamageKind kind )
        {
            sw::DestructionDamageEvent event;
            event._position = position;
            event._strain   = strain;
            event._radius   = radius;
            event._kind     = kind;
            event._impulse  = 50.0f;
            return event;
        }
    };
} // namespace

/**
 * @brief [DestructionSnapshotTest] 사건 둘을 겪은 상태의 스냅숏을 새 상태가 읽으면 해시 · 그룹 · 사건 수가 같고, 세 번째 사건을 둘에 주면 다시 같다
 */
SW_TEST_CASE( DestructionSnapshotTest, SnapshotCarriesStructureStrainAndGroups )
{
    using Internal = TestDestructionSnapshotInternal;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( Internal::makeWall( asset, 8 ) );
    const sw::vector<uint8> anchors = Internal::makeBottomAnchors( asset );
    sw::DestructionState    server;
    server.initialize( asset._graph, Internal::makeProfile(), anchors );
    sw::DestructionChange change;
    // 약한 폭발(변형만 쌓인다) + 센 맞음(떨어져 나간다) — 스냅숏은 끊김과 쌓인 변형을 모두 실어야 한다.
    (void)server.applyDamage( Internal::makeHit( sw::float3{ 1.0f, 1.0f, 0.0f }, 30.0f, 0.8f, sw::DestructionDamageKind::Radial ), change );
    // 바뀜 여부는 보지 않는다 — 아래 단언이 그룹 수를 본다
    (void)server.applyDamage( Internal::makeHit( sw::float3{ -1.0f, 2.0f, 0.0f }, 300.0f, 1.0f, sw::DestructionDamageKind::Radial ), change );
    SW_ASSERT_TRUE( server.getGroups().size() > 1 );

    sw::vector<uint8> bytes;
    server.writeSnapshot( bytes );
    SW_EXPECT_TRUE( bytes.size() < 2048u );
    sw::DestructionState client;
    client.initialize( asset._graph, Internal::makeProfile(), anchors );
    SW_ASSERT_TRUE( client.readSnapshot( bytes.data(), bytes.size() ) );
    SW_EXPECT_EQUAL( server.computeStateHash(), client.computeStateHash() );
    SW_EXPECT_EQUAL( server.getEventCount(), client.getEventCount() );
    SW_ASSERT_EQUAL( server.getGroups().size(), client.getGroups().size() );
    for ( uint32 leaf = 0; leaf < asset.getPieceCount(); ++leaf )
    {
        SW_EXPECT_EQUAL( server.getGroupOfLeaf( leaf ), client.getGroupOfLeaf( leaf ) );
        SW_EXPECT_EQUAL( server.getActiveNodeOfLeaf( leaf ), client.getActiveNodeOfLeaf( leaf ) );
    }

    // 뒤따르는 사건 — 쌓인 변형 · 다음 그룹 번호까지 넘어갔으므로 같은 결과다.
    const sw::DestructionDamageEvent next = Internal::makeHit( sw::float3{ 1.0f, 1.0f, 0.0f }, 30.0f, 0.8f, sw::DestructionDamageKind::Radial );
    sw::DestructionChange            serverChange;
    sw::DestructionChange            clientChange;
    (void)server.applyDamage( next, serverChange ); // 바뀜 여부는 보지 않는다 — 아래 상태 해시로 비교한다
    (void)client.applyDamage( next, clientChange ); // 바뀜 여부는 보지 않는다 — 아래 상태 해시로 비교한다
    SW_EXPECT_EQUAL( server.computeStateHash(), client.computeStateHash() );
    SW_EXPECT_TRUE( serverChange._listCreatedGroup == clientChange._listCreatedGroup );
}

/**
 * @brief [DestructionSnapshotTest] 다른 그래프의 스냅숏 · 잘린 바이트 · 잘못된 매직은 거절하고 상태를 그대로 둔다
 */
SW_TEST_CASE( DestructionSnapshotTest, ForeignOrTruncatedSnapshotIsRejected )
{
    using Internal = TestDestructionSnapshotInternal;
    sw::FractureAsset wall;
    sw::FractureAsset other;
    SW_ASSERT_TRUE( Internal::makeWall( wall, 8 ) );
    SW_ASSERT_TRUE( Internal::makeWall( other, 5 ) );
    sw::DestructionState source;
    source.initialize( wall._graph, Internal::makeProfile(), Internal::makeBottomAnchors( wall ) );
    sw::DestructionChange change;
    // 바뀜 여부는 보지 않는다 — 스냅숏 비교가 결과를 본다
    (void)source.applyDamage( Internal::makeHit( sw::float3{ 0.0f, 1.5f, 0.0f }, 300.0f, 1.0f, sw::DestructionDamageKind::Radial ), change );
    sw::vector<uint8> bytes;
    source.writeSnapshot( bytes );

    sw::DestructionState foreign;
    foreign.initialize( other._graph, Internal::makeProfile(), Internal::makeBottomAnchors( other ) );
    const uint64 foreignHash = foreign.computeStateHash();
    SW_EXPECT_FALSE( foreign.readSnapshot( bytes.data(), bytes.size() ) );
    SW_EXPECT_EQUAL( foreignHash, foreign.computeStateHash() );

    sw::DestructionState target;
    target.initialize( wall._graph, Internal::makeProfile(), Internal::makeBottomAnchors( wall ) );
    const uint64 targetHash = target.computeStateHash();
    SW_EXPECT_FALSE( target.readSnapshot( bytes.data(), bytes.size() / 2 ) );
    SW_EXPECT_EQUAL( targetHash, target.computeStateHash() );
    sw::vector<uint8> wrongMagic = bytes;
    wrongMagic[0] ^= 0xFF;
    SW_EXPECT_FALSE( target.readSnapshot( wrongMagic.data(), wrongMagic.size() ) );
    SW_EXPECT_TRUE( target.readSnapshot( bytes.data(), bytes.size() ) );
    SW_EXPECT_EQUAL( source.computeStateHash(), target.computeStateHash() );
}
