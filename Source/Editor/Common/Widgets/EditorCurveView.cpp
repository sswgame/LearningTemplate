#include "pch.h"

#include "Editor/Common/Widgets/EditorCurveView.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Utility/FloatCurve.h"

#include <cmath>

namespace sw::editor
{
    namespace
    {
        struct EditorCurveViewInternal
        {
            /** @brief 범위가 0 으로 무너지지 않게 하는 가장 작은 폭입니다. */
            static constexpr float32 kMinSpan = 1e-4f;

            static float32 getSpan( float32 minValue, float32 maxValue ) { return MathUtil::max( maxValue - minValue, kMinSpan ); }
        };
    } // namespace

    float2 EditorCurveView::toScreen( float32 time, float32 value ) const
    {
        const float32 timeSpan  = EditorCurveViewInternal::getSpan( _timeMin, _timeMax );
        const float32 valueSpan = EditorCurveViewInternal::getSpan( _valueMin, _valueMax );
        const float32 x         = _frameMin._x + ( time - _timeMin ) / timeSpan * _frameSize._x;
        const float32 y         = _frameMin._y + ( 1.0f - ( value - _valueMin ) / valueSpan ) * _frameSize._y;
        return float2{ x, y };
    }

    void EditorCurveView::toCurve( const float2& screen, float32& outTime, float32& outValue ) const
    {
        const float32 width  = MathUtil::max( _frameSize._x, 1.0f );
        const float32 height = MathUtil::max( _frameSize._y, 1.0f );
        outTime              = _timeMin + ( screen._x - _frameMin._x ) / width * EditorCurveViewInternal::getSpan( _timeMin, _timeMax );
        outValue             = _valueMin + ( 1.0f - ( screen._y - _frameMin._y ) / height ) * EditorCurveViewInternal::getSpan( _valueMin, _valueMax );
    }

    void EditorCurveView::fitToCurve( const FloatCurve& curve, float32 paddingRatio )
    {
        float32 valueMin{ 0.0f };
        float32 valueMax{ 1.0f };
        if ( curve.computeValueRange( valueMin, valueMax ) == false )
        {
            _timeMin  = 0.0f;
            _timeMax  = 1.0f;
            _valueMin = 0.0f;
            _valueMax = 1.0f;
            return;
        }
        float32 timeMin = curve._listKey.front()._time;
        float32 timeMax = curve._listKey.back()._time;
        // 키 하나 · 평평한 커브는 폭이 0 이다 — 단위 폭을 둘러 둔다.
        if ( timeMax - timeMin < EditorCurveViewInternal::kMinSpan )
        {
            timeMin -= 0.5f;
            timeMax += 0.5f;
        }
        if ( valueMax - valueMin < EditorCurveViewInternal::kMinSpan )
        {
            valueMin -= 0.5f;
            valueMax += 0.5f;
        }
        const float32 timePadding  = ( timeMax - timeMin ) * paddingRatio;
        const float32 valuePadding = ( valueMax - valueMin ) * paddingRatio;
        _timeMin                   = timeMin - timePadding;
        _timeMax                   = timeMax + timePadding;
        _valueMin                  = valueMin - valuePadding;
        _valueMax                  = valueMax + valuePadding;
    }

    uint32 EditorCurveView::findKeyAt( const FloatCurve& curve, const float2& screen, float32 radius ) const
    {
        uint32  bestIndex    = invalid_index::kUint32;
        float32 bestDistance = radius * radius;
        for ( size_t index = 0; index < curve._listKey.size(); ++index )
        {
            const FloatCurveKey& key      = curve._listKey[index];
            const float32        distance = float2::getDistanceSquared( toScreen( key._time, key._value ), screen );
            if ( distance > bestDistance )
                continue;
            bestDistance = distance;
            bestIndex    = static_cast<uint32>( index );
        }
        return bestIndex;
    }

