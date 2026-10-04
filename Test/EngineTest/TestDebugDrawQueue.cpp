#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    const float4 kWhite{ 1.0f, 1.0f, 1.0f, 1.0f };
} // namespace

/**
 * @brief [DebugDrawQueueTest] 넣은 것은 프레임 끝에 보이고, 지속 시간 0 은 한 프레임 뒤 사라진다
 * @details 그리는 쪽(에디터 UI)은 씬 틱보다 먼저 돈다. 넣자마자 보이는 목록에 두고 프레임 끝에 비우면 씬 틱에서 넣은 것이 한 번도 그려지지
 *          않는다 — 그래서 확정은 `endFrame` 이 한다.
 */
SW_TEST_CASE( DebugDrawQueueTest, ItemsBecomeVisibleAtFrameEndAndOneFrameItemsExpire )
{
    DebugDrawQueue queue;
    queue.drawLine( float3{ 0.0f, 0.0f, 0.0f }, float3{ 1.0f, 0.0f, 0.0f }, kWhite );
    SW_EXPECT_TRUE( queue.getVisibleLines().empty() );

    queue.endFrame( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( size_t( 1 ), queue.getVisibleLines().size() );

    queue.endFrame( 1.0f / 60.0f );
    SW_EXPECT_TRUE( queue.getVisibleLines().empty() );

    // 일시정지(델타 0)에는 한 프레임짜리도 남는다 — 멈춘 화면에서 마지막 프레임의 디버그 도형을 본다.
    queue.drawLine( float3{ 0.0f, 0.0f, 0.0f }, float3{ 0.0f, 1.0f, 0.0f }, kWhite );
    queue.endFrame( 0.0f );
    queue.endFrame( 0.0f );
    SW_EXPECT_EQUAL( size_t( 1 ), queue.getVisibleLines().size() );
}

/**
 * @brief [DebugDrawQueueTest] 지속 시간을 준 도형은 그 시간만큼 남고, 일시정지(델타 0)에는 줄지 않는다
 */
SW_TEST_CASE( DebugDrawQueueTest, DurationKeepsItemsAndPauseFreezesThem )
{
    DebugDrawQueue queue;
    queue.drawSphere( float3{}, 1.0f, kWhite, 0.25f );
    queue.drawText( float3{}, "hp 10", kWhite, 0.25f );

    for ( uint32 frameIndex = 0; frameIndex < 2; ++frameIndex )
    {
        queue.endFrame( 0.1f );
        SW_EXPECT_EQUAL( size_t( 1 ), queue.getVisibleSpheres().size() );
        SW_ASSERT_EQUAL( size_t( 1 ), queue.getVisibleTexts().size() );
        SW_EXPECT_STREQ( "hp 10", queue.getVisibleTexts()[0]._text.c_str() );
    }
    // 0.2 초가 흘렀다. 일시정지 프레임은 시간을 흘리지 않는다.
    for ( uint32 frameIndex = 0; frameIndex < 5; ++frameIndex )
    {
        queue.endFrame( 0.0f );
        SW_EXPECT_EQUAL( size_t( 1 ), queue.getVisibleSpheres().size() );
    }
    queue.endFrame( 0.1f ); // 0.3 초 — 이 프레임까지 보이고 지난다
    SW_EXPECT_EQUAL( size_t( 1 ), queue.getVisibleSpheres().size() );
    queue.endFrame( 0.1f );
    SW_EXPECT_TRUE( queue.getVisibleSpheres().empty() );
    SW_EXPECT_TRUE( queue.getVisibleTexts().empty() );
}

/**
 * @brief [DebugDrawQueueTest] 끈 카테고리는 보이는 목록에서 빠지고, 빈 카테고리는 Default 다
 */
SW_TEST_CASE( DebugDrawQueueTest, DisabledCategoryIsHidden )
{
    DebugDrawQueue queue;
    queue.drawLine( float3{}, float3{ 1.0f, 0.0f, 0.0f }, kWhite, 1.0f, "AI" );
    queue.drawLine( float3{}, float3{ 0.0f, 1.0f, 0.0f }, kWhite, 1.0f );

    vector<hashed_string> listCategory;
    queue.collectCategories( listCategory );
    SW_ASSERT_EQUAL( size_t( 2 ), listCategory.size() );
    SW_EXPECT_STREQ( "AI", listCategory[0].c_str() );
    SW_EXPECT_STREQ( DebugDrawQueue::kDefaultCategoryName, listCategory[1].c_str() );

    queue.setCategoryEnabled( "AI", false );
    SW_EXPECT_FALSE( queue.isCategoryEnabled( "ai" ) ); // 이름은 대소문자를 가리지 않는다
    queue.endFrame( 0.1f );
    SW_ASSERT_EQUAL( size_t( 1 ), queue.getVisibleLines().size() );
    SW_EXPECT_NEAR_EQUAL( 1.0f, queue.getVisibleLines()[0]._to._y, 1e-6f );

    // 다시 켜면 남은 시간 안의 도형이 돌아온다.
    queue.setCategoryEnabled( "AI", true );
    queue.endFrame( 0.1f );
    SW_EXPECT_EQUAL( size_t( 2 ), queue.getVisibleLines().size() );
}

/**
 * @brief [DebugDrawQueueTest] 상자는 모서리 열둘, 반 크기 0 인 축이 있으면(2D) 사각형 넷이고 회전을 받는다
 */
SW_TEST_CASE( DebugDrawQueueTest, BoxExpandsToEdges )
{
    DebugDrawQueue queue;
    queue.drawBox( float3{ 0.0f, 0.0f, 0.0f }, float3{ 1.0f, 2.0f, 3.0f }, kWhite );
    queue.endFrame( 0.0f );
    SW_ASSERT_EQUAL( size_t( 12 ), queue.getVisibleLines().size() );
    float32 edgeLengthSum{ 0.0f };
    for ( const DebugLine& line : queue.getVisibleLines() )
    {
        edgeLengthSum += ( line._to - line._from ).getLength();
    }
    SW_EXPECT_NEAR_EQUAL( 4.0f * ( 2.0f + 4.0f + 6.0f ), edgeLengthSum, 1e-4f );

    queue.clear();
    queue.drawBox( float3{ 5.0f, 5.0f, 0.0f }, float3{ 1.0f, 1.0f, 0.0f }, kWhite );
    queue.endFrame( 0.0f );
    SW_ASSERT_EQUAL( size_t( 4 ), queue.getVisibleLines().size() );
    for ( const DebugLine& line : queue.getVisibleLines() )
    {
        SW_EXPECT_NEAR_EQUAL( 2.0f, ( line._to - line._from ).getLength(), 1e-5f );
        SW_EXPECT_NEAR_EQUAL( 0.0f, line._from._z, 1e-6f );
    }

    // X 반 크기가 0 이면 YZ 평면의 사각형이다(변 길이 2 · 4).
    queue.clear();
    queue.drawBox( float3{}, float3{ 0.0f, 1.0f, 2.0f }, kWhite );
    queue.endFrame( 0.0f );
    SW_ASSERT_EQUAL( size_t( 4 ), queue.getVisibleLines().size() );
    float32 flatLengthSum{ 0.0f };
    for ( const DebugLine& line : queue.getVisibleLines() )
    {
        flatLengthSum += ( line._to - line._from ).getLength();
        SW_EXPECT_NEAR_EQUAL( 0.0f, line._from._x, 1e-6f );
    }
    SW_EXPECT_NEAR_EQUAL( 12.0f, flatLengthSum, 1e-4f );

    // Z 축으로 90 도 돌린 상자의 X 반 크기는 Y 방향으로 간다.
    queue.clear();
    const quaternion rotation = quaternion::createFromAxisAngle( float3{ 0.0f, 0.0f, 1.0f }, MathUtil::Pi * 0.5f );
    queue.drawOrientedBox( float3{}, float3{ 3.0f, 0.5f, 0.0f }, rotation, kWhite );
    queue.endFrame( 0.0f );
    float32 maxAbsY{ 0.0f };
    for ( const DebugLine& line : queue.getVisibleLines() )
    {
        maxAbsY = MathUtil::max( maxAbsY, MathUtil::abs( line._from._y ) );
    }
    SW_EXPECT_NEAR_EQUAL( 3.0f, maxAbsY, 1e-4f );
}

/**
 * @brief [DebugDrawQueueTest] 화살표는 몸통 하나와 머리 넷이고, 머리 한 쌍은 XY 평면 안에 있다(2D 뷰의 화살촉)
 */
SW_TEST_CASE( DebugDrawQueueTest, ArrowHasAShaftAndAPlanarHead )
{
    DebugDrawQueue queue;
    queue.drawArrow( float3{ 0.0f, 0.0f, 0.0f }, float3{ 10.0f, 0.0f, 0.0f }, kWhite, 0.0f, "Nav" );
    queue.drawArrow( float3{ 1.0f, 1.0f, 1.0f }, float3{ 1.0f, 1.0f, 1.0f }, kWhite ); // 길이 0 은 넣지 않는다
    queue.endFrame( 0.0f );
    const vector<DebugLine>& listLine = queue.getVisibleLines();
    SW_ASSERT_EQUAL( size_t( 5 ), listLine.size() );
    SW_EXPECT_NEAR_EQUAL( 10.0f, listLine[0]._to._x, 1e-6f );

    uint32 planarHeadCount{ 0 };
    for ( size_t lineIndex = 1; lineIndex < listLine.size(); ++lineIndex )
    {
        const DebugLine& head = listLine[lineIndex];
        SW_EXPECT_NEAR_EQUAL( 10.0f, head._from._x, 1e-5f );
        SW_EXPECT_NEAR_EQUAL( 10.0f * ( 1.0f - DebugDrawQueue::kArrowHeadRatio ), head._to._x, 1e-4f );
        SW_EXPECT_STREQ( "Nav", head._category.c_str() );
        if ( MathUtil::abs( head._to._z ) < 1e-6f && MathUtil::abs( head._to._y ) > 1e-3f )
            ++planarHeadCount;
    }
    SW_EXPECT_EQUAL( 2u, planarHeadCount );
}
