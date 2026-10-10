#include "pch.h"

#include "Engine/Utility/FloatCurve.h"

#include "Core/Math/MathUtil.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        struct FloatCurveInternal
        {
            /** @brief 표본 수 — 범위 계산이 키 사이 곡선의 넘침을 잡는 데 쓴다. */
            static constexpr uint32 kRangeSampleCount = 64;

            static bool isKeyBefore( const FloatCurveKey& left, const FloatCurveKey& right ) { return left._time < right._time; }

            /** @brief 구간 [@p from, @p to] 안의 @p time 값입니다. */
            static float32 interpolate( const FloatCurveKey& from, const FloatCurveKey& to, float32 time )
            {
                const float32 span = to._time - from._time;
                if ( from._interpolation == CurveInterpolation::Constant || span <= 0.0f )
                    return from._value;
                const float32 t = MathUtil::saturate( ( time - from._time ) / span );
                if ( from._interpolation == CurveInterpolation::Linear )
                    return from._value + ( to._value - from._value ) * t;
                // 에르미트 기저: h00 · p0 + h10 · dt · m0 + h01 · p1 + h11 · dt · m1
                const float32 t2  = t * t;
                const float32 t3  = t2 * t;
                const float32 h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
                const float32 h10 = t3 - 2.0f * t2 + t;
                const float32 h01 = -2.0f * t3 + 3.0f * t2;
                const float32 h11 = t3 - t2;
                return h00 * from._value + h10 * span * from._leaveTangent + h01 * to._value + h11 * span * to._arriveTangent;
            }
        };
    } // namespace

    float32 FloatCurve::evaluate( float32 time, float32 fallback ) const
    {
        if ( _listKey.empty() )
            return fallback;
        if ( time <= _listKey.front()._time )
            return _listKey.front()._value;
        if ( time >= _listKey.back()._time )
            return _listKey.back()._value;
        // 이분 탐색: time 보다 늦은 첫 키 — 그 앞 키와 구간을 이룬다(위 두 줄이 양 끝을 걸렀다).
        size_t low  = 1;
        size_t high = _listKey.size() - 1;
        while ( low < high )
        {
            const size_t middle = ( low + high ) / 2;
            if ( _listKey[middle]._time <= time )
                low = middle + 1;
            else
                high = middle;
        }
        return FloatCurveInternal::interpolate( _listKey[low - 1], _listKey[low], time );
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
        std::stable_sort( _listKey.begin(), _listKey.end(), &FloatCurveInternal::isKeyBefore );
        const size_t keyCount = _listKey.size();
        for ( size_t index = 0; index < keyCount; ++index )
        {
            FloatCurveKey& key = _listKey[index];
            if ( key._bAutoTangent == false )
                continue;
            float32 slope{ 0.0f }; // 끝 키는 평평하다
            if ( index > 0 && index + 1 < keyCount )
            {
                const FloatCurveKey& previous = _listKey[index - 1];
                const FloatCurveKey& next     = _listKey[index + 1];
                const float32        span     = next._time - previous._time;
                slope                         = span > 0.0f ? ( next._value - previous._value ) / span : 0.0f;
            }
            key._arriveTangent = slope;
            key._leaveTangent  = slope;
        }
    }

    bool FloatCurve::computeValueRange( float32& outMin, float32& outMax ) const
    {
        if ( _listKey.empty() )
            return false;
        outMin = _listKey.front()._value;
        outMax = outMin;
        for ( const FloatCurveKey& key : _listKey )
        {
            outMin = MathUtil::min( outMin, key._value );
            outMax = MathUtil::max( outMax, key._value );
        }
        const float32 startTime = _listKey.front()._time;
        const float32 span      = _listKey.back()._time - startTime;
        if ( span <= 0.0f )
            return true;
        for ( uint32 sampleIndex = 0; sampleIndex <= FloatCurveInternal::kRangeSampleCount; ++sampleIndex )
        {
            const float32 time  = startTime + span * static_cast<float32>( sampleIndex ) / static_cast<float32>( FloatCurveInternal::kRangeSampleCount );
            const float32 value = evaluate( time );
            outMin              = MathUtil::min( outMin, value );
            outMax              = MathUtil::max( outMax, value );
        }
        return true;
    }
} // namespace sw
