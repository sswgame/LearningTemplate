#include "pch.h"

#include "Engine/Destruction/DestructionDamage.h"
#include "Engine/Destruction/DestructionProfile.h"
#include "Engine/Destruction/DestructionState.h"
#include "Engine/Destruction/FractureAsset.h"
#include "Engine/Destruction/MeshFracture.h"

#include "EngineTest/DestructionTestUtil.h"

#include "TestFramework/TestFramework.h"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

// DestructionDamageTest — 변형 문턱(깊이별) · 넘친 몫이 아래 레벨로 · 연결 세기 · 부딪힘 충격량 · 폭발 감쇠, 같은 사건열 = 같은 상태(네트워크 동기화).

namespace
{
    struct TestDestructionDamageInternal
    {
        /** @brief 4 × 3 × 0.3 벽(가운데 바닥이 y = 0)을 벽돌 8 × 6 으로 쪼개고 묶음 레벨 [3, 12] 를 짓습니다. */
        static bool makeWall( sw::FractureAsset& outAsset )
        {
            const sw::vector<sw::RHIVertex> listBox = test::DestructionTestUtil::makeBox( sw::float3{ 2.0f, 1.5f, 0.15f }, sw::float3{ 0.0f, 1.5f, 0.0f } );
            sw::FractureSettings            settings;
            settings._pattern          = sw::FracturePattern::Slices;
            settings._arrSliceCount[0] = 8;
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
                listAnchor[leaf] = asset._listPiece[leaf]._boundsMin._y < 0.01f ? 1 : 0;
            return listAnchor;
        }

        static sw::DestructionProfile makeProfile()
        {
            sw::DestructionProfile profile;
            profile._listStrainThreshold = { 50.0f, 80.0f, 30.0f };
            profile._linkStrength        = 100.0f; // 벽돌 단면 0.5 × 0.3 = 0.15 → 15
            profile._supportStrength     = 1.0e9f;
            return profile;
        }

