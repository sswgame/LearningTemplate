/**
 * @file NetSequence.h
 * @brief 감기는 16 비트 시퀀스의 비교를 정의합니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

namespace sw
{
    /**
     * @struct NetSequence
     * @brief 16비트 시퀀스를 비교합니다. 65535 다음에 0으로 감겨도 "더 새 것"을 맞게 고릅니다(반 바퀴 안의 차이를 앞으로 봅니다).
     */
    struct NetSequence
    {
        static constexpr int32 kRange     = 65536;      ///< 한 바퀴(16 비트 값의 수)
        static constexpr int32 kHalfRange = kRange / 2; ///< 이보다 작은 차이를 앞으로 본다

        static constexpr bool isGreater( uint16 lhs, uint16 rhs )
        {
            return ( lhs > rhs && lhs - rhs <= kHalfRange ) || ( lhs < rhs && rhs - lhs > kHalfRange );
        }
        static constexpr bool isLess( uint16 lhs, uint16 rhs ) { return isGreater( rhs, lhs ); }
        /** @brief @p lhs - @p rhs 를 감김을 고려한 부호 있는 차이로 반환합니다. */
        static constexpr int32 computeDifference( uint16 lhs, uint16 rhs )
        {
            const int32 difference = static_cast<int32>( lhs ) - static_cast<int32>( rhs );
            if ( difference > kHalfRange )
                return difference - kRange;
            if ( difference < -kHalfRange )
                return difference + kRange;
            return difference;
        }
    };
} // namespace sw
