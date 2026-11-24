// 컨테이너 범위 검사(`SW_CHECK_INDEX` · `SW_CHECK_INDEX_OR_END`) — 경계 밖 접근이 개발 구성에서 단언으로 잡히고(가로채기로 센다), 경계 안 접근은 하나도 잡히지 않는지(음성 대조).
// 경계 밖 케이스는 가로채기 뒤에도 코드가 이어지므로 원소를 읽지 않고 주소만 만들거나, 할당 · 용량 안쪽 칸만 가리키게 짠다(ASan 이 남의 메모리 접근으로 보지 않게).
// `string` 의 범위 밖은 StringTest.OutOfRangePositionsAssertAndClamp 가 본다.
#include "pch.h"

#include "Core/Container/EnumArray.h"
#include "Core/Container/RingBuffer.h"
#include "Core/Container/array.h"
#include "Core/Container/deque.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"
#include "Core/Testing/ScopedAssertCapture.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    enum class BoundsCheckColor : uint8
    {
        Red,
        Green,
        Blue,
        Count
    };

    /** @brief @p fn 을 단언 가로채기 안에서 부르고 잡힌 단언 수를 반환합니다. */
    template <typename Fn>
    uint32 countCapturedAsserts( Fn&& fn )
    {
        const test::ScopedAssertCapture assertCapture;
        fn();
        return assertCapture.getCount();
    }

    constexpr bool kAssertsAreActive = test::ScopedAssertCapture::kAssertsAreActive;

    // constexpr 문맥: 검사가 든 `operator[]` 가 상수 평가에서 그대로 쓰인다(실패 분기만 상수 식이 아니다). `array` 는 개발 구성에서 레이스 탐지 멤버를 들어 리터럴 형식이 아니므로 여기서 보지 않는다.
    constexpr int32             kArrRaw[3] = { 10, 20, 30 };
    constexpr span<const int32> kViewConstant( kArrRaw );
    static_assert( kViewConstant[2] == 30 );
    static_assert( kViewConstant.front() == 10 && kViewConstant.back() == 30 );
    static_assert( kViewConstant.subspan( 1, 2 ).size() == 2 );
    constexpr EnumArray<BoundsCheckColor, int32> kArrColorConstant = makeEnumTable<BoundsCheckColor, int32>( 1, 2, 3 );
    static_assert( kArrColorConstant[BoundsCheckColor::Blue] == 3 );
} // namespace

/**
 * @brief [BoundsCheckTest] 매크로 자체 — 끝 미만 · 끝 이하 경계와 음수 인덱스
 */
SW_TEST_CASE( BoundsCheckTest, MacroBoundaries )
{
    SW_EXPECT_EQUAL( 0u, countCapturedAsserts( []
    {
        SW_CHECK_INDEX( 0, 1 );
        SW_CHECK_INDEX( 3, 4 );
        SW_CHECK_INDEX_OR_END( 4, 4 );
        SW_CHECK_INDEX_OR_END( 0, 0 );
    } ) );
    if constexpr ( kAssertsAreActive )
    {
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( []
        { SW_CHECK_INDEX( 4, 4 ); } ) );
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( []
        { SW_CHECK_INDEX( 0, 0 ); } ) );
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( []
        { SW_CHECK_INDEX_OR_END( 5, 4 ); } ) );
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( []
        {
            const int32 negativeIndex = -1;
            SW_CHECK_INDEX( negativeIndex, 4 ); // 음수는 size_t 로 바뀌어 아주 큰 수가 된다
        } ) );
    }
}

/**
 * @brief [BoundsCheckTest] array — operator[] · front · back
 */
SW_TEST_CASE( BoundsCheckTest, Array )
{
    array<int32, 4> arrValue{ 1, 2, 3, 4 };
    SW_EXPECT_EQUAL( 0u, countCapturedAsserts( [&]
    {
        SW_EXPECT_EQUAL( 4, arrValue[3] );
        SW_EXPECT_EQUAL( 1, arrValue.front() );
        SW_EXPECT_EQUAL( 4, arrValue.back() );
    } ) );
    if constexpr ( kAssertsAreActive )
    {
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&arrValue[4]; } ) );
        const array<int32, 4>& arrConst = arrValue;
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&arrConst[4]; } ) );
        array<int32, 0> arrEmpty; // 저장소는 한 칸이라 주소는 안쪽이다
        SW_EXPECT_EQUAL( 2u, countCapturedAsserts( [&]
        {
            (void)&arrEmpty.front();
            (void)&arrEmpty.back();
        } ) );
    }
}

