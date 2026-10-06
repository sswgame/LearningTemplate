/**
 * @file DestructionRandom.h
 * @brief 씨앗이 같으면 어느 플랫폼 · 구성에서도 같은 수열을 내는 난수(splitmix64)입니다. 파쇄 씨앗점 · 파편 흩기가 씁니다.
 * @details 표준 분포(`std::uniform_real_distribution`)는 구현마다 수열이 달라 쓰지 않습니다 — 파괴를 네트워크로 맞출 때 씨앗과 피해 사건만
 *          보내므로, 같은 씨앗이 모든 기계에서 같은 조각 · 같은 흩기를 내야 합니다. 정수 연산과 2 의 거듭제곱 나눗셈만 씁니다.
 */
#pragma once
#include "Core/Common/HashUtil.h"
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    /** @brief splitmix64 수열입니다. 씨앗 0 도 쓸 수 있습니다(고정점이 없다). */
    struct DestructionRandom
    {
        uint64 _state;

        explicit DestructionRandom( uint64 seed )
            : _state{ seed }
        {
        }

        /** @brief 다음 64 비트 값입니다. */
        uint64 nextUint64()
        {
            _state += HashUtil::kGoldenRatio64;
            uint64 value = _state;
            return HashUtil::mix64( value );
        }

        /** @brief [0, 1) 의 실수입니다(위 24 비트 — float32 가 정확히 담는다). */
        float32 nextFloat01() { return static_cast<float32>( nextUint64() >> 40 ) * ( 1.0f / 16777216.0f ); }

        /** @brief [@p low, @p high) 의 실수입니다. */
        float32 nextRange( float32 low, float32 high ) { return low + ( high - low ) * nextFloat01(); }

        /** @brief 단위 구 안의 고른 점입니다(거절 표집 — 반복 횟수도 씨앗이 정하므로 결정적이다). */
        float3 nextPointInUnitSphere()
        {
            for ( ;; )
            {
                const float3 point{ nextRange( -1.0f, 1.0f ), nextRange( -1.0f, 1.0f ), nextRange( -1.0f, 1.0f ) };
                if ( point.getLengthSquared() <= 1.0f )
                    return point;
            }
        }

        /** @brief 두 값을 섞은 씨앗입니다(오브젝트 씨앗 × 사건 번호 → 그 사건의 흩기 씨앗). */
        static uint64 combineSeed( uint64 seed, uint64 value )
        {
            DestructionRandom random{ seed ^ ( value * 0xD6E8FEB86659FD93ull ) };
            return random.nextUint64();
        }
    };
} // namespace sw
