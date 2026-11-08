/**
 * @file ShadingMath.h
 * @brief 셰이딩 모델 스칼라 식(`shading_math.hlsli`)을 C++ 에서 그대로 부르게 하는 다리입니다.
 * @details 셰이더 파일 하나를 두 언어가 읽는다 — 테스트가 셰이더가 아닌 사본을 재지 않게(`vfx_simulate.hlsli` 가 숫자 계약을 나누는 것과 같은 길, 이쪽은 함수까지).
 *          셰이더 파일이 쓰는 HLSL 내장 함수를 같은 이름으로 `sw::shadermath` 에 두고 include 한다. 내장 함수를 더 쓰려면 여기에 한 줄을 더한다
 *          (빠뜨리면 C++ 컴파일이 멈추므로 조용히 갈라지지 않는다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/MathUtil.h"

namespace sw::shadermath
{
    [[nodiscard]] inline float32 saturate( float32 value )
    {
        return MathUtil::saturate( value );
    }
    [[nodiscard]] inline float32 lerp( float32 from, float32 to, float32 t )
    {
        return from + ( to - from ) * t;
    }
    [[nodiscard]] inline float32 max( float32 a, float32 b )
    {
        return a > b ? a : b;
    }
    [[nodiscard]] inline float32 min( float32 a, float32 b )
    {
        return a < b ? a : b;
    }
    [[nodiscard]] inline float32 clamp( float32 value, float32 low, float32 high )
    {
        return min( max( value, low ), high );
    }
    [[nodiscard]] inline float32 frac( float32 value )
    {
        return value - std::floor( value );
    }
    using std::abs;
    using std::cos;
    using std::exp2;
    using std::floor;
    using std::log2;
    using std::pow;
    using std::sqrt;
} // namespace sw::shadermath

#include "shading_math.hlsli"
