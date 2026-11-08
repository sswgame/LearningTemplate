// 표면 변형 층 — 도장 모양(SurfaceStampDef), 높이 격자(SurfaceDeformGrid: 찍기 · 겹침 · 메우기 · 창 옮기기 · 텍스처 인코딩), 씬 서비스(SurfaceDeformSystem: 붙잡기 · 놓기 · 띠)(nogpu).
#include "pch.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Environment/Surface/SurfaceDeformGrid.h"
#include "Engine/Environment/Surface/SurfaceDeformSystem.h"
#include "Engine/Environment/Surface/SurfaceFootprintSystem.h"
#include "Engine/Environment/Surface/SurfaceKind.h"
#include "Engine/Environment/Surface/SurfaceStamp.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

using namespace sw;

namespace
{
    struct SurfaceDeformTestUtil
    {
        /** @brief 발자국 하나(너비 0.2 · 길이 0.4 · 깊이 0.05 · 테두리 0.01 / 0.05 · 0.01 m/s 로 메워짐). */
        static SurfaceStampDef makePrint()
        {
            SurfaceStampDef def;
            def._shape     = SurfaceStampShape::Print;
            def._size      = float2{ 0.2f, 0.4f };
            def._depth     = 0.05f;
            def._rimHeight = 0.01f;
            def._rimWidth  = 0.05f;
            def._fillSpeed = 0.01f;
            return def;
        }

        /** @brief 지름 1 m · 깊이 0.3 m · 홈 셋 · 초당 2 라디안으로 도는 깔때기. */
        static SurfaceStampDef makeFunnel()
        {
            SurfaceStampDef def;
            def._shape     = SurfaceStampShape::Funnel;
            def._size      = float2{ 1.0f, 0.0f };
            def._depth     = 0.3f;
            def._fillSpeed = 0.5f;
            def._spinSpeed = 2.0f;
            def._armCount  = 3;
            return def;
        }
    };
} // namespace

/**
 * @brief [SurfaceStampTest] 발자국은 가운데가 깊이만큼 꺼지고 가장자리 바깥에 테두리가 솟으며, 도장 밖은 0 이다 — 앞뒤(길이)가 좌우(너비)보다 길다
 */
