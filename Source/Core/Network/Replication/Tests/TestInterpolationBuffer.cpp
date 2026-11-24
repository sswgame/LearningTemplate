#include "pch.h"

#include "Core/Network/Replication/InterpolationBuffer.h"
#include "Core/Testing/TestFramework.h"

// 보간 표본 줄 — 틱 순 끼우기(같은 틱은 바꾸기 인자로), 상한, 구간 넷(빔 · 첫 것 앞 · 사이 · 끝 뒤 — 내다보지 않는다), 다 쓴 표본 버리기, 비율 식.

using namespace sw;

namespace
{
    using IntSample = InterpolationBuffer<int32>::Sample;
} // namespace

/**
 * @brief [InterpolationBufferTest] 늦게 온 옛 표본도 제자리에 들고, 같은 틱은 바꾸기 인자일 때만 바뀌고, 상한을 넘으면 가장 오래된 것을 버린다
 */
SW_TEST_CASE( InterpolationBufferTest, SamplesStayInTickOrderWithinCapacity )
{
    InterpolationBuffer<int32> buffer;
    buffer.initialize( 3 );
    buffer.insert( 10, 1, false );
    buffer.insert( 4, 2, false ); // 앞질러 온 옛 것 — 앞에 끼운다
    buffer.insert( 7, 3, false );
    buffer.insert( 7, 4, false ); // 같은 틱 — 바꾸지 않는다
    SW_EXPECT_EQUAL( 3, buffer.getCount() );
    const IntSample* pFrom = nullptr;
    const IntSample* pTo   = nullptr;
    NetSampleRatio   ratio = NetSampleRatio::makeHeld();
    SW_EXPECT_TRUE( InterpolationBracketKind::Between == buffer.findBracket( 8.5f, pFrom, pTo, ratio ) );
    SW_REQUIRE_NOT_NULL( pFrom );
    SW_REQUIRE_NOT_NULL( pTo );
    SW_EXPECT_EQUAL( 3, pFrom->_value );
    SW_EXPECT_EQUAL( 10u, pTo->_tick );
    SW_EXPECT_NEAR( 0.5f, ratio.interpolate( 0.0f, 1.0f ), 1.0e-6f );
    buffer.insert( 7, 5, true ); // 같은 틱 — 바꾼다(멈춤 확정)
    SW_EXPECT_TRUE( InterpolationBracketKind::Between == buffer.findBracket( 7.0f, pFrom, pTo, ratio ) );
    SW_EXPECT_EQUAL( 5, pFrom->_value );
    SW_EXPECT_NEAR( 0.0f, ratio.interpolate( 0.0f, 1.0f ), 1.0e-6f );
    buffer.insert( 12, 6, false ); // 상한 3 — 가장 오래된 4 를 버린다
    SW_EXPECT_EQUAL( 3, buffer.getCount() );
    SW_EXPECT_TRUE( InterpolationBracketKind::BeforeFirst == buffer.findBracket( 5.0f, pFrom, pTo, ratio ) );
    SW_EXPECT_EQUAL( 7u, pFrom->_tick );
    SW_EXPECT_TRUE( pFrom == pTo );
}

/**
 * @brief [InterpolationBufferTest] 구간은 비었다 · 첫 것 앞 · 사이 · 끝 뒤 넷이고, 끝 뒤는 마지막 표본에 멈춘다(외삽하지 않는다)
 */
SW_TEST_CASE( InterpolationBufferTest, BracketHoldsTheEndsAndDoesNotExtrapolate )
{
    InterpolationBuffer<int32> buffer;
    buffer.initialize( 8 );
    const IntSample* pFrom = nullptr;
    const IntSample* pTo   = nullptr;
    NetSampleRatio   ratio = NetSampleRatio::makeHeld();
    SW_EXPECT_TRUE( InterpolationBracketKind::Empty == buffer.findBracket( 1.0f, pFrom, pTo, ratio ) );
    SW_EXPECT_NULL( pFrom );
    SW_EXPECT_NULL( pTo );
    buffer.insert( 10, 1, false );
    buffer.insert( 20, 2, false );
    SW_EXPECT_TRUE( InterpolationBracketKind::BeforeFirst == buffer.findBracket( 9.0f, pFrom, pTo, ratio ) );
    SW_EXPECT_EQUAL( 1, pFrom->_value );
    SW_EXPECT_TRUE( InterpolationBracketKind::Between == buffer.findBracket( 15.0f, pFrom, pTo, ratio ) );
    SW_EXPECT_NEAR( 0.5f, ratio.interpolate( 0.0f, 1.0f ), 1.0e-6f );
    SW_EXPECT_TRUE( InterpolationBracketKind::AfterLast == buffer.findBracket( 20.0f, pFrom, pTo, ratio ) );
    SW_EXPECT_EQUAL( 2, pFrom->_value );
    SW_EXPECT_TRUE( pFrom == pTo );
    SW_EXPECT_TRUE( InterpolationBracketKind::AfterLast == buffer.findBracket( 25.0f, pFrom, pTo, ratio ) );
    SW_EXPECT_NEAR( 0.0f, ratio.interpolate( 0.0f, 1.0f ), 1.0e-6f );
}

