#include "pch.h"

#include "Core/Container/EnumArray.h"
#include "Core/TestFramework/TestFramework.h"

namespace
{
    enum class EnumArrayColor : uint8
    {
        Red,
        Green,
        Blue,
        Count
    };

    enum class EnumArrayNoCount : uint8
    {
        First,
        Second,
    };
} // namespace

template <>
struct sw::EnumCount<EnumArrayNoCount>
{
    static constexpr size_t kValue = 2;
};

namespace
{
    constexpr auto kArrColorName = sw::makeEnumTable<EnumArrayColor, const utf8*>( "Red", "Green", "Blue" );

    struct EnumArrayColorRow
    {
        const utf8* _pName;
        uint32      _rgb;
    };
    constexpr auto kArrColorRow = sw::makeEnumTable<EnumArrayColor, EnumArrayColorRow>( {
        {  "Red", 0xFF0000u},
        {"Green", 0x00FF00u},
        { "Blue", 0x0000FFu},
    } );
    static_assert( kArrColorRow[EnumArrayColor::Green]._rgb == 0x00FF00u, "중괄호 목록 표도 열거자로 읽는다" );

    static_assert( sw::kEnumCount<EnumArrayColor> == 3, "끝 열거자 Count 가 열거자 수다" );
    static_assert( sw::kEnumCount<EnumArrayNoCount> == 2, "Count 가 없으면 특수화가 준다" );
    static_assert( sw::EnumArray<EnumArrayColor, int32>::size() == 3 );
    static_assert( std::is_aggregate_v<sw::EnumArray<EnumArrayColor, int32>> == false, "모자란 중괄호 목록으로 채울 수 없어야 한다" );
    static_assert( std::is_constructible_v<sw::EnumArray<EnumArrayColor, int32>, int32, int32> == false, "나열 채우기는 makeEnumTable 로만" );
    static_assert( kArrColorName[EnumArrayColor::Blue][0] == 'B', "constexpr 표를 열거자로 읽는다" );
} // namespace

/**
 * @brief [EnumArrayTest] 열거자로 색인하고, 기본값은 값 초기화이며, 범위 for 가 열거자 순서로 돈다
 */
SW_TEST_CASE( EnumArrayTest, IndexesByEnumerator )
{
    sw::EnumArray<EnumArrayColor, int32> arrCount;
    for ( const int32 value : arrCount )
    {
        SW_EXPECT_EQUAL( 0, value );
    }
    arrCount[EnumArrayColor::Green] = 7;
    SW_EXPECT_EQUAL( 7, arrCount.data()[1] );

    arrCount.fill( 3 );
    int32 total{ 0 };
    for ( const int32 value : arrCount )
    {
        total += value;
    }
    SW_EXPECT_EQUAL( 9, total );

    sw::EnumArray<EnumArrayNoCount, float32> arrNoCount;
    arrNoCount[EnumArrayNoCount::Second] = 1.5f;
    SW_EXPECT_EQUAL( size_t( 2 ), arrNoCount.size() );
    SW_EXPECT_EQUAL( 1.5f, arrNoCount[EnumArrayNoCount::Second] );
}

/**
 * @brief [EnumArrayTest] EnumRange 는 열거자를 0 부터 Count 앞까지 차례로 돈다
 */
SW_TEST_CASE( EnumArrayTest, EnumRangeVisitsEveryEnumerator )
{
    sw::EnumArray<EnumArrayColor, int32> arrOrder;
    int32                                order{ 0 };
    for ( const EnumArrayColor color : sw::EnumRange<EnumArrayColor>{} )
    {
        arrOrder[color] = ++order;
    }
    SW_EXPECT_EQUAL( 3, order );
    SW_EXPECT_EQUAL( 1, arrOrder[EnumArrayColor::Red] );
    SW_EXPECT_EQUAL( 3, arrOrder[EnumArrayColor::Blue] );
}

/**
 * @brief [EnumArrayTest] makeEnumTable 의 항목이 열거자 순서로 들어간다
 */
SW_TEST_CASE( EnumArrayTest, MakeEnumTableKeepsOrder )
{
    SW_EXPECT_TRUE( sw::string_view( kArrColorName[EnumArrayColor::Red] ) == "Red" );
    SW_EXPECT_TRUE( sw::string_view( kArrColorName[EnumArrayColor::Green] ) == "Green" );
    SW_EXPECT_TRUE( sw::string_view( kArrColorName[EnumArrayColor::Blue] ) == "Blue" );
}