SW_TEST_CASE( SurfaceStampTest, PrintSinksInsideAndRaisesItsRim )
{
    const SurfaceStampDef def = SurfaceDeformTestUtil::makePrint();
    SW_EXPECT_NEAR( -0.05f, def.computeHeight( 0.0f, 0.0f, 0.0f, 0.0f ), 1.0e-6f );
    SW_EXPECT_TRUE( def.computeHeight( 0.0f, 0.15f, 0.0f, 0.0f ) < 0.0f );           // 길이 안(반 길이 0.2)
    SW_EXPECT_TRUE( def.computeHeight( 0.15f, 0.0f, 0.0f, 0.0f ) >= 0.0f );          // 너비 밖(반 너비 0.1)
    SW_EXPECT_NEAR( 0.01f, def.computeHeight( 0.125f, 0.0f, 0.0f, 0.0f ), 1.0e-4f ); // 테두리 가운데(가장자리 + 폭 절반)
    SW_EXPECT_NEAR( 0.0f, def.computeHeight( 1.0f, 1.0f, 0.0f, 0.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.2f + 0.05f, def.computeInfluenceRadius( 0.0f ), 1.0e-6f );
}

/**
 * @brief [SurfaceStampTest] 띠는 두 점 사이 길이 전체를 같은 깊이로 꺼뜨리고, 깔때기는 가운데가 가장 깊고 홈이 회전(phase)에 따라 옮겨 간다
 */
SW_TEST_CASE( SurfaceStampTest, TrailCoversItsLengthAndFunnelGroovesTurn )
{
    SurfaceStampDef trail;
    trail._shape     = SurfaceStampShape::Trail;
    trail._size      = float2{ 0.3f, 0.3f };
    trail._depth     = 0.04f;
    trail._fillSpeed = 0.01f;
    SW_EXPECT_NEAR( -0.04f, trail.computeHeight( 0.0f, 0.9f, 2.0f, 0.0f ), 1.0e-6f ); // 길이 2 m 띠의 끝 가까이도 바닥이다
    SW_EXPECT_NEAR( 0.0f, trail.computeHeight( 0.0f, 1.3f, 2.0f, 0.0f ), 1.0e-6f );   // 끝(1.0) + 반 너비(0.15) 밖

    const SurfaceStampDef funnel = SurfaceDeformTestUtil::makeFunnel();
    const float32         center = funnel.computeHeight( 0.0f, 0.0f, 0.0f, 0.0f );
    const float32         middle = funnel.computeHeight( 0.25f, 0.0f, 0.0f, 0.0f );
    SW_EXPECT_TRUE( center < middle && middle < 0.0f );
    SW_EXPECT_NEAR( 0.0f, funnel.computeHeight( 0.6f, 0.0f, 0.0f, 0.0f ), 1.0e-6f );
    // 같은 자리라도 홈이 돌면 깊이가 바뀐다(돌지 않으면 같다).
    const float32 turned = funnel.computeHeight( 0.25f, 0.0f, 0.0f, 0.5f );
    SW_EXPECT_TRUE( MathUtil::abs( turned - middle ) > 1.0e-3f );
}

/**
 * @brief [SurfaceDeformGridTest] 찍은 자리가 꺼지고, 테두리는 다른 자국을 메우지 않으며, 메워지는 속도로 0 까지 돌아오면 남은 칸이 없다
 */
SW_TEST_CASE( SurfaceDeformGridTest, StampsSinkOverlapAndFillBack )
{
    SurfaceDeformGrid grid;
    grid.reset( 128, 4.0f, 0.0f, 0.0f ); // 텍셀 3.125 cm
    SW_EXPECT_TRUE( grid.hasDirtyRect() );
    grid.clearDirtyRect();
    const SurfaceStampDef def = SurfaceDeformTestUtil::makePrint();
    grid.applyStamp( def, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f );
    SW_EXPECT_TRUE( grid.findHeightAt( 0.0f, 0.0f ) < -0.04f );
    SW_EXPECT_TRUE( grid.computeMaxDepth() > 0.04f );
    SW_EXPECT_TRUE( grid.countActiveTexels() > 0 );
    SW_EXPECT_TRUE( grid.hasDirtyRect() );

    // 옆에 찍은 발자국의 테두리가 첫 자국 자리에 닿아도 자국은 그대로 꺼져 있다.
    const float32 before = grid.findHeightAt( 0.09f, 0.0f );
    grid.applyStamp( def, 0.22f, 0.0f, 0.0f, 0.0f, 0.0f );
    SW_EXPECT_TRUE( grid.findHeightAt( 0.09f, 0.0f ) <= before );

    // 깊이 0.05 m · 0.01 m/s → 5 초면 모두 평평하다(테두리 0.01 은 1 초).
    grid.fill( 2.0f );
    SW_EXPECT_TRUE( grid.findHeightAt( 0.0f, 0.0f ) < 0.0f );
    grid.fill( 3.5f );
    SW_EXPECT_NEAR( 0.0f, grid.findHeightAt( 0.0f, 0.0f ), 1.0e-6f );
    SW_EXPECT_EQUAL( 0u, grid.countActiveTexels() );
    SW_EXPECT_NEAR( 0.0f, grid.computeMaxDepth(), 1.0e-6f );
}

/**
 * @brief [SurfaceDeformGridTest] 창을 옮겨도 자국은 같은 월드 자리에 남고, 창 밖으로 나간 자국은 사라진다
 */
SW_TEST_CASE( SurfaceDeformGridTest, RecenterKeepsMarksInWorldPlace )
{
    SurfaceDeformGrid grid;
    grid.reset( 64, 8.0f, 0.0f, 0.0f ); // 텍셀 0.125 m, 창 [-4, 4)
    SurfaceStampDef def = SurfaceDeformTestUtil::makePrint();
    def._size           = float2{ 0.5f, 0.5f };
    def._rimHeight      = 0.0f;
    def._rimWidth       = 0.0f;
    grid.applyStamp( def, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f );
    grid.applyStamp( def, -3.5f, 0.0f, 0.0f, 0.0f, 0.0f );
    const float32 kept = grid.findHeightAt( 1.0f, 1.0f );
    SW_REQUIRE_TRUE( kept < 0.0f );

    SW_EXPECT_TRUE( grid.recenter( 2.0f, 0.0f ) ); // 창 [-2, 6)
    SW_EXPECT_NEAR( kept, grid.findHeightAt( 1.0f, 1.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, grid.findHeightAt( -3.5f, 0.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( -2.0f, grid.getOrigin()._x, 1.0e-5f );
    SW_EXPECT_FALSE( grid.recenter( 2.01f, 0.0f ) ); // 한 칸이 안 되면 그대로다
    // 둘레 찾기는 그 사각형 안 자국만 본다.
    SW_EXPECT_NEAR( -kept, grid.computeMaxDepthNear( 1.0f, 1.0f, 0.5f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, grid.computeMaxDepthNear( 4.0f, -3.0f, 0.5f ), 1.0e-6f );
}

/**
 * @brief [SurfaceDeformGridTest] 텍스처 인코딩 — 평평은 흰색(R = G = 255), 범위만큼 꺼지면 R = 0, 범위를 넘으면 범위에 붙는다
 */
SW_TEST_CASE( SurfaceDeformGridTest, TexelsEncodeSinkInRedAndFlatAsWhite )
{
    SurfaceDeformGrid grid;
    grid.reset( 16, 1.6f, 0.0f, 0.0f ); // 텍셀 0.1 m
    SurfaceStampDef def = SurfaceDeformTestUtil::makePrint();
    def._size           = float2{ 0.6f, 0.6f };
    def._depth          = 0.2f; // 범위 0.1 을 넘는다
    def._rimHeight      = 0.0f;
    def._rimWidth       = 0.0f;
    grid.applyStamp( def, 0.05f, 0.05f, 0.0f, 0.0f, 0.0f );
    vector<uint8> listByte;
    // 가운데 칸(8, 8)과 구석 칸(0, 0).
    grid.writeTexels( SurfaceDeformGrid::TexelRect{ 8, 8, 9, 9 }, 0.1f, listByte );
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( listByte.size() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( listByte[0] ) );
    SW_EXPECT_EQUAL( 255u, static_cast<uint32>( listByte[1] ) );
    grid.writeTexels( SurfaceDeformGrid::TexelRect{ 0, 0, 1, 1 }, 0.1f, listByte );
    SW_EXPECT_EQUAL( 255u, static_cast<uint32>( listByte[0] ) );
    SW_EXPECT_EQUAL( 255u, static_cast<uint32>( listByte[1] ) );
    SW_EXPECT_EQUAL( 255u, static_cast<uint32>( listByte[3] ) );
}

/**
 * @brief [SurfaceDeformSystemTest] 붙잡은 도장은 메워지지 않고, 놓으면 자기 속도로 메워진다 — 씬을 비운 뒤 옛 핸들로 옮기기는 거짓이다
 */
SW_TEST_CASE( SurfaceDeformSystemTest, HeldStampStaysUntilReleased )
{
    GameObjectManager     manager;
    SurfaceDeformSystem&  deform = manager.getSubsystem<SurfaceDeformSystem>();
    const SurfaceStampDef funnel = SurfaceDeformTestUtil::makeFunnel();
    if constexpr ( build::kWithClientCode == false )
    {
        // 변형은 보이는 것만이다 — 전용 서버는 격자를 두지 않아 도장을 받지 않는다(SurfaceDeformSystem::ensureGrid).
        SW_EXPECT_FALSE( deform.addPersistentStamp( funnel, float3{ 3.0f, 0.0f, -2.0f }, 0.0f ).isValid() );
        SW_EXPECT_EQUAL( 0u, deform.getPersistentStampCount() );
        return;
    }
    const SurfaceStampHandle handle = deform.addPersistentStamp( funnel, float3{ 3.0f, 0.0f, -2.0f }, 0.0f );
    SW_REQUIRE_TRUE( handle.isValid() );
    SW_EXPECT_EQUAL( 1u, deform.getPersistentStampCount() );
    for ( uint32 frame = 0; frame < 120; ++frame )
    {
        deform.update( 1.0f / 60.0f );
    }
    // 2 초 지나도(메움 0.5 m/s 면 0.6 초에 평평) 붙잡은 깔때기는 깊다 — 가운데 칸은 홈 사이 둔덕이어도 0.3 × 0.95 × 0.7 m 보다 깊다.
    SW_EXPECT_TRUE( deform.findHeightAt( 3.0f, -2.0f ) < -0.19f );
    SW_EXPECT_TRUE( deform.movePersistentStamp( handle, float3{ 3.5f, 0.0f, -2.0f }, 0.0f ) );

    deform.removePersistentStamp( handle );
    SW_EXPECT_EQUAL( 0u, deform.getPersistentStampCount() );
    SW_EXPECT_FALSE( deform.movePersistentStamp( handle, float3{ 3.0f, 0.0f, -2.0f }, 0.0f ) );
    for ( uint32 frame = 0; frame < 60; ++frame )
    {
        deform.update( 1.0f / 60.0f );
    }
    SW_EXPECT_EQUAL( 0u, deform.getGrid().countActiveTexels() ); // 0.3 m · 0.5 m/s → 0.6 초

    const SurfaceStampHandle again = deform.addPersistentStamp( funnel, float3{ 3.0f, 0.0f, -2.0f }, 0.0f );
    deform.onSceneCleared();
    SW_EXPECT_FALSE( deform.movePersistentStamp( again, float3{ 3.0f, 0.0f, -2.0f }, 0.0f ) );
    SW_EXPECT_EQUAL( 0u, deform.getStampCount() );
}

/**
 * @brief [SurfaceDeformSystemTest] 띠는 두 점 사이 땅 전체를 꺼뜨리고, 발걸음은 발자국을 남기는 종류에서만 찍힌다(돌은 세기만 한다)
 */
SW_TEST_CASE( SurfaceDeformSystemTest, SegmentAndFootstepsStampTheLayer )
{
    GameObjectManager    manager;
    SurfaceDeformSystem& deform = manager.getSubsystem<SurfaceDeformSystem>();
    if constexpr ( build::kWithClientCode == false )
    {
        // 전용 서버는 변형 격자를 두지 않는다 — 찍어도 쌓이지 않는다(SurfaceDeformSystem::ensureGrid)
        SW_EXPECT_EQUAL( 0u, deform.getPersistentStampCount() );
        return;
    }
    SurfaceStampDef wheel;
    wheel._shape     = SurfaceStampShape::Trail;
    wheel._size      = float2{ 0.2f, 0.2f };
    wheel._depth     = 0.03f;
    wheel._fillSpeed = 0.01f;
    deform.stampSegment( wheel, float3{ -1.0f, 0.0f, 0.0f }, float3{ 1.0f, 0.0f, 0.0f } );
    SW_EXPECT_TRUE( deform.findHeightAt( -0.8f, 0.0f ) < -0.02f );
    SW_EXPECT_TRUE( deform.findHeightAt( 0.0f, 0.0f ) < -0.02f );
    SW_EXPECT_TRUE( deform.findHeightAt( 0.8f, 0.0f ) < -0.02f );
    SW_EXPECT_NEAR( 0.0f, deform.findHeightAt( 0.0f, 0.5f ), 1.0e-6f );

    const shared_ptr<const SurfaceKindTable> pTable = SurfaceKindTable::findDefault();
    SW_REQUIRE_NOT_NULL( pTable );
    const SurfaceKindDef* pSnow  = pTable->findKind( "Snow" );
    const SurfaceKindDef* pStone = pTable->findKind( "Stone" );
    SW_REQUIRE_NOT_NULL( pSnow );
    SW_REQUIRE_NOT_NULL( pStone );
    SurfaceFootprintSystem& footprints = manager.getSubsystem<SurfaceFootprintSystem>();
    footprints.addFootstep( *pStone, float3{ 0.0f, 0.0f, 2.0f }, 0.0f );
    SW_EXPECT_EQUAL( 0u, footprints.getPlacedCount() );
    SW_EXPECT_NEAR( 0.0f, deform.findHeightAt( 0.0f, 2.0f ), 1.0e-6f );
    footprints.addFootstep( *pSnow, float3{ 0.0f, 0.0f, 2.0f }, 0.0f );
    SW_EXPECT_EQUAL( 1u, footprints.getPlacedCount() );
    SW_EXPECT_TRUE( deform.findHeightAt( 0.0f, 2.0f ) < -0.5f * pSnow->_footprint._depth );
    SW_EXPECT_EQUAL( 1u, footprints.getFootstepCount( hashed_string( "Snow" ) ) );
}
