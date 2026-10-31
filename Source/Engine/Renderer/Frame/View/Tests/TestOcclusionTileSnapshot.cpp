#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Frame/View/OcclusionTileSnapshot.h"

using namespace sw;

// 가림 타일 사본 — 구가 가려졌는지 묻는 식(gpu_cull.hlsl 의 isOccluded 와 같다)과 우편함.

namespace
{
    constexpr uint32  kTileSize = 16;
    constexpr uint32  kColumns  = 80;   // 1280 / 16
    constexpr uint32  kRows     = 45;   // 720 / 16
    constexpr float32 kNear     = 0.0f; // 카메라 바로 앞의 벽 — 화면 안의 무엇이든 가린다
    constexpr float32 kFar      = 1.0f; // 아무것도 가리지 않는다

    /** @brief 원점에서 +Z 를 보는 시야각 0.7 rad 카메라의 사본입니다. 모든 타일의 최대 깊이를 @p tileDepth 로 채운다. */
    OcclusionTileSnapshot makeSnapshot( float32 tileDepth )
    {
        OcclusionTileSnapshot snapshot;
        const float32         scale     = 1.0f / MathUtil::tan( 0.35f );
        const float32         farPlane  = 1000.0f;
        const float32         nearPlane = 0.1f;
        snapshot._viewProj              = float4x4{}; // 단위 행렬에서 시작
        // 행 벡터 규약의 원근 투영(뷰는 단위): x' = x * scale / aspect, y' = y * scale, z' = (z - near) * far / (far - near), w' = z.
        snapshot._viewProj._11 = scale / ( 16.0f / 9.0f );
        snapshot._viewProj._22 = scale;
        snapshot._viewProj._33 = farPlane / ( farPlane - nearPlane );
        snapshot._viewProj._43 = -nearPlane * farPlane / ( farPlane - nearPlane );
        snapshot._viewProj._34 = 1.0f;
        snapshot._viewProj._44 = 0.0f; // 기본은 단위 행렬이다 — w 는 z 만 읽는다
        snapshot._columns      = kColumns;
        snapshot._rows         = kRows;
        snapshot._tileSize     = kTileSize;
        snapshot._depthWidth   = 1280.0f;
        snapshot._depthHeight  = 720.0f;
        snapshot._listTileDepth.assign( static_cast<size_t>( kColumns ) * kRows, tileDepth );
        return snapshot;
    }
} // namespace

/**
 * @brief [OcclusionTileQueryTest] 화면 안의 작은 구는 타일 깊이가 가까우면 가려지고, 멀면 보인다
 */
SW_TEST_CASE( OcclusionTileQueryTest, SphereBehindANearTileIsOccludedAndBehindAFarTileIsNot )
{
    const float3 center{ 0.0f, 0.0f, 40.0f }; // 화면 가운데, 네 타일 안팎을 덮는다
    SW_EXPECT_TRUE( OcclusionTileQuery::isSphereOccluded( makeSnapshot( kNear ), center, 1.0f ) );
    SW_EXPECT_FALSE( OcclusionTileQuery::isSphereOccluded( makeSnapshot( kFar ), center, 1.0f ) );
}

/**
 * @brief [OcclusionTileQueryTest] 타일마다 따로 본다 — 왼쪽 절반만 가까운 벽이면 좌우 대칭인 두 구 중 한쪽만 가려진다
 */
SW_TEST_CASE( OcclusionTileQueryTest, OnlyTheCoveredSideIsOccluded )
{
    OcclusionTileSnapshot snapshot = makeSnapshot( kFar );
    for ( uint32 row = 0; row < kRows; ++row )
    {
        for ( uint32 column = 0; column < kColumns / 2; ++column )
        {
            snapshot._listTileDepth[static_cast<size_t>( row ) * kColumns + column] = kNear;
        }
    }
    const bool bLeftOccluded  = OcclusionTileQuery::isSphereOccluded( snapshot, float3{ -8.0f, 0.0f, 40.0f }, 1.0f );
    const bool bRightOccluded = OcclusionTileQuery::isSphereOccluded( snapshot, float3{ 8.0f, 0.0f, 40.0f }, 1.0f );
    SW_EXPECT_TRUE( bLeftOccluded != bRightOccluded ); // 한쪽 반쪽 화면에만 벽이 있다
}

/**
 * @brief [OcclusionTileQueryTest] 판단할 수 없으면 보인다 — 카메라 뒤, 화면 밖, 타일을 넓게 덮는 큰 구, 쓸 수 없는 사본
 */
SW_TEST_CASE( OcclusionTileQueryTest, UnknownCasesStayVisible )
{
    const OcclusionTileSnapshot wall = makeSnapshot( kNear );
    SW_EXPECT_FALSE( OcclusionTileQuery::isSphereOccluded( wall, float3{ 0.0f, 0.0f, -10.0f }, 1.0f ) );  // 카메라 뒤
    SW_EXPECT_FALSE( OcclusionTileQuery::isSphereOccluded( wall, float3{ 500.0f, 0.0f, 40.0f }, 1.0f ) ); // 화면 밖
    SW_EXPECT_FALSE( OcclusionTileQuery::isSphereOccluded( wall, float3{ 0.0f, 0.0f, 40.0f }, 20.0f ) );  // 타일 여덟 줄을 넘게 덮는다

    OcclusionTileSnapshot broken = makeSnapshot( kNear );
    broken._listTileDepth.pop_back(); // 타일 수가 격자와 맞지 않는다
    SW_EXPECT_FALSE( broken.isValid() );
    SW_EXPECT_FALSE( OcclusionTileQuery::isSphereOccluded( broken, float3{ 0.0f, 0.0f, 40.0f }, 1.0f ) );
}

/**
 * @brief [OcclusionTileMailboxTest] 올린 사본을 가져가고, 비우면 없다 — 가져간 사본은 새 사본을 올려도 그대로다
 */
SW_TEST_CASE( OcclusionTileMailboxTest, PublishesTheLatestSnapshotAndClears )
{
    OcclusionTileMailbox mailbox;
    SW_EXPECT_TRUE( mailbox.getLatest() == nullptr );

    shared_ptr<OcclusionTileSnapshot> pFirst = make_shared<OcclusionTileSnapshot>( makeSnapshot( kNear ) );
    pFirst->_serial                          = 1;
    mailbox.publish( pFirst );
    const shared_ptr<const OcclusionTileSnapshot> pTaken = mailbox.getLatest();
    SW_REQUIRE_TRUE( pTaken != nullptr );
    SW_EXPECT_EQUAL( uint64( 1 ), pTaken->_serial );

    shared_ptr<OcclusionTileSnapshot> pSecond = make_shared<OcclusionTileSnapshot>( makeSnapshot( kFar ) );
    pSecond->_serial                          = 2;
    mailbox.publish( pSecond );
    SW_EXPECT_EQUAL( uint64( 2 ), mailbox.getLatest()->_serial );
    SW_EXPECT_EQUAL( uint64( 1 ), pTaken->_serial ); // 쥐고 있던 것은 바뀌지 않는다

    mailbox.clear();
    SW_EXPECT_TRUE( mailbox.getLatest() == nullptr );
}
