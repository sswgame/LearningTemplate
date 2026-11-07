/**
 * @file DepletingValue.h
 * @brief 깎여서 0 에 닿는 수치(체력 · 내구도 · 정신력)를 깎는 한 곳입니다. 칸은 그대로 `int32` · `float32` 입니다.
 * @details 칸을 감싸는 형식으로 두지 않습니다 — `_hp` 는 리플렉션 칸 · 상태 바이트 · 상수 비교에 그대로 쓰여, 형식을 바꾸면 세이브와 스키마가 흔들립니다. 깎는 규칙만 한 벌로 둡니다.
 *          언리얼 GAS 어트리뷰트의 고정(clamp) 자리이지만 수정자 층은 없습니다(기억에 의존). 실드 · 재생 · 기절까지 필요하면 `CombatHealth` 를 씁니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

#include <type_traits>

namespace sw
{
    /** @brief `DepletingValue::take` 한 번의 결과입니다. */
    template <typename T>
    struct DepletingValueResult
    {
        T     _taken;     ///< 실제로 깎인 양(0 이상, 깎기 전 값 이하)
        uint8 _bDepleted; ///< 깎은 뒤 값이 0 이하입니다
    };

    /** @brief 수치를 깎는 규칙입니다. 음수 양은 깎지 않고, 0 아래로 내려가지 않습니다. */
    struct DepletingValue
    {
        /** @param amount 양의 형식은 @p inoutValue 를 따릅니다(추론에서 빼 `take( _hp, 1.5f )` 같은 섞임을 그 자리의 변환 경고로 드러냅니다). */
        template <typename T>
        static DepletingValueResult<T> take( T& inoutValue, const std::common_type_t<T> amount )
        {
            static_assert( std::is_arithmetic_v<T>, "DepletingValue takes from an integer or floating-point field" );
            const T taken = MathUtil::max( T{ 0 }, MathUtil::min( amount, inoutValue ) );
            inoutValue -= taken;
            return DepletingValueResult<T>{ taken, inoutValue <= T{ 0 } ? SW_TRUE : SW_FALSE };
        }
    };
} // namespace sw
