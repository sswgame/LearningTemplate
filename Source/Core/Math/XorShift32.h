/**
 * @file XorShift32.h
 * @brief 결정적 난수 xorshift32(13 · 17 · 5)의 한 걸음과 단위 구간 변환입니다 — 상태 하나(uint32)를 부르는 쪽이 들고 있습니다.
 * @details 같은 씨앗이면 어느 플랫폼 · 구성에서도 같은 수열이라 시험 · 리플레이 · 절차 생성이 되풀이됩니다. 정수 연산뿐입니다.
 *          씨앗 0 은 고정점(0 만 계속 나온다)이라 부르는 쪽이 0 이 아닌 씨앗을 줍니다. 64 비트 수열은 `HashUtil::splitMix64Next` 입니다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief xorshift32 의 걸음 · 변환입니다(전부 static). */
    struct XorShift32
    {
        /** @brief 다음 상태입니다(그대로 난수 값이기도 합니다). */
        [[nodiscard]] static constexpr uint32 next( uint32 state ) noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        /** @brief 위 24 비트로 만든 [0, 1) 실수입니다(float32 가 정확히 담는다). */
        [[nodiscard]] static constexpr float32 toUnit24( uint32 value ) noexcept { return static_cast<float32>( value >> 8 ) * ( 1.0f / 16777216.0f ); }
    };
} // namespace sw