/** @brief [InterpolationBufferTest] 칸마다의 규칙은 비율 형식이 정한다 — 잇는 칸은 사이 값, 이산 칸 · 있고 없음은 뒤 표본의 시각이 되어야 바뀐다 */
SW_TEST_CASE( InterpolationBufferTest, RatioDecidesHowEachFieldChanges )
{
    const NetSampleRatio middle = NetSampleRatio::make( 10, 14, 12.0 );
    SW_EXPECT_NEAR( 3.0f, middle.interpolate( 2.0f, 4.0f ), 1.0e-6f );
    SW_EXPECT_NEAR( 0.5f, NetSampleRatio::make( 1u << 24, ( 1u << 24 ) + 2u, 16777217.0 ).interpolate( 0.0f, 1.0f ), 1.0e-6f ); // 큰 틱에서도 비율을 잃지 않는다
    SW_EXPECT_EQUAL( 1, middle.choose( 1, 2 ) );                                                                                // 쓰러짐 · 무기는 아직이다
    SW_EXPECT_TRUE( middle.isPresent( true, false ) );                                                                          // 사라지는 몸은 그때까지 있다
    SW_EXPECT_FALSE( middle.isPresent( false, true ) );                                                                         // 생겨나는 몸은 그때부터 있다
    const NetSampleRatio end = NetSampleRatio::make( 10, 14, 99.0 );                                                            // 끝으로 자른다
    SW_EXPECT_EQUAL( 2, end.choose( 1, 2 ) );
    SW_EXPECT_TRUE( end.isPresent( false, true ) );
    SW_EXPECT_NEAR( 0.0f, NetSampleRatio::make( 5, 5, 7.0 ).interpolate( 0.0f, 1.0f ), 1.0e-6f ); // 뒤가 앞보다 크지 않다

    // 속도는 두 표본에서 낸다(4 틱에 8 m, 틱 0.1 초).
    const float3 from{ 0.0f, 0.0f, 0.0f };
    const float3 to{ 8.0f, 0.0f, 0.0f };
    SW_EXPECT_NEAR( 20.0f, middle.computeVelocity( from, to, 0.1f )._x, 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, NetSampleRatio::makeHeld().computeVelocity( from, to, 0.1f )._x, 1.0e-6f );

    // 순간 이동은 잇지 않고 속도가 0 이다.
    NetSampleRatio broken = middle;
    broken.breakAt( 3, 4 );
    SW_EXPECT_NEAR( 0.0f, broken.interpolate( from, to )._x, 1.0e-6f );
    SW_EXPECT_NEAR( 0.0f, broken.computeVelocity( from, to, 0.1f )._x, 1.0e-6f );
}

/**
 * @brief [InterpolationBufferTest] 다 쓴 표본은 렌더 틱 이하의 마지막 하나(보간의 앞)만 남기고, 둘보다 적게는 줄이지 않는다
 */
SW_TEST_CASE( InterpolationBufferTest, ConsumedSamplesKeepOneBehindTheRenderTick )
{
    InterpolationBuffer<int32> buffer;
    buffer.initialize( 8 );
    for ( uint32 tick = 0; tick <= 18; tick += 6 )
    {
        buffer.insert( tick, static_cast<int32>( tick ), false );
    }
    buffer.removeConsumed( 13.0f ); // 0 · 6 을 버리고 12 · 18 이 남는다
    SW_EXPECT_EQUAL( 2, buffer.getCount() );
    const IntSample* pFrom = nullptr;
    const IntSample* pTo   = nullptr;
    NetSampleRatio   ratio = NetSampleRatio::makeHeld();
    SW_EXPECT_TRUE( InterpolationBracketKind::Between == buffer.findBracket( 13.0f, pFrom, pTo, ratio ) );
    SW_EXPECT_EQUAL( 12u, pFrom->_tick );
    buffer.removeConsumed( 100.0f ); // 둘 — 더 줄이지 않는다
    SW_EXPECT_EQUAL( 2, buffer.getCount() );
    buffer.clear();
    SW_EXPECT_TRUE( buffer.isEmpty() );
}
