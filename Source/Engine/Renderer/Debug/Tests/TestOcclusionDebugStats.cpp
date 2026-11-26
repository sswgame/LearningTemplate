/**
 * @file TestOcclusionDebugStats.cpp
 * @brief 가림 타일 요약(`OcclusionTileStats`) 테스트 — 덮은 타일 수 · 가장 가까운 깊이, 가려진 구 수가 GPU 컬링 식과 같다.
 */
#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Renderer/Debug/OcclusionDebugStats.h"

using namespace sw;

namespace
{
    /** @brief 원점에서 +Z 를 보는 카메라(TestOcclusionTileSnapshot 과 같은 투영)의 4 x 2 타일 사본입니다 — 왼쪽 두 열은 @p nearDepth, 나머지는 1(아무것도 없음). */
    OcclusionTileSnapshot makeHalfCoveredSnapshot( float32 nearDepth )
    {
        constexpr uint32      kColumns   = 4;
        constexpr uint32      kRows      = 2;
        constexpr float32     kFarPlane  = 1000.0f;
        constexpr float32     kNearPlane = 0.1f;
        OcclusionTileSnapshot snapshot;
        const float32         scale = 1.0f / MathUtil::tan( 0.35f );
        snapshot._viewProj          = float4x4{};
        snapshot._viewProj._11      = scale;
        snapshot._viewProj._22      = scale;
        snapshot._viewProj._33      = kFarPlane / ( kFarPlane - kNearPlane );
        snapshot._viewProj._43      = -kNearPlane * kFarPlane / ( kFarPlane - kNearPlane );
        snapshot._viewProj._34      = 1.0f;
        snapshot._viewProj._44      = 0.0f;
        snapshot._columns           = kColumns;
        snapshot._rows              = kRows;
        snapshot._tileSize          = 16;
        snapshot._depthWidth        = 64.0f;
        snapshot._depthHeight       = 32.0f;
        snapshot._serial            = 7;
        snapshot._listTileDepth     = { nearDepth, nearDepth, 1.0f, 1.0f, nearDepth, nearDepth, 1.0f, 1.0f };
        return snapshot;
    }
} // namespace

SW_TEST_CASE( OcclusionDebugStatsTest, CountsCoveredTilesAndNearestDepth )
{
    const OcclusionTileStats stats = OcclusionTileStats::make( makeHalfCoveredSnapshot( 0.25f ) );
    SW_EXPECT_EQUAL( 8u, stats._tileCount );
    SW_EXPECT_EQUAL( 4u, stats._coveredTileCount );
    SW_EXPECT_NEAR( 0.25f, stats._nearestDepth, 1.0e-6f );
    SW_EXPECT_EQUAL( uint64{ 7 }, stats._serial );
    SW_EXPECT_EQUAL( 0u, OcclusionTileStats::make( OcclusionTileSnapshot{} )._tileCount ); // 쓸 수 없는 사본
}

SW_TEST_CASE( OcclusionDebugStatsTest, CountsSpheresBehindTheSameRuleAsCulling )
{
    // 가까운 벽(깊이 0)이 왼쪽 절반을 덮는다 — 왼쪽의 작은 구만 가려진다(같은 식: OcclusionTileSnapshot::isSphereOccluded).
    const OcclusionTileSnapshot snapshot = makeHalfCoveredSnapshot( 0.0f );
    const float4                arrSphere[]{
        float4{-10.0f, 0.0f, 40.0f, 0.5f},
        float4{ 10.0f, 0.0f, 40.0f, 0.5f}
    };
    const uint32 occluded = OcclusionTileStats::countOccludedSpheres( snapshot, vector_reference<const float4>( arrSphere, 2 ) );
    SW_EXPECT_EQUAL( 1u, occluded ); // 왼쪽(화면 u 약 0.16 — 0 열)만
    SW_EXPECT_TRUE( OcclusionTileSnapshot::isSphereOccluded( snapshot, float3{ -10.0f, 0.0f, 40.0f }, 0.5f ) );
}
