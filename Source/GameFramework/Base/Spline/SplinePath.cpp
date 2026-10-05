#include "pch.h"

#include "GameFramework/Base/Spline/SplinePath.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Spline/ArcLengthUtil.h"

namespace sw
{
    namespace
    {
        struct SplinePathInternal
        {
            static constexpr float32 kMinTangentLengthSq = 1.0e-12f;

            static float3 evaluateCatmullRom( const float3& p0, const float3& p1, const float3& p2, const float3& p3, float32 t )
            {
                const float32 t2 = t * t;
                const float32 t3 = t2 * t;
                return ( p1 * 2.0f + ( p2 - p0 ) * t + ( p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3 ) * t2 + ( p1 * 3.0f - p0 - p2 * 3.0f + p3 ) * t3 ) * 0.5f;
            }

            static float3 evaluateCatmullRomDerivative( const float3& p0, const float3& p1, const float3& p2, const float3& p3, float32 t )
            {
                return ( ( p2 - p0 ) + ( p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3 ) * ( 2.0f * t ) + ( p1 * 3.0f - p0 - p2 * 3.0f + p3 ) * ( 3.0f * t * t ) ) * 0.5f;
            }

            static float3 evaluateBezier( const float3& p0, const float3& p1, const float3& p2, const float3& p3, float32 t )
            {
                const float32 u = 1.0f - t;
                return p0 * ( u * u * u ) + p1 * ( 3.0f * u * u * t ) + p2 * ( 3.0f * u * t * t ) + p3 * ( t * t * t );
            }