    float2 EditorCurveView::computeTangentHandle( const FloatCurveKey& key, bool bLeave, float32 lengthPixels ) const
    {
        // 화면에서 접선 방향: 시간 한 단위 → (픽셀/시간, -기울기 × 픽셀/값). 그 방향으로 lengthPixels 만큼.
        const float32 pixelsPerTime  = _frameSize._x / EditorCurveViewInternal::getSpan( _timeMin, _timeMax );
        const float32 pixelsPerValue = _frameSize._y / EditorCurveViewInternal::getSpan( _valueMin, _valueMax );
        const float32 slope          = bLeave ? key._leaveTangent : key._arriveTangent;
        float32       directionX     = pixelsPerTime;
        float32       directionY     = -slope * pixelsPerValue;
        const float32 length         = std::sqrt( directionX * directionX + directionY * directionY );
        if ( length <= 0.0f )
            return toScreen( key._time, key._value );
        const float32 sign  = bLeave ? 1.0f : -1.0f;
        directionX          = directionX / length * lengthPixels * sign;
        directionY          = directionY / length * lengthPixels * sign;
        const float2 center = toScreen( key._time, key._value );
        return float2{ center._x + directionX, center._y + directionY };
    }

    float32 EditorCurveView::computeTangentFromHandle( const FloatCurveKey& key, const float2& handle ) const
    {
        float32 handleTime{ 0.0f };
        float32 handleValue{ 0.0f };
        toCurve( handle, handleTime, handleValue );
        const float32 deltaTime  = handleTime - key._time;
        const float32 deltaValue = handleValue - key._value;
        // 손잡이가 키와 같은 세로줄이면 기울기가 끝없다 — 큰 값으로 묶는다(언리얼 커브 편집기도 수직 접선을 막는다).
        constexpr float32 kMaxSlope = 1e4f;
        if ( MathUtil::abs( deltaTime ) < EditorCurveViewInternal::kMinSpan )
            return deltaValue >= 0.0f ? kMaxSlope : -kMaxSlope;
        return MathUtil::clamp( deltaValue / deltaTime, -kMaxSlope, kMaxSlope );
    }

    void EditorCurveView::zoomAt( const float2& screen, float32 factor )
    {
        if ( factor <= 0.0f )
            return;
        float32 pivotTime{ 0.0f };
        float32 pivotValue{ 0.0f };
        toCurve( screen, pivotTime, pivotValue );
        _timeMin  = pivotTime + ( _timeMin - pivotTime ) / factor;
        _timeMax  = pivotTime + ( _timeMax - pivotTime ) / factor;
        _valueMin = pivotValue + ( _valueMin - pivotValue ) / factor;
        _valueMax = pivotValue + ( _valueMax - pivotValue ) / factor;
    }

    void EditorCurveView::pan( const float2& delta )
    {
        const float32 timeShift  = -delta._x / MathUtil::max( _frameSize._x, 1.0f ) * EditorCurveViewInternal::getSpan( _timeMin, _timeMax );
        const float32 valueShift = delta._y / MathUtil::max( _frameSize._y, 1.0f ) * EditorCurveViewInternal::getSpan( _valueMin, _valueMax );
        _timeMin += timeShift;
        _timeMax += timeShift;
        _valueMin += valueShift;
        _valueMax += valueShift;
    }

    float32 EditorCurveView::computeGridStep( float32 span, float32 pixels, float32 minPixels )
    {
        if ( span <= 0.0f || pixels <= 0.0f || minPixels <= 0.0f )
            return 1.0f;
        const float32 rawStep = span * minPixels / pixels;
        const float32 power   = std::pow( 10.0f, std::floor( std::log10( rawStep ) ) );
        for ( const float32 multiplier : { 1.0f, 2.0f, 5.0f, 10.0f } )
        {
            if ( power * multiplier >= rawStep )
                return power * multiplier;
        }
        return power * 10.0f;
    }
} // namespace sw::editor
