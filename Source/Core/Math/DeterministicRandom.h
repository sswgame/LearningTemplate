/**
 * @file DeterministicRandom.h
 * @brief 씨앗이 같으면 어느 플랫폼 · 구성에서도 같은 수열을 내는 결정적 난수 둘 — `DeterministicRandom32`(xorshift32) · `DeterministicRandom64`(splitmix64)입니다.
 * @details 테스트 · 리플레이 · 절차 생성 · 네트워크 동기화가 같은 결과를 되풀이하게 합니다. 정수 연산과 2 의 거듭제곱 나눗셈만 씁니다
 *          (`<random>` 의 분포는 표준 라이브러리마다 수열이 달라 쓰지 않는다). 상태를 손으로 들고 `XorShift32::next` · `splitMix64Next` 를
 *          부르면 [0, 1) 변환과 범위 정수가 호출부마다 달라지므로 이 둘을 씁니다. 도메인 도우미(가중치 고르기 · 섞기 · 단위 구 점)는
 *          이것을 상속하는 쪽(`GameRandom` · `DestructionRandom`)에 둡니다. 비밀 · 겹치면 안 되는 값은 `SecureRandom` 입니다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/XorShift32.h"

namespace sw
{
    /**
     * @struct DeterministicRandom32
     * @brief xorshift32(13 · 17 · 5) 수열입니다. 상태가 32 비트라 저장 · 복원이 싸고 게임 규칙이 씁니다.
     * @details 씨앗 0 은 고정점(0 만 계속 나온다)이라 **거절합니다** — 모든 구성에서 단언 오류를 남기고 상태를 바꾸지 않습니다.
     *          조용히 다른 씨앗으로 바꾸면 "씨앗 0" 이라는 데이터 오류가 같은 수열을 내는 두 씨앗으로 숨어 버립니다.
     */
    struct DeterministicRandom32
    {
        /** @brief 씨앗을 주지 않은 수열의 시작 상태입니다. */
        static constexpr uint32 kDefaultSeed = HashUtil::kGoldenRatio32;

        DeterministicRandom32() = default;

        /** @brief @p seed 로 시작합니다. 0 은 거절합니다(`setSeed`). */
        explicit DeterministicRandom32( uint32 seed ) { setSeed( seed ); }

        /** @brief 씨앗을 다시 둡니다. 0 이면 단언 오류를 남기고 상태를 그대로 둡니다. */
        void setSeed( uint32 seed )
        {
            SW_LOW_LEVEL_ASSERT( seed != 0, "DeterministicRandom32 seed 0 is the xorshift fixed point - give a non-zero seed" );
            if ( seed != 0 )
                _state = seed;
        }

        /** @brief 지금 상태입니다. 0 이 아니고, 그대로 `setSeed` 에 넣으면 수열이 이어집니다. */
        [[nodiscard]] uint32 getState() const { return _state; }

        /** @brief 다음 32 비트 값입니다. */
        uint32 nextUint()
        {
            _state = XorShift32::next( _state );
            return _state;
        }

        /** @brief [0, 1) 의 실수입니다. 위 24 비트를 씁니다(float32 가 정확히 담는다). */
        float32 nextFloat() { return XorShift32::toUnit24( nextUint() ); }

        /** @brief [@p minValue, @p maxValue) 의 실수입니다. */
        float32 nextRange( float32 minValue, float32 maxValue ) { return minValue + ( maxValue - minValue ) * nextFloat(); }

        /** @brief [@p minValue, @p maxValue] 의 정수입니다(양 끝 포함). 곱셈 위 32 비트를 써서 나머지 연산의 치우침이 없습니다. */
        int32 nextInt( int32 minValue, int32 maxValue )
        {
            if ( maxValue <= minValue )
                return minValue;
            const uint32 span = static_cast<uint32>( maxValue - minValue ) + 1u;
            return minValue + static_cast<int32>( static_cast<uint64>( nextUint() ) * span >> 32 );
        }

        /** @brief 확률 @p probability 로 true 입니다. */
        bool nextChance( float32 probability ) { return nextFloat() < probability; }

    private:
        uint32 _state{ kDefaultSeed };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct DeterministicRandom64
     * @brief splitmix64 수열입니다. 64 비트 씨앗(오브젝트 씨앗 · 사건 번호를 섞은 값)을 그대로 받는 엔진 시스템(파괴 · 오디오)이 씁니다.
     * @details splitmix64 는 고정점이 없어 씨앗 0 도 정상 씨앗입니다(32 비트 쪽과 달리 거절하지 않는다).
     */
    struct DeterministicRandom64
    {
        /** @brief 씨앗을 주지 않은 수열의 시작 상태입니다. */
        static constexpr uint64 kDefaultSeed = HashUtil::kGoldenRatio64;

        DeterministicRandom64() = default;

        explicit DeterministicRandom64( uint64 seed )
            : _state{ seed }
        {
        }

        /** @brief 씨앗을 다시 둡니다. */
        void setSeed( uint64 seed ) { _state = seed; }

        /** @brief 지금 상태입니다. 그대로 `setSeed` 에 넣으면 수열이 이어집니다. */
        [[nodiscard]] uint64 getState() const { return _state; }

        /** @brief 다음 64 비트 값입니다. */
        uint64 nextUint64() { return HashUtil::splitMix64Next( _state ); }

        /** @brief [0, 1) 의 실수입니다. 위 24 비트를 씁니다(float32 가 정확히 담는다). */
        float32 nextFloat() { return static_cast<float32>( nextUint64() >> 40 ) * ( 1.0f / 16777216.0f ); }

        /** @brief [@p minValue, @p maxValue) 의 실수입니다. */
        float32 nextRange( float32 minValue, float32 maxValue ) { return minValue + ( maxValue - minValue ) * nextFloat(); }

        /** @brief [@p minValue, @p maxValue] 의 정수입니다(양 끝 포함). 위 32 비트와 곱셈 위 32 비트로 고릅니다. */
        int32 nextInt( int32 minValue, int32 maxValue )
        {
            if ( maxValue <= minValue )
                return minValue;
            const uint32 span = static_cast<uint32>( maxValue - minValue ) + 1u;
            return minValue + static_cast<int32>( ( nextUint64() >> 32 ) * span >> 32 );
        }

    private:
        uint64 _state{ kDefaultSeed };
    };
} // namespace sw