/**
 * @brief [BoundsCheckTest] span — operator[] · front · first · last · subspan
 */
SW_TEST_CASE( BoundsCheckTest, Span )
{
    int32       arrBuffer[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    span<int32> view( arrBuffer, 4 ); // 뒤 네 칸은 버퍼 안이라 경계 밖 주소도 남의 메모리가 아니다
    SW_EXPECT_EQUAL( 0u, countCapturedAsserts( [&]
    {
        SW_EXPECT_EQUAL( 3, view[3] );
        SW_EXPECT_EQUAL( size_t( 4 ), view.first( 4 ).size() );
        SW_EXPECT_EQUAL( size_t( 0 ), view.last( 0 ).size() );
        SW_EXPECT_EQUAL( size_t( 0 ), view.subspan( 4 ).size() );
        SW_EXPECT_EQUAL( size_t( 2 ), view.subspan( 1, 2 ).size() );
    } ) );
    if constexpr ( kAssertsAreActive )
    {
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&view[4]; } ) );
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)view.first( 5 ); } ) );
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)view.subspan( 5 ); } ) );
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)view.subspan( 2, 3 ); } ) );
        const span<int32> emptyView( arrBuffer, size_t( 0 ) );
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&emptyView.front(); } ) );
    }
}

/**
 * @brief [BoundsCheckTest] vector — operator[] · at · back(용량 안쪽 칸만 가리킨다)
 */
SW_TEST_CASE( BoundsCheckTest, Vector )
{
    vector<int32> listValue;
    listValue.reserve( 8 );
    listValue.push_back( 1 );
    listValue.push_back( 2 );
    SW_EXPECT_EQUAL( 0u, countCapturedAsserts( [&]
    {
        SW_EXPECT_EQUAL( 2, listValue[1] );
        SW_EXPECT_EQUAL( 2, listValue.back() );
    } ) );
    if constexpr ( kAssertsAreActive )
    {
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&listValue[2]; } ) );
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&listValue.at( 5 ); } ) );
    }
}

/**
 * @brief [BoundsCheckTest] deque — operator[] · 끝 반복자 역참조(링은 마스크로 감겨 할당 안쪽을 가리킨다)
 */
SW_TEST_CASE( BoundsCheckTest, Deque )
{
    deque<int32> listValue;
    listValue.push_back( 1 );
    listValue.push_back( 2 );
    SW_EXPECT_EQUAL( 0u, countCapturedAsserts( [&]
    {
        SW_EXPECT_EQUAL( 2, listValue[1] );
        SW_EXPECT_EQUAL( 1, *listValue.begin() );
        SW_EXPECT_EQUAL( 2, listValue.begin()[1] );
    } ) );
    if constexpr ( kAssertsAreActive )
    {
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&listValue[2]; } ) );
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&*listValue.end(); } ) );
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&listValue.begin()[2]; } ) );
    }
}

/**
 * @brief [BoundsCheckTest] EnumArray — 끝 표식(`Count`)으로 색인
 */
SW_TEST_CASE( BoundsCheckTest, EnumArray )
{
    EnumArray<BoundsCheckColor, int32> arrValue{};
    SW_EXPECT_EQUAL( 0u, countCapturedAsserts( [&]
    { arrValue[BoundsCheckColor::Blue] = 7; } ) );
    SW_EXPECT_EQUAL( 7, arrValue[BoundsCheckColor::Blue] );
    if constexpr ( kAssertsAreActive )
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&arrValue[BoundsCheckColor::Count]; } ) );
}

/**
 * @brief [BoundsCheckTest] RingBuffer — 든 개수를 넘는 첨자(저장소 안쪽 칸)와 빈 버퍼의 front
 */
SW_TEST_CASE( BoundsCheckTest, RingBuffer )
{
    RingBuffer<int32, 4> ring;
    ring.push( 1 );
    SW_EXPECT_EQUAL( 0u, countCapturedAsserts( [&]
    { SW_EXPECT_EQUAL( 1, ring[0] ); } ) );
    if constexpr ( kAssertsAreActive )
    {
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&ring[1]; } ) );
        ring.clear();
        SW_EXPECT_EQUAL( 1u, countCapturedAsserts( [&]
        { (void)&ring.front(); } ) );
    }
}
