/**
 * @file HashUtil.h
 * @brief 해시 · 씨앗을 섞는 상수(황금비 · splitmix64 · FNV-1a)와 도우미입니다.
 * @details 같은 상수를 파일마다 16 진 리터럴로 다시 적지 않는다(`CheckWellKnownConstants` 가 막는다). 값을 바꾸면 프로세스 밖으로 나가는
 *          해시(네트워크 비교 · 쿠킹 산출물)가 갈라지므로 여기 값은 바꾸지 않는다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct HashUtil
     * @brief 해시 섞기 상수와 함수입니다(전부 static constexpr).
     */
    struct HashUtil
    {
        /** @brief 2^64 / φ 입니다. 피보나치 해싱 · splitmix64 걸음 · 기본 씨앗이 씁니다. */
        static constexpr uint64 kGoldenRatio64 = 0x9E3779B97F4A7C15ull;
        /** @brief 2^32 / φ 입니다. 32 비트 씨앗 · 32 비트 결합이 씁니다. */
        static constexpr uint32 kGoldenRatio32 = 0x9E3779B9u;
        /** @brief splitmix64 마무리의 첫 곱수입니다. */
        static constexpr uint64 kSplitMixMultiplier0 = 0xBF58476D1CE4E5B9ull;
        /** @brief splitmix64 마무리의 둘째 곱수입니다. */
        static constexpr uint64 kSplitMixMultiplier1 = 0x94D049BB133111EBull;
        /** @brief FNV-1a 32 비트 기저값입니다. */
        static constexpr uint32 kFnvOffset32 = 2166136261u;
        /** @brief FNV-1a 32 비트 소수입니다. */
        static constexpr uint32 kFnvPrime32 = 16777619u;
        /** @brief FNV-1a 64 비트 기저값입니다. */
        static constexpr uint64 kFnvOffset64 = 14695981039346656037ull;
        /** @brief FNV-1a 64 비트 소수입니다. */
        static constexpr uint64 kFnvPrime64 = 1099511628211ull;

        /** @brief splitmix64 의 마무리 섞기입니다. 비트를 고르게 퍼뜨립니다. */
        [[nodiscard]] static constexpr uint64 mix64( uint64 value ) noexcept
        {
            value = ( value ^ ( value >> 30 ) ) * kSplitMixMultiplier0;
            value = ( value ^ ( value >> 27 ) ) * kSplitMixMultiplier1;
            return value ^ ( value >> 31 );
        }

        /** @brief @p seed 에 @p value 를 섞습니다(boost `hash_combine` 의 64 비트 판). */
        [[nodiscard]] static constexpr uint64 combine( const uint64 seed, const uint64 value ) noexcept
        {
            return seed ^ ( value + kGoldenRatio64 + ( seed << 6 ) + ( seed >> 2 ) );
        }
    };
} // namespace sw