        static uint32 countActiveNodes( const sw::DestructionState& state )
        {
            uint32 count = 0;
            for ( const sw::DestructionGroup& group : state.getGroups() )
                count += static_cast<uint32>( group._listNode.size() );
            return count;
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
 * @brief [DestructionDamageTest] 약한 피해는 쌓이기만 하고, 쌓여 뿌리 문턱을 넘으면 큰 덩어리(묶음)로만 갈라지고, 더 맞으면 그 안의 조각이 떨어진다
 */
SW_TEST_CASE( DestructionDamageTest, StrainAccumulatesAndOpensLevelsProgressively )
{
    using Internal = TestDestructionDamageInternal;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( Internal::makeWall( asset ) );
    SW_ASSERT_EQUAL( 48u, asset.getPieceCount() );
    sw::DestructionState state;
    state.initialize( asset._graph, Internal::makeProfile(), Internal::makeBottomAnchors( asset ) );
    const sw::float3 hitPoint{ 1.25f, 2.25f, 0.0f };

    sw::DestructionChange change;
    SW_EXPECT_FALSE( state.applyDamage( Internal::makeHit( hitPoint, 30.0f, 0.0f, sw::DestructionDamageKind::Point ), change ) );
    SW_EXPECT_NEAR_EQUAL( 30.0f, state.getNodeStrain( asset._graph.getRootNode() ), 1e-4f );
    SW_EXPECT_EQUAL( 1u, Internal::countActiveNodes( state ) );

    // 쌓여 60 — 뿌리(50)가 갈라지고 넘친 10 은 묶음 문턱(80)에 못 미친다. 떨어진 것은 없다.
    SW_ASSERT_TRUE( state.applyDamage( Internal::makeHit( hitPoint, 30.0f, 0.0f, sw::DestructionDamageKind::Point ), change ) );
    SW_EXPECT_FALSE( change.hasGroupChange() );
    const uint32 afterRoot = Internal::countActiveNodes( state );
    SW_EXPECT_TRUE( afterRoot >= 3 );

    // 센 피해 한 번 — 묶음 · 그 아래 묶음이 갈라지고 맞은 잎이 떨어져 나간다.
    change.clear();
    SW_ASSERT_TRUE( state.applyDamage( Internal::makeHit( hitPoint, 400.0f, 0.0f, sw::DestructionDamageKind::Point ), change ) );
    SW_EXPECT_TRUE( Internal::countActiveNodes( state ) > afterRoot );
    SW_EXPECT_TRUE( change.hasGroupChange() );
    uint32 freeLeafCount = 0;
    for ( const sw::DestructionGroup& group : state.getGroups() )
        freeLeafCount += group._bAnchored == SW_FALSE ? group._leafCount : 0u;
    SW_EXPECT_TRUE( freeLeafCount >= 1 );
}

/**
 * @brief [DestructionDamageTest] 폭발은 반경 안을 거리 감쇠로 깨 가까운 조각만 떼고, 먼 곳은 변형 없이 앵커에 붙은 채다
 */
SW_TEST_CASE( DestructionDamageTest, ExplosionShattersNearPiecesAndKeepsFarOnesAnchored )
{
    using Internal = TestDestructionDamageInternal;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( Internal::makeWall( asset ) );
    sw::DestructionState state;
    state.initialize( asset._graph, Internal::makeProfile(), Internal::makeBottomAnchors( asset ) );
    // 아래 가운데를 날려 그 위가 받침을 잃게 한다 — 가운데 두 열 아래 셋 행.
    sw::DestructionChange change;
    SW_ASSERT_TRUE( state.applyDamage( Internal::makeHit( sw::float3{ 0.0f, 0.75f, 0.0f }, 500.0f, 1.1f, sw::DestructionDamageKind::Radial ), change ) );
    SW_EXPECT_TRUE( change._brokenLinkCount > 0 );
    SW_EXPECT_TRUE( change._listCreatedGroup.size() >= 2 );
    // 왼쪽 끝 아래 벽돌은 맞지 않았고 앵커에 붙어 있다.
    uint32 cornerLeaf = 0;
    for ( uint32 leaf = 0; leaf < asset.getPieceCount(); ++leaf )
    {
        if ( asset._listPiece[leaf]._boundsMin._x < -1.99f && asset._listPiece[leaf]._boundsMin._y < 0.01f )
            cornerLeaf = leaf;
    }
    const sw::DestructionGroup* pCorner = state.findGroup( state.getGroupOfLeaf( cornerLeaf ) );
    SW_ASSERT_NOT_NULL( pCorner );
    SW_EXPECT_TRUE( pCorner->_bAnchored == SW_TRUE );
    SW_EXPECT_NEAR_EQUAL( 0.0f, state.getNodeStrain( cornerLeaf ), 0.0f );
}

/**
 * @brief [DestructionDamageTest] 부딪힘 충격량은 문턱 아래면 피해가 아니고, 넘은 몫 × 배율이 변형이다
 */
SW_TEST_CASE( DestructionDamageTest, ImpactImpulseBecomesStrainAboveTheMinimum )
{
    sw::DestructionProfile profile;
    profile._minImpulse                   = 40.0f;
    profile._impulseToStrain              = 0.5f;
    profile._impactRadius                 = 0.3f;
    const sw::DestructionDamageEvent weak = sw::DestructionDamageUtil::makeImpactEvent( profile, sw::float3{}, sw::float3{ 0.0f, 1.0f, 0.0f }, 30.0f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, weak._strain, 0.0f );
    const sw::DestructionDamageEvent strong = sw::DestructionDamageUtil::makeImpactEvent( profile, sw::float3{}, sw::float3{ 0.0f, 1.0f, 0.0f }, 140.0f );
    SW_EXPECT_NEAR_EQUAL( 50.0f, strong._strain, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, strong._radius, 0.0f );
    SW_EXPECT_TRUE( strong._kind == sw::DestructionDamageKind::Impact );
}

/**
 * @brief [DestructionDamageTest] 씨앗 + 사건열(바이트로 보낸 것)을 다른 상태에 같은 순서로 적용하면 상태 해시가 같다 — 변환을 보내지 않는다
 */
SW_TEST_CASE( DestructionDamageTest, SameEventLogGivesTheSameStateOnAnotherMachine )
{
    using Internal = TestDestructionDamageInternal;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( Internal::makeWall( asset ) );
    const sw::vector<uint8> anchors = Internal::makeBottomAnchors( asset );
    sw::DestructionState    server;
    server.initialize( asset._graph, Internal::makeProfile(), anchors );
    sw::DestructionEventLog log;
    log._seed = 77;
    log._listEvent.push_back( Internal::makeHit( sw::float3{ 1.0f, 1.0f, 0.0f }, 90.0f, 0.8f, sw::DestructionDamageKind::Radial ) );
    log._listEvent.push_back( Internal::makeHit( sw::float3{ -1.0f, 2.0f, 0.0f }, 200.0f, 0.0f, sw::DestructionDamageKind::Point ) );
    log._listEvent.back()._leafHint = 5;
    log._listEvent.push_back( Internal::makeHit( sw::float3{ 0.0f, 0.5f, 0.0f }, 300.0f, 1.2f, sw::DestructionDamageKind::Radial ) );
    sw::DestructionChange change;
    for ( const sw::DestructionDamageEvent& event : log._listEvent )
        (void)server.applyDamage( event, change );

    sw::vector<uint8> bytes;
    log.makeBytes( bytes );
    sw::DestructionEventLog received;
    SW_ASSERT_TRUE( received.readFromBytes( bytes.data(), bytes.size() ) );
    SW_EXPECT_EQUAL( 77ull, received._seed );
    SW_ASSERT_EQUAL( size_t( 3 ), received._listEvent.size() );
    sw::DestructionState client;
    client.initialize( asset._graph, Internal::makeProfile(), anchors );
    for ( const sw::DestructionDamageEvent& event : received._listEvent )
        (void)client.applyDamage( event, change );
    SW_EXPECT_EQUAL( server.computeStateHash(), client.computeStateHash() );
    SW_EXPECT_EQUAL( 3u, client.getEventCount() );
    SW_EXPECT_TRUE( server.getGroups().size() > 1 );

    // 순서를 바꾸면 다른 상태다(사건열의 순서가 계약의 일부).
    sw::DestructionState reordered;
    reordered.initialize( asset._graph, Internal::makeProfile(), anchors );
    for ( size_t index = received._listEvent.size(); index > 0; --index )
        (void)reordered.applyDamage( received._listEvent[index - 1], change );
    SW_EXPECT_NOT_EQUAL( server.computeStateHash(), reordered.computeStateHash() );

    test::ScopedDefensiveTestLog expected( "a truncated or foreign event log is rejected" );
    SW_EXPECT_FALSE( received.readFromBytes( bytes.data(), bytes.size() - 1 ) );
    bytes[20] = 9; // 모르는 종류
    SW_EXPECT_FALSE( received.readFromBytes( bytes.data(), bytes.size() ) );
}

/**
 * @brief [DestructionDamageTest] 같은 벽 · 사건열의 상태 해시는 기록해 둔 값이다 — Windows(clang-cl) · 리눅스(clang) · Debug · Shipping 이 모두 같은 수를 내야 한다
 * @details 파괴 네트워킹은 변환이 아니라 씨앗과 사건을 보내고 받는 쪽이 같은 계산을 한다. 한 프로세스 안의 두 상태가 같은 것(앞 케이스)으로는 컴파일러 ·
 *          최적화(FMA 축약 · 벡터화)가 계산을 바꾸는 것을 못 본다. 기준값이 바뀌어야 하는 변경(파쇄 · 피해 계산을 일부러 바꿈)이면 새 값을 적고 커밋
 *          메시지에 이유를 적는다. 새 값은 환경 변수 `SW_DESTRUCTION_PRINT_GOLDEN=1` 로 돌려 출력에서 읽는다.
 */
SW_TEST_CASE( DestructionDamageTest, EventLogHashMatchesTheRecordedValueOnEveryBuild )
{
    using Internal = TestDestructionDamageInternal;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( Internal::makeWall( asset ) );
    const sw::vector<uint8> anchors = Internal::makeBottomAnchors( asset );
    sw::DestructionState    state;
    state.initialize( asset._graph, Internal::makeProfile(), anchors );
    sw::DestructionChange change;
    sw::vector<uint64>    listHash;
    listHash.push_back( state.computeStateHash() ); // 파쇄 결과(사건 전)
    sw::DestructionDamageEvent arrEvent[] = {
        Internal::makeHit( sw::float3{ 1.0f, 1.0f, 0.0f }, 90.0f, 0.8f, sw::DestructionDamageKind::Radial ),
        Internal::makeHit( sw::float3{ -1.0f, 2.0f, 0.0f }, 200.0f, 0.0f, sw::DestructionDamageKind::Point ),
        Internal::makeHit( sw::float3{ 0.0f, 0.5f, 0.0f }, 300.0f, 1.2f, sw::DestructionDamageKind::Radial ),
        Internal::makeHit( sw::float3{ 0.7f, 2.6f, 0.0f }, 150.0f, 0.6f, sw::DestructionDamageKind::Radial ),
    };
    arrEvent[1]._leafHint = 5;
    for ( const sw::DestructionDamageEvent& event : arrEvent )
    {
        (void)state.applyDamage( event, change );
        listHash.push_back( state.computeStateHash() );
    }
    const utf8* pPrint = std::getenv( "SW_DESTRUCTION_PRINT_GOLDEN" );
    if ( pPrint != nullptr && pPrint[0] == '1' )
    {
        for ( size_t index = 0; index < listHash.size(); ++index )
            std::fprintf( stdout, "golden[%zu] = 0x%016" PRIX64 "ull\n", index, listHash[index] );
    }
    // Windows Debug 에서 뜬 값 — Windows Shipping 에서도 같다(적용 때 확인). 리눅스는 CI 의 리눅스 잡이 지킨다.
    constexpr uint64 kArrGolden[] = { 0xCB9A665FA74275FEull, 0xDACF675D07084B52ull, 0x349FA0F7E0A77667ull, 0xC2948E404FB7B710ull, 0x417A0BF6BAD099C0ull };
    SW_ASSERT_EQUAL( sizeof( kArrGolden ) / sizeof( kArrGolden[0] ), listHash.size() );
    for ( size_t index = 0; index < listHash.size(); ++index )
        SW_EXPECT_TRUE_MSG( kArrGolden[index] == listHash[index], "destruction hash differs from the recorded value - a compiler or build setting changed the arithmetic" );
}

/**
 * @brief [DestructionDamageTest] 조각이 깨지지 않을 만큼(잎 문턱이 높다)이라도 맞닿은 면의 세기를 넘는 변형이면 연결이 끊겨 모서리 벽돌이 떨어진다
 */
SW_TEST_CASE( DestructionDamageTest, LinkStrainSeparatesPiecesThatDoNotShatter )
{
    using Internal = TestDestructionDamageInternal;
    sw::FractureAsset asset;
    SW_ASSERT_TRUE( Internal::makeWall( asset ) );
    sw::DestructionProfile profile = Internal::makeProfile();
    profile._listStrainThreshold   = { 1.0f, 1.0f, 1.0f, 1.0e6f }; // 뿌리 · 두 묶음 레벨은 쉽게 갈라지고 잎(깊이 3)은 깨지지 않는다
    sw::DestructionState state;
    state.initialize( asset._graph, profile, Internal::makeBottomAnchors( asset ) );
    uint32 cornerLeaf = 0;
    for ( uint32 leaf = 0; leaf < asset.getPieceCount(); ++leaf )
    {
        if ( asset._listPiece[leaf]._boundsMax._x > 1.99f && asset._listPiece[leaf]._boundsMax._y > 2.99f )
            cornerLeaf = leaf;
    }
    // 모서리 벽돌 중심 40, 이웃 중심(0.5 m) 은 6.7 — 연결 평균 23 > 넓이 0.15 × 세기 100 = 15.
    sw::DestructionChange change;
    SW_ASSERT_TRUE( state.applyDamage( Internal::makeHit( asset._graph._listNode[cornerLeaf]._centroid, 40.0f, 0.6f, sw::DestructionDamageKind::Radial ), change ) );
    SW_EXPECT_FALSE( state.isNodeBroken( cornerLeaf ) ); // 깨지지 않았다
    SW_EXPECT_TRUE( change._brokenLinkCount >= 2 );
    const sw::DestructionGroup* pCorner = state.findGroup( state.getGroupOfLeaf( cornerLeaf ) );
    SW_ASSERT_NOT_NULL( pCorner );
    SW_EXPECT_TRUE( pCorner->_bAnchored == SW_FALSE );
    SW_EXPECT_EQUAL( 1u, pCorner->_leafCount );
}
