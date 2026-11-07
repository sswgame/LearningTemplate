// constant::invalid_index — 이름 붙은 '없음' 표지가 그 자리의 리터럴 -1 과 같은 값 · 같은 변환을 하는지(리터럴을 표지 이름으로 바꾼 묶음의 근거).
#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Testing/TestFramework.h"

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
 * @details 바꾼 자리에는 int8 · int16 · int64 칸과 부호 없는 비교가 섞여 있다. 상수 식이라 좁히기 중괄호 초기화도 리터럴처럼 통과한다.
 */
SW_TEST_CASE( ConstantsTest, InvalidIndexConvertsLikeLiteral )
{
    const int8  narrow{ constant::invalid_index::kInt32 };
    const int16 middle{ constant::invalid_index::kInt32 };
    const int64 wide = constant::invalid_index::kInt32;
    SW_EXPECT_EQUAL( static_cast<int8>( -1 ), narrow );
    SW_EXPECT_EQUAL( static_cast<int16>( -1 ), middle );
    SW_EXPECT_EQUAL( static_cast<int64>( -1 ), wide );
    SW_EXPECT_EQUAL( constant::invalid_index::kUint32, static_cast<uint32>( constant::invalid_index::kInt32 ) );
    SW_EXPECT_EQUAL( constant::invalid_index::kUint64, static_cast<uint64>( constant::invalid_index::kInt64 ) );
}
