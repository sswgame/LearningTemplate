/**
 * @file DeterministicMathUtil.h
 * @brief 같은 입력이면 구성(Debug · Release · Shipping)과 컴파일러 옵션을 넘어 같은 비트를 내는 삼각함수입니다 — 롤백 · 결정적 시뮬레이션 TU 가 libm 대신 씁니다.
 * @details `MathUtil::sin` 등은 libm(`::sinf`)이라 CRT 판 · 벡터화 판 · 상수 접기에 따라 끝 비트가 달라질 수 있다. 여기 함수는 IEEE 기본 연산(+ − × ÷ sqrt floor)만
 *          쓰는 다항식 · 급수이고 줄 밖(.cpp, 축약을 끈 `sw_markDeterministicSources`)에 있어 부르는 TU 의 옵션이 계산을 바꾸지 못한다(VectorMath 와 같은 이유).
 *          범위는 같은 플랫폼 안의 구성 간 결정성이다 — 플랫폼 간 비트 결정성은 보장하지 않는다(결정 D7-8). 정확도는 float32 결과 기준 libm 과 1 ulp 안이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 결정적 삼각함수입니다(전부 static, 줄 밖 — 무한대 입력은 libm 과 달리 NaN 이다). 렌더 · 애니메이션은 `MathUtil` 을 그대로 쓴다. */
    struct SW_API DeterministicMathUtil
    {
        /** @brief sin 입니다. 무한대 · NaN 이면 NaN 입니다. */
        [[nodiscard]] static float32 sin( float32 radian ) noexcept;
        /** @brief cos 입니다. 무한대 · NaN 이면 NaN 입니다. */
        [[nodiscard]] static float32 cos( float32 radian ) noexcept;
        /** @brief acos 입니다(라디안, [0, π]). [-1, 1] 밖이거나 NaN 이면 NaN 입니다. */
        [[nodiscard]] static float32 acos( float32 x ) noexcept;
        /** @brief atan2 입니다(라디안, [-π, π], 0 의 부호 규칙은 libm 과 같다). 무한대 · NaN 이 있으면 NaN 입니다. */
        [[nodiscard]] static float32 atan2( float32 y, float32 x ) noexcept;
    };
} // namespace sw
