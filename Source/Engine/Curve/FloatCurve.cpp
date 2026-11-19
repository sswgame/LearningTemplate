#include "pch.h"

#include "Engine/Curve/FloatCurve.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct FloatCurveInternal
        {
            /** @brief 자동 접선의 한도 — 양옆 구간 기울기의 이 배수를 넘으면 단조 구간의 에르미트가 키 값 밖으로 넘친다(Fritsch-Carlson 의 α · β ≤ 3). */
            static constexpr float32 kAutoTangentSlopeLimit = 3.0f;
        };
    } // namespace

    float32 FloatCurveKey::interpolateTo( const FloatCurveKey& next, float32 time ) const
    {
        const float32 span = next._time - _time;
        if ( _interpolation == CurveInterpolation::Constant || span <= 0.0f )
            return _value;
        const float32 t = MathUtil::saturate( ( time - _time ) / span );
        if ( _interpolation == CurveInterpolation::Linear )
            return _value + ( next._value - _value ) * t;
        if ( _interpolation == CurveInterpolation::Eased )
        {
            // 블렌드 곡선은 정규화 시간을 받는다 — 스프링이 초를 쓰므로 길이는 구간 길이다.
            BlendCurveDef spec{};
            spec._curve    = _easing;
            spec._duration = span;
            return _value + ( next._value - _value ) * evaluateBlendWeight( spec, t );
        }
        // 에르미트 기저: h00 · p0 + h10 · dt · m0 + h01 · p1 + h11 · dt · m1
        const float32 t2  = t * t;
        const float32 t3  = t2 * t;
        const float32 h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        const float32 h10 = t3 - 2.0f * t2 + t;
        const float32 h01 = -2.0f * t3 + 3.0f * t2;
        const float32 h11 = t3 - t2;
        return h00 * _value + h10 * span * _leaveTangent + h01 * next._value + h11 * span * next._arriveTangent;
    }

    optional<float32> FloatCurve::evaluate( float32 time ) const
    {
        if ( _listKey.empty() )
            return nullopt;
        // `!( time > 첫 키 )` 로 묻는다 — NaN 시간은 어느 비교에도 지므로 `<=` · `>=` 둘을 빠져나가 아래 구간 찾기가 키 하나짜리 커브에서 범위 밖을 읽었다.
        if ( ( time > _listKey.front()._time ) == false )
            return _listKey.front()._value;
        if ( time >= _listKey.back()._time )
            return _listKey.back()._value;
        // time 보다 늦은 첫 키 — 그 앞 키와 구간을 이룬다(위 두 줄이 양 끝을 걸렀으므로 [1, 끝-1) 안에 있다).
        const auto next = std::upper_bound( _listKey.begin() + 1, _listKey.end() - 1, time, []( float32 value, const FloatCurveKey& key )
        { return value < key._time; } );
        return ( next - 1 )->interpolateTo( *next, time );
    }

    uint32 FloatCurve::addKey( float32 time, float32 value, CurveInterpolation interpolation )
    {
        FloatCurveKey key{};
        key._time          = time;
        key._value         = value;
        key._interpolation = interpolation;
        size_t index       = 0;
        while ( index < _listKey.size() && _listKey[index]._time <= time )
        {
            ++index;
        }
        _listKey.insert( _listKey.begin() + static_cast<ptrdiff_t>( index ), key );
        sortAndComputeAutoTangents();
        return static_cast<uint32>( index );
    }

    void FloatCurve::sortAndComputeAutoTangents()
    {
        VectorUtil::stableSort( _listKey, &FloatCurveKey::isBefore );
        const size_t keyCount = _listKey.size();
        for ( size_t index = 0; index < keyCount; ++index )
        {
            FloatCurveKey& key = _listKey[index];
            if ( key._bAutoTangent == false )
                continue;
            float32 slope{ 0.0f }; // 끝 키는 평평하다
            if ( index > 0 && index + 1 < keyCount )
            {
                const FloatCurveKey& previous   = _listKey[index - 1];
                const FloatCurveKey& next       = _listKey[index + 1];
                const float32        span       = next._time - previous._time;
                const float32        spanBefore = key._time - previous._time;
                const float32        spanAfter  = next._time - key._time;
                const float32        deltaIn    = spanBefore > 0.0f ? ( key._value - previous._value ) / spanBefore : 0.0f;
                const float32        deltaOut   = spanAfter > 0.0f ? ( next._value - key._value ) / spanAfter : 0.0f;
                // 극값(이웃 둘이 같은 쪽)이면 평평하다 — Catmull-Rom 기울기를 그대로 두면 키 너머로 넘친다.
                if ( span > 0.0f && deltaIn * deltaOut > 0.0f )
                {
                    slope               = ( next._value - previous._value ) / span;
                    const float32 limit = FloatCurveInternal::kAutoTangentSlopeLimit * MathUtil::min( MathUtil::abs( deltaIn ), MathUtil::abs( deltaOut ) );
                    slope               = MathUtil::clamp( slope, -limit, limit );
                }
            }
            key._arriveTangent = slope;
            key._leaveTangent  = slope;
        }
    }
} // namespace sw
