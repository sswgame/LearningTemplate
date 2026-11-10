// constant::invalid_index — 이름 붙은 '없음' 표지가 그 자리의 리터럴 -1 과 같은 값 · 같은 변환을 하는지(리터럴을 표지 이름으로 바꾼 묶음의 근거).
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Testing/TestFramework.h"

#include <type_traits>

using namespace sw;

/**
 * @brief [ConstantsTest] 부호 있는 표지는 -1, 부호 없는 표지는 모든 비트가 1 이다
 * @details 코드의 `-1` 을 `constant::invalid_index::kInt32` 로 바꾼 자리(필드 초기값 · 찾지 못함 반환 · 이벤트 대상 없음)가
 *          저장 · 복제되는 바이트를 바꾸지 않으려면 값이 같아야 한다.
 */
SW_TEST_CASE( ConstantsTest, InvalidIndexEqualsMinusOne )
{
    static_assert( constant::invalid_index::kInt8 == -1 && constant::invalid_index::kInt16 == -1 );
    static_assert( constant::invalid_index::kInt32 == -1 && constant::invalid_index::kInt64 == -1 );
    static_assert( constant::invalid_index::kUint8 == 0xFFu && constant::invalid_index::kUint16 == 0xFFFFu );
    static_assert( constant::invalid_index::kUint32 == 0xFFFFFFFFu && constant::invalid_index::kUint64 == ~0ull );
    SW_EXPECT_EQUAL( -1, constant::invalid_index::kInt32 );
}

/**
 * @brief [ConstantsTest] int32 표지는 다른 정수 칸에 넣어도 리터럴 -1 과 같은 값이 된다
 * @details 바꾼 자리에는 int8 · int16 · int64 칸과 부호 없는 비교가 섞여 있다. 표지는 따로 된 형식(`constant::InvalidIndex`)이라 좁히는 칸에는 형식을 적어 바꾼다.
 */
SW_TEST_CASE( ConstantsTest, InvalidIndexConvertsLikeLiteral )
{
    const int8  narrow = static_cast<int8>( constant::invalid_index::kInt32 );
    const int16 middle = static_cast<int16>( constant::invalid_index::kInt32 );
    const int64 wide   = constant::invalid_index::kInt32;
    SW_EXPECT_EQUAL( static_cast<int8>( -1 ), narrow );
    SW_EXPECT_EQUAL( static_cast<int16>( -1 ), middle );
    SW_EXPECT_EQUAL( static_cast<int64>( -1 ), wide );
    SW_EXPECT_EQUAL( constant::invalid_index::kUint32, static_cast<uint32>( constant::invalid_index::kInt32 ) );
    SW_EXPECT_EQUAL( constant::invalid_index::kUint64, static_cast<uint64>( constant::invalid_index::kInt64 ) );
}

/**
 * @brief [ConstantsTest] 표지는 정수로는 바뀌지만 optional · expected 의 값으로는 들어가지 못한다
 * @details `optional<int32>` 를 돌려주는 찾기가 못 찾으면 `kInt32` 를 돌려줘 `_list[-1]` 로 죽었다(SkillTreeState::findIndex). 표지를 따로 된 형식으로 두고
 *          `expected` 가 그 생성 · 대입을 지워, 같은 모양(반환 · 인자 · 대입 · 비교)이 컴파일 오류가 된다.
 */
SW_TEST_CASE( ConstantsTest, InvalidIndexIsNotAnOptionalValue )
{
    using TMarker = std::decay_t<decltype( constant::invalid_index::kInt32 )>;
    static_assert( std::is_convertible_v<TMarker, int32> );
    static_assert( std::is_convertible_v<TMarker, optional<int32>> == false );
    static_assert( std::is_constructible_v<optional<int32>, TMarker> == false );
    static_assert( std::is_assignable_v<optional<int32>&, TMarker> == false );
    static_assert( std::is_convertible_v<std::decay_t<decltype( constant::invalid_index::kUint32 )>, expected<uint32>> == false );
    static_assert( std::is_convertible_v<int32, optional<int32>> ); // 정수 값은 그대로 들어간다
    const optional<int32> none = nullopt;
    SW_EXPECT_FALSE( none.has_value() );
}