            static float3 evaluateBezierDerivative( const float3& p0, const float3& p1, const float3& p2, const float3& p3, float32 t )
            {
                const float32 u = 1.0f - t;
                return ( p1 - p0 ) * ( 3.0f * u * u ) + ( p2 - p1 ) * ( 6.0f * u * t ) + ( p3 - p2 ) * ( 3.0f * t * t );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SplinePath::SplinePath()
        : _listControlPoint{}
        , _listTablePoint{}
        , _length{ 0.0f }
        , _segmentCount{ 0 }
        , _type{ SplineType::CatmullRom }
        , _bClosed{ SW_FALSE }
    {
    }

    void SplinePath::clear()
    {
        _listControlPoint.clear();
        _listTablePoint.clear();
        _length       = 0.0f;
        _segmentCount = 0;
    }

    bool SplinePath::initialize( const vector<float3>& listControlPoint, SplineType type, bool bClosed, int32 samplesPerSegment )
    {
        clear();
        _type             = type;
        _bClosed          = bClosed ? SW_TRUE : SW_FALSE;
        const int32 count = static_cast<int32>( listControlPoint.size() );
        int32       segmentCount{ 0 };
        switch ( type )
        {
            case SplineType::CatmullRom:
            case SplineType::Linear:
            {
                segmentCount = count < 2 ? 0 : ( bClosed ? count : count - 1 );
                break;
            }
            case SplineType::Bezier:
            {
                const bool bOpenShape   = bClosed == false && count >= 4 && ( count - 1 ) % 3 == 0;
                const bool bClosedShape = bClosed && count >= 3 && count % 3 == 0;
                segmentCount            = bOpenShape ? ( count - 1 ) / 3 : ( bClosedShape ? count / 3 : 0 );
                break;
            }
        }
        if ( segmentCount <= 0 )
            return false;

        _listControlPoint       = listControlPoint;
        _segmentCount           = segmentCount;
        const int32 sampleCount = MathUtil::max( 1, samplesPerSegment );
        const int32 tableCount  = segmentCount * sampleCount + ( bClosed ? 0 : 1 );
        _listTablePoint.reserve( static_cast<size_t>( tableCount ) );
        for ( int32 tableIndex = 0; tableIndex < tableCount; ++tableIndex )
        {
            TablePoint point;
            point._parameter = static_cast<float32>( tableIndex ) / static_cast<float32>( sampleCount );
            point._position  = evaluate( point._parameter );
            if ( _listTablePoint.empty() == false )
                _length += float3::getDistance( _listTablePoint.back()._position, point._position );
            point._distance = _length;
            _listTablePoint.push_back( point );
        }
        if ( bClosed )
            _length += float3::getDistance( _listTablePoint.back()._position, _listTablePoint.front()._position );
        return true;
    }

    float3 SplinePath::getControlPointWrapped( int32 index ) const
    {
        const int32 count = static_cast<int32>( _listControlPoint.size() );
        if ( _bClosed == SW_TRUE )
            return _listControlPoint[static_cast<size_t>( ( ( index % count ) + count ) % count )];
        // 열린 Catmull-Rom 의 바깥 이웃은 끝 점을 거울로 비춘 점이다(끝에서 곡선이 직선으로 빠진다).
        if ( index < 0 )
            return _listControlPoint[0] * 2.0f - _listControlPoint[1];
        if ( index >= count )
            return _listControlPoint[static_cast<size_t>( count - 1 )] * 2.0f - _listControlPoint[static_cast<size_t>( count - 2 )];
        return _listControlPoint[static_cast<size_t>( index )];
    }

    void SplinePath::splitParameter( float32 parameter, int32& outSegment, float32& outT ) const
    {
        const float32 clamped = MathUtil::clamp( parameter, 0.0f, static_cast<float32>( _segmentCount ) );
        outSegment            = MathUtil::min( static_cast<int32>( clamped ), _segmentCount - 1 );
        outT                  = clamped - static_cast<float32>( outSegment );
    }

    float3 SplinePath::evaluate( float32 parameter ) const
    {
        if ( _segmentCount <= 0 )
            return float3{};
        int32   segment{ 0 };
        float32 t{ 0.0f };
        splitParameter( parameter, segment, t );
        switch ( _type )
        {
            case SplineType::CatmullRom:
            {
                return SplinePathInternal::evaluateCatmullRom( getControlPointWrapped( segment - 1 ), getControlPointWrapped( segment ), getControlPointWrapped( segment + 1 ),
                                                               getControlPointWrapped( segment + 2 ), t );
            }
            case SplineType::Bezier:
            {
                const int32 base = segment * 3;
                return SplinePathInternal::evaluateBezier( getControlPointWrapped( base ), getControlPointWrapped( base + 1 ), getControlPointWrapped( base + 2 ),
                                                           getControlPointWrapped( base + 3 ), t );
            }
            case SplineType::Linear:
            {
                return float3::lerp( getControlPointWrapped( segment ), getControlPointWrapped( segment + 1 ), t );
            }
        }
        return float3{};
    }

    float3 SplinePath::evaluateDerivative( float32 parameter ) const
    {
        if ( _segmentCount <= 0 )
            return float3{ 0.0f, 0.0f, 1.0f };
        int32   segment{ 0 };
        float32 t{ 0.0f };
        splitParameter( parameter, segment, t );
        switch ( _type )
        {
            case SplineType::CatmullRom:
            {
                return SplinePathInternal::evaluateCatmullRomDerivative( getControlPointWrapped( segment - 1 ), getControlPointWrapped( segment ),
                                                                         getControlPointWrapped( segment + 1 ), getControlPointWrapped( segment + 2 ), t );
            }
            case SplineType::Bezier:
            {
                const int32 base = segment * 3;
                return SplinePathInternal::evaluateBezierDerivative( getControlPointWrapped( base ), getControlPointWrapped( base + 1 ), getControlPointWrapped( base + 2 ),
                                                                     getControlPointWrapped( base + 3 ), t );
            }
            case SplineType::Linear:
            {
                return getControlPointWrapped( segment + 1 ) - getControlPointWrapped( segment );
            }
        }
        return float3{ 0.0f, 0.0f, 1.0f };
    }

    float32 SplinePath::wrapDistance( float32 distance ) const { return ArcLengthUtil::wrapDistance( distance, _length, _bClosed == SW_TRUE ); }

    float32 SplinePath::findParameterAtDistance( float32 wrappedDistance ) const
    {
        const ArcLengthSpan span      = ArcLengthUtil::findSpan( _listTablePoint, &TablePoint::_distance, wrappedDistance, _length, _bClosed == SW_TRUE );
        const float32       fromParam = _listTablePoint[span._index]._parameter;
        // 닫힌 곡선의 마지막 구간은 첫 점(매개변수 0)이 아니라 한 바퀴 끝(구간 수)으로 간다.
        const bool    bClosingSpan = span._nextIndex == 0 && span._index != 0;
        const float32 toParam      = bClosingSpan ? static_cast<float32>( _segmentCount ) : _listTablePoint[span._nextIndex]._parameter;
        return fromParam + ( toParam - fromParam ) * span._alpha;
    }

    SplineSample SplinePath::sampleAtDistance( float32 distance ) const
    {
        SplineSample sample;
        if ( isValid() == false )
            return sample;
        const float32 wrapped   = wrapDistance( distance );
        const float32 parameter = findParameterAtDistance( wrapped );
        sample._distance        = wrapped;
        sample._position        = evaluate( parameter );
        const float3 derivative = evaluateDerivative( parameter );
        sample._tangent         = derivative.getLengthSquared() > SplinePathInternal::kMinTangentLengthSq ? derivative.normalize() : float3{ 0.0f, 0.0f, 1.0f };
        return sample;
    }

    SplineSample SplinePath::findClosest( const float3& point ) const
    {
        SplineSample best;
        if ( isValid() == false )
            return best;
        const size_t tableCount   = _listTablePoint.size();
        const size_t spanCount    = _bClosed == SW_TRUE ? tableCount : tableCount - 1;
        float32      bestDistSq   = -1.0f;
        float32      bestParam    = 0.0f;
        float32      bestDistance = 0.0f;
        for ( size_t spanIndex = 0; spanIndex < spanCount; ++spanIndex )
        {
            const TablePoint& from      = _listTablePoint[spanIndex];
            const bool        bClosing  = spanIndex + 1 == tableCount;
            const TablePoint& to        = _listTablePoint[bClosing ? 0 : spanIndex + 1];
            const float32     toParam   = bClosing ? static_cast<float32>( _segmentCount ) : to._parameter;
            const float32     toDist    = bClosing ? _length : to._distance;
            const float3      chord     = to._position - from._position;
            const float32     chordSq   = chord.getLengthSquared();
            const float32     alpha     = chordSq > SplinePathInternal::kMinTangentLengthSq ? MathUtil::saturate( ( point - from._position ).dot( chord ) / chordSq ) : 0.0f;
            const float3      projected = from._position + chord * alpha;
            const float32     distSq    = float3::getDistanceSquared( projected, point );
            if ( bestDistSq >= 0.0f && distSq >= bestDistSq )
                continue;
            bestDistSq   = distSq;
            bestParam    = from._parameter + ( toParam - from._parameter ) * alpha;
            bestDistance = from._distance + ( toDist - from._distance ) * alpha;
        }
        best._position          = evaluate( bestParam );
        best._distance          = wrapDistance( bestDistance );
        const float3 derivative = evaluateDerivative( bestParam );
        best._tangent           = derivative.getLengthSquared() > SplinePathInternal::kMinTangentLengthSq ? derivative.normalize() : float3{ 0.0f, 0.0f, 1.0f };
        return best;
    }

    void SplinePath::sampleUniform( float32 spacing, vector<SplineSample>& outListSample ) const
    {
        outListSample.clear();
        if ( isValid() == false || spacing <= 0.0f || _length <= 0.0f )
            return;
        const int32 stepCount = static_cast<int32>( _length / spacing );
        outListSample.reserve( static_cast<size_t>( stepCount ) + 2 );
        for ( int32 stepIndex = 0; stepIndex <= stepCount; ++stepIndex )
        {
            const float32 distance = static_cast<float32>( stepIndex ) * spacing;
            if ( _bClosed == SW_TRUE && distance >= _length )
                break;
            outListSample.push_back( sampleAtDistance( distance ) );
        }
        const bool bNeedsEnd = _bClosed == SW_FALSE && ( outListSample.empty() || _length - outListSample.back()._distance > spacing * 1.0e-3f );
        if ( bNeedsEnd )
            outListSample.push_back( sampleAtDistance( _length ) );
    }
} // namespace sw
