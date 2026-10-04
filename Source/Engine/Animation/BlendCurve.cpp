#include "pch.h"

#include "Engine/Animation/BlendCurve.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct BlendCurveInternal
        {
            static constexpr float32 kTwoPi = MathUtil::Pi * 2.0f;

            /** @brief 0 에서 출발해 1 로 다가가는 스프링의 자리입니다(속도 0 출발, 감쇠비 ≥ 1 이라 넘치지 않는다). */
            static float32 computeSpringPosition( float32 seconds, float32 angularFrequency, float32 dampingRatio )
            {
                const float32 omegaTime = angularFrequency * seconds;
                const float32 root      = MathUtil::sqrt( MathUtil::max( 0.0f, dampingRatio * dampingRatio - 1.0f ) );
                if ( root < 1.0e-3f )
                    return 1.0f - ( 1.0f + omegaTime ) * ::expf( -omegaTime );
                // 과감쇠 — 두 실근 r1 · r2 의 지수 합.
                const float32 rootSlow = -( dampingRatio - root );
                const float32 rootFast = -( dampingRatio + root );
                const float32 decay    = ( rootFast * ::expf( rootSlow * omegaTime ) - rootSlow * ::expf( rootFast * omegaTime ) ) / ( rootFast - rootSlow );
                return 1.0f - decay;
            }

            static float32 evaluateSpring( const BlendCurveSpec& spec, float32 normalizedTime )
            {
                const float32 angularFrequency = MathUtil::max( 0.01f, spec._springFrequency ) * kTwoPi;
                const float32 dampingRatio     = MathUtil::max( 1.0f, spec._springDamping );
                const float32 endPosition      = computeSpringPosition( spec._duration, angularFrequency, dampingRatio );
                if ( endPosition < 1.0e-6f )
                    return normalizedTime;
                // 끝의 자리로 나눠 1 에서 1 — 스프링이 다 붙기 전에 블렌드가 끝나도 튀지 않는다.
                return computeSpringPosition( normalizedTime * spec._duration, angularFrequency, dampingRatio ) / endPosition;
            }

            static float32 evaluateCustom( const vector<BlendCurveKey>& listKey, float32 normalizedTime )
            {
                if ( listKey.empty() )
                    return normalizedTime;
                if ( normalizedTime <= listKey.front()._time )
                    return listKey.front()._value;
                for ( size_t index = 1; index < listKey.size(); ++index )
                {
                    const BlendCurveKey& next = listKey[index];
                    if ( normalizedTime > next._time )
                        continue;
                    const BlendCurveKey& previous = listKey[index - 1];
                    const float32        span     = next._time - previous._time;
                    if ( span <= MathUtil::Epsilon )
                        return next._value;
                    return MathUtil::lerp( previous._value, next._value, ( normalizedTime - previous._time ) / span );
                }
                return listKey.back()._value;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    float32 evaluateBlendWeight( const BlendCurveSpec& spec, float32 normalizedTime )
    {
        const float32 time     = MathUtil::saturate( normalizedTime );
        const float32 exponent = MathUtil::max( 0.01f, spec._exponent );
        switch ( spec._curve )
        {
            case BlendCurve::Cut:
            {
                return time > 0.0f ? 1.0f : 0.0f;
            }
            case BlendCurve::Linear:
            {
                return time;
            }
            case BlendCurve::EaseIn:
            {
                return MathUtil::pow( time, exponent );
            }
            case BlendCurve::EaseOut:
            {
                return 1.0f - MathUtil::pow( 1.0f - time, exponent );
            }
            case BlendCurve::EaseInOut:
            {
                if ( time < 0.5f )
                    return 0.5f * MathUtil::pow( time * 2.0f, exponent );
                return 1.0f - 0.5f * MathUtil::pow( 2.0f - time * 2.0f, exponent );
            }
            case BlendCurve::SmoothStep:
            {
                return time * time * ( 3.0f - 2.0f * time );
            }
            case BlendCurve::Cubic:
            {
                if ( time < 0.5f )
                    return 4.0f * time * time * time;
                const float32 remaining = 2.0f - time * 2.0f;
                return 1.0f - remaining * remaining * remaining * 0.5f;
            }
            case BlendCurve::Exponential:
            {
                return ( 1.0f - ::expf( -exponent * time ) ) / ( 1.0f - ::expf( -exponent ) );
            }
            case BlendCurve::Spring:
            {
                return BlendCurveInternal::evaluateSpring( spec, time );
            }
            case BlendCurve::Custom:
            {
                return BlendCurveInternal::evaluateCustom( spec._listCustomKey, time );
            }
        }
        return time;
    }
} // namespace sw
