#include "pch.h"

#include "Core/Container/array.h"
#include "Core/Container/span.h"
#include "Core/Container/vector.h"

#include "TestFramework/TestFramework.h"

namespace
{
    int32 sumOf( sw::vector_reference<const int32> listValue )
    {
        int32 total = 0;
        for ( const int32 value : listValue )
            total += value;
        return total;
    }

    void fillWith( sw::vector_reference<int32> listValue, int32 value )
    {
        for ( int32& element : listValue )
            element = value;
    }

    struct SpanBase
    {
        int32 _value{ 0 };
    };

    struct SpanDerived : SpanBase
    {
        int32 _extra{ 0 };
    };

    // 막아야 하는 변환 — 컴파일 타임에 본다.
    static_assert( std::is_constructible_v<sw::span<const int32>, sw::vector<int32>&> );
    static_assert( std::is_constructible_v<sw::span<const int32>, const sw::vector<int32>&> );
    static_assert( std::is_constructible_v<sw::span<const int32>, sw::vector<int32>&&> ); // 임시는 const 뷰로만(인자 전달용)
    static_assert( std::is_constructible_v<sw::span<int32>, sw::small_vector<int32, 4>&> );
    static_assert( std::is_constructible_v<sw::span<const int32>, sw::span<int32>> );
    static_assert( std::is_constructible_v<sw::span<int32>, const sw::vector<int32>&> == false, "const 컨테이너로 쓰기 뷰를 만들면 안 된다" );
    static_assert( std::is_constructible_v<sw::span<int32>, sw::vector<int32>&&> == false, "임시 컨테이너로 쓰기 뷰를 만들면 안 된다" );
    static_assert( std::is_constructible_v<sw::span<int32>, sw::span<const int32>> == false, "const 뷰를 쓰기 뷰로 바꾸면 안 된다" );
    static_assert( std::is_constructible_v<sw::span<SpanBase>, sw::vector<SpanDerived>&> == false, "크기가 다른 원소로 걸으면 안 된다" );
    static_assert( std::is_trivially_copyable_v<sw::span<int32>> );
    static_assert( std::is_same_v<sw::vector_reference<const int32>, sw::span<const int32>>, "vector_reference 는 span 의 별칭이다" );
} // namespace

/**
 * @brief [SpanTest] vector_reference<const T> 인자 하나가 vector · small_vector · array · 표준 컨테이너 · C 배열 · 포인터 + 개수를 모두 받는다
 * @details `const vector<T>&` 로 받으면 `small_vector` 는 할당자가 달라 넘길 수 없다. 이것이 span 을 둔 이유다.
 */
SW_TEST_CASE( SpanTest, AcceptsEveryContiguousContainer )
{
    sw::vector<int32>          listValue{ 1, 2, 3 };
    sw::small_vector<int32, 4> listSmall;
    listSmall.push_back( 10 );
    listSmall.push_back( 20 );
    const sw::array<int32, 3> arrValue{ 100, 200, 300 };
    std::vector<int32>        listStd{ 5, 6 };
    std::array<int32, 2>      arrStd{ 7, 8 };
    const int32               arrRaw[3] = { 1000, 2000, 3000 };

    SW_EXPECT_EQUAL( 6, sumOf( listValue ) );
    SW_EXPECT_EQUAL( 30, sumOf( listSmall ) );
    SW_EXPECT_EQUAL( 600, sumOf( arrValue ) );
    SW_EXPECT_EQUAL( 11, sumOf( listStd ) );
    SW_EXPECT_EQUAL( 15, sumOf( arrStd ) );
    SW_EXPECT_EQUAL( 6000, sumOf( arrRaw ) );
    SW_EXPECT_EQUAL( 5, sumOf( { listValue.data() + 1, 2 } ) ); // 포인터 + 개수
    SW_EXPECT_EQUAL( 9, sumOf( sw::vector<int32>{ 4, 5 } ) );   // 임시 컨테이너(인자 전달)
    SW_EXPECT_EQUAL( 0, sumOf( {} ) );                          // 빈 뷰

    // 원소 타입을 적지 않아도 컨테이너에서 가져온다.
    sw::span deduced = listSmall;
    SW_EXPECT_EQUAL( size_t( 2 ), deduced.size() );
    SW_EXPECT_EQUAL( listSmall.data(), deduced.data() );
}

/**
 * @brief [SpanTest] vector_reference<T> 는 원소를 고쳐 쓰고, vector_reference<const T> 로 바뀐다 — 원소를 복사하지 않고 가리킨다
 */
SW_TEST_CASE( SpanTest, MutableSpanWritesThroughAndConvertsToConst )
{
    sw::small_vector<int32, 4> listSmall;
    listSmall.push_back( 1 );
    listSmall.push_back( 2 );
    sw::array<int32, 3> arrValue{ 0, 0, 0 };

    fillWith( listSmall, 7 );
    fillWith( arrValue, 3 );
    SW_EXPECT_EQUAL( 7, listSmall[0] );
    SW_EXPECT_EQUAL( 7, listSmall[1] );
    SW_EXPECT_EQUAL( 3, arrValue[2] );

    const sw::vector_reference<int32>       writable = arrValue;
    const sw::vector_reference<const int32> readOnly = writable;
    writable[1]                                      = 42;
    SW_EXPECT_EQUAL( 42, readOnly[1] );
    SW_EXPECT_EQUAL( arrValue.data(), readOnly.data() );
}

/**
 * @brief [SpanTest] 부분 뷰(first · last · subspan)와 앞 · 뒤 · 역순 · 바이트 크기
 */
SW_TEST_CASE( SpanTest, SubViewsAndAccessors )
{
    const sw::vector<int32>                 listValue{ 1, 2, 3, 4, 5 };
    const sw::vector_reference<const int32> whole = listValue;

    SW_EXPECT_EQUAL( size_t( 5 ), whole.size() );
    SW_EXPECT_EQUAL( size_t( 5 * sizeof( int32 ) ), whole.size_bytes() );
    SW_EXPECT_FALSE( whole.empty() );
    SW_EXPECT_EQUAL( 1, whole.front() );
    SW_EXPECT_EQUAL( 5, whole.back() );

    SW_EXPECT_EQUAL( 3, sumOf( whole.first( 2 ) ) );
    SW_EXPECT_EQUAL( 9, sumOf( whole.last( 2 ) ) );
    SW_EXPECT_EQUAL( 9, sumOf( whole.subspan( 1, 3 ) ) );
    SW_EXPECT_EQUAL( 12, sumOf( whole.subspan( 2 ) ) ); // 끝까지
    SW_EXPECT_TRUE( whole.subspan( 5 ).empty() );

    SW_EXPECT_EQUAL( 5, *whole.rbegin() );
    SW_EXPECT_EQUAL( 1, *( whole.rend() - 1 ) );
}
