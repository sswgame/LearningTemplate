#include "pch.h"

#include "Engine/Animation/Graph/BlendSpace.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct BlendSpaceInternal
        {
            static constexpr float64 kDegenerateDeterminant     = 1.0e-12;  /**< 세 점이 한 줄로 보이는 외접원 행렬식 크기입니다(정규화 공간). */
            static constexpr float64 kInCircleTolerance         = 1.0e-9;   /**< 외접원 "확실히 안쪽" 여유입니다 — 한 원 위 네 점(격자)에서 겹치는 삼각형을 막는다. */
            static constexpr float64 kSuperTriangleNear         = -1000.0;  /**< 모든 점을 품는 큰 삼각형의 가까운 좌표입니다. */
            static constexpr float64 kSuperTriangleFar          = 3000.0;   /**< 모든 점을 품는 큰 삼각형의 먼 좌표입니다. */
            static constexpr float32 kCoincidentDistanceSquared = 1.0e-10f; /**< 표본 위로 보는 정규화 제곱 거리입니다. */
            static constexpr float32 kInsideTolerance           = -1.0e-5f; /**< 두 삼각형이 맞닿은 변 위의 점이 반올림으로 양쪽에서 다 빠지지 않게 하는 무게중심 여유입니다. */

            /** @brief 삼각분할용 점입니다. 굽기는 한 번뿐이라 외접원 판정의 반올림을 줄이려 float64 로 계산합니다. */
            struct DelaunayPoint
            {
                float64 _x;
                float64 _y;
            };

            /** @brief 굽는 중의 삼각형입니다. 외접원(중심 · 반지름²)을 함께 듭니다. */
            struct DelaunayTriangle
            {
                uint32  _a;
                uint32  _b;
                uint32  _c;
                float64 _centerX;
                float64 _centerY;
                float64 _radiusSquared;
            };

            struct DelaunayEdge
            {
                uint32 _a;
                uint32 _b;
            };

            /** @brief 1D 가중치입니다. 표본은 x 오름차순입니다. */
            static void computeLinearWeights( const vector<BlendSpaceSample>& listSample, float32 parameter, vector<float32>& outListWeight )
            {
                const size_t count = listSample.size();
                if ( parameter <= listSample.front()._position._x )
                {
                    outListWeight.front() = 1.0f;
                    return;
                }
                if ( parameter >= listSample.back()._position._x )
                {
                    outListWeight.back() = 1.0f;
                    return;
                }
                for ( size_t index = 0; index + 1 < count; ++index )
                {
                    const float32 lower = listSample[index]._position._x;
                    const float32 upper = listSample[index + 1]._position._x;
                    if ( lower <= parameter && parameter <= upper )
                    {
                        const float32 span       = upper - lower;
                        const float32 alpha      = ( span > MathUtil::kEpsilon ) ? ( parameter - lower ) / span : 0.0f;
                        outListWeight[index]     = 1.0f - alpha;
                        outListWeight[index + 1] = alpha;
                        return;
                    }
                }
                outListWeight.back() = 1.0f;
            }

            /** @brief 세 점의 삼각형과 외접원을 만듭니다. 세 점이 한 줄이면 거짓입니다. */
            static bool makeTriangle( const vector<DelaunayPoint>& listPoint, uint32 a, uint32 b, uint32 c, DelaunayTriangle& outTriangle )
            {
                const DelaunayPoint& pa              = listPoint[a];
                const DelaunayPoint& pb              = listPoint[b];
                const DelaunayPoint& pc              = listPoint[c];
                const float64        halfDeterminant = pa._x * ( pb._y - pc._y ) + pb._x * ( pc._y - pa._y ) + pc._x * ( pa._y - pb._y );
                const float64        determinant     = halfDeterminant + halfDeterminant;
                if ( MathUtil::abs( determinant ) < kDegenerateDeterminant )
                    return false;
                const float64 lengthA = pa._x * pa._x + pa._y * pa._y;
                const float64 lengthB = pb._x * pb._x + pb._y * pb._y;
                const float64 lengthC = pc._x * pc._x + pc._y * pc._y;
                const float64 centerX = ( lengthA * ( pb._y - pc._y ) + lengthB * ( pc._y - pa._y ) + lengthC * ( pa._y - pb._y ) ) / determinant;
                const float64 centerY = ( lengthA * ( pc._x - pb._x ) + lengthB * ( pa._x - pc._x ) + lengthC * ( pb._x - pa._x ) ) / determinant;
                const float64 radiusX = pa._x - centerX;
                const float64 radiusY = pa._y - centerY;
                outTriangle           = DelaunayTriangle{ a, b, c, centerX, centerY, radiusX * radiusX + radiusY * radiusY };
                return true;
            }

            /** @brief 지운 삼각형들의 변 가운데 둘이 함께 쓰지 않은 변(빈 구멍의 테두리)만 @p outListBoundary 에 남깁니다. */
            static void collectBoundary( const vector<DelaunayEdge>& listBadEdge, vector<DelaunayEdge>& outListBoundary )
            {
                outListBoundary.clear();
                for ( size_t edgeIndex = 0; edgeIndex < listBadEdge.size(); ++edgeIndex )
                {
                    const DelaunayEdge& edge    = listBadEdge[edgeIndex];
                    bool                bShared = false;
                    for ( size_t otherIndex = 0; otherIndex < listBadEdge.size() && bShared == false; ++otherIndex )
                    {
                        const DelaunayEdge& other = listBadEdge[otherIndex];
                        bShared                   = otherIndex != edgeIndex && ( ( other._a == edge._a && other._b == edge._b ) || ( other._a == edge._b && other._b == edge._a ) );
                    }
                    if ( bShared == false )
                        outListBoundary.push_back( edge );
                }
            }

            /**
             * @brief 보이어-왓슨으로 들로네 삼각분할을 굽습니다. @p listPoint 의 끝 셋은 모든 점을 품는 큰 삼각형이고, 그 꼭짓점이 닿은 삼각형은 버립니다.
             * @details 외접원 판정은 "안쪽이 확실할 때만" 이라 격자(네 점이 한 원 위)에서도 겹치는 삼각형이 생기지 않는다.
             *          표본 수가 수십 개라 O(n²) 로 충분하다 — 굽기는 그래프를 읽을 때 한 번이다.
             */
            static void triangulate( const vector<DelaunayPoint>& listPoint, uint32 sampleCount, vector<BlendSpaceTriangle>& outListTriangle )
            {
                outListTriangle.clear();
                DelaunayTriangle superTriangle{};
                if ( makeTriangle( listPoint, sampleCount, sampleCount + 1, sampleCount + 2, superTriangle ) == false )
                    return;
                vector<DelaunayTriangle> listTriangle{ superTriangle };
                vector<DelaunayTriangle> listKept;
                vector<DelaunayEdge>     listBadEdge;
                vector<DelaunayEdge>     listBoundary;
                for ( uint32 pointIndex = 0; pointIndex < sampleCount; ++pointIndex )
                {
                    const DelaunayPoint& point = listPoint[pointIndex];
                    listKept.clear();
                    listBadEdge.clear();
                    for ( const DelaunayTriangle& triangle : listTriangle )
                    {
                        const float64 deltaX = point._x - triangle._centerX;
                        const float64 deltaY = point._y - triangle._centerY;
                        if ( deltaX * deltaX + deltaY * deltaY < triangle._radiusSquared - kInCircleTolerance )
                        {
                            listBadEdge.push_back( DelaunayEdge{ triangle._a, triangle._b } );
                            listBadEdge.push_back( DelaunayEdge{ triangle._b, triangle._c } );
                            listBadEdge.push_back( DelaunayEdge{ triangle._c, triangle._a } );
                            continue;
                        }
                        listKept.push_back( triangle );
                    }
                    collectBoundary( listBadEdge, listBoundary );
                    for ( const DelaunayEdge& edge : listBoundary )
                    {
                        DelaunayTriangle triangle{};
                        if ( makeTriangle( listPoint, edge._a, edge._b, pointIndex, triangle ) )
                            listKept.push_back( triangle );
                    }
                    listTriangle.swap( listKept );
                }
                for ( const DelaunayTriangle& triangle : listTriangle )
                {
                    if ( triangle._a >= sampleCount || triangle._b >= sampleCount || triangle._c >= sampleCount )
                        continue;
                    outListTriangle.push_back( BlendSpaceTriangle{ triangle._a, triangle._b, triangle._c } );
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BlendSpace::BlendSpace()
        : _listSample{}
        , _listTriangle{}
        , _axisOrigin{ 0.0f, 0.0f }
        , _axisInverseRange{ 0.0f, 0.0f }
        , _axisCount{ 1 }
    {
    }

    void BlendSpace::setAxisCount( uint32 axisCount )
    {
        _axisCount = ( axisCount == 2 ) ? 2u : 1u;
        rebuildTriangulation();
    }

    void BlendSpace::addSample( const float2& position, const hashed_string& clipName )
    {
        _listSample.push_back( BlendSpaceSample{ position, clipName } );
        if ( _axisCount == 1 )
        {
            VectorUtil::stableSort( _listSample, []( const BlendSpaceSample& lhs, const BlendSpaceSample& rhs )
            {
                return lhs._position._x < rhs._position._x;
            } );
        }
        rebuildTriangulation();
    }

    void BlendSpace::clear()
    {
        _listSample.clear();
        _listTriangle.clear();
    }

    void BlendSpace::rebuildTriangulation()
    {
        _listTriangle.clear();
        if ( _axisCount != 2 || _listSample.size() < 3 )
            return;
        float2 minPosition = _listSample.front()._position;
        float2 maxPosition = _listSample.front()._position;
        for ( const BlendSpaceSample& sample : _listSample )
        {
            minPosition = float2::min( minPosition, sample._position );
            maxPosition = float2::max( maxPosition, sample._position );
        }
        const float32 rangeX = maxPosition._x - minPosition._x;
        const float32 rangeY = maxPosition._y - minPosition._y;
        if ( rangeX <= MathUtil::kEpsilon || rangeY <= MathUtil::kEpsilon )
            return; // 한 축으로 늘어선 표본은 넓이가 없다
        // 들로네는 축 비율에 따라 고르는 대각선이 달라진다 — 언리얼처럼 축마다 정규화해 단위가 큰 축이 지배하지 않게 한다.
        // 범위는 표본 최솟값 ~ 최댓값이다(에셋에 축 범위 칸이 없다). 무게중심 좌표 자체는 축 비율과 무관하다.
        _axisOrigin       = minPosition;
        _axisInverseRange = float2{ 1.0f / rangeX, 1.0f / rangeY };

        using Internal                              = BlendSpaceInternal;
        const uint32                    sampleCount = static_cast<uint32>( _listSample.size() );
        vector<Internal::DelaunayPoint> listPoint;
        listPoint.reserve( sampleCount + 3 );
        for ( uint32 sample = 0; sample < sampleCount; ++sample )
        {
            const float2 normalized = toNormalized( _listSample[sample]._position );
            listPoint.push_back( Internal::DelaunayPoint{ normalized._x, normalized._y } );
        }
        // 정규화 공간은 [0, 1]² 다. 큰 삼각형이 가까우면 바깥 변 근처의 납작한 삼각형이 빠지므로 멀리 둔다.
        listPoint.push_back( Internal::DelaunayPoint{ Internal::kSuperTriangleNear, Internal::kSuperTriangleNear } );
        listPoint.push_back( Internal::DelaunayPoint{ Internal::kSuperTriangleFar, Internal::kSuperTriangleNear } );
        listPoint.push_back( Internal::DelaunayPoint{ Internal::kSuperTriangleNear, Internal::kSuperTriangleFar } );
        Internal::triangulate( listPoint, sampleCount, _listTriangle );
    }

    float2 BlendSpace::toNormalized( const float2& position ) const
    {
        return float2{ ( position._x - _axisOrigin._x ) * _axisInverseRange._x, ( position._y - _axisOrigin._y ) * _axisInverseRange._y };
    }

    void BlendSpace::computeTriangleWeights( const float2& parameter, vector<float32>& outListWeight ) const
    {
        // 언리얼 Blend Space(들로네 삼각분할 + 무게중심) · Godot BlendSpace2D(삼각형, 바깥은 가장 가까운 변)를 따른다.
        // 유니티 Freeform Cartesian · Directional 은 경사 대역 가중이라 모든 표본이 켜질 수 있어, 샘플 수 상한(셋)을 위해 택하지 않는다.
        SW_ASSERT( hasArea() ); // 표본이 둘 이상인 2D 는 그래프 로드가 넓이를 요구한다
        if ( hasArea() == false )
            return;
        const float2 point = toNormalized( parameter );

        // 표본 위면 그 표본 하나다(무게중심의 반올림으로 이웃이 아주 조금 켜지지 않게).
        for ( size_t sample = 0; sample < _listSample.size(); ++sample )
        {
            if ( float2::getDistanceSquared( point, toNormalized( _listSample[sample]._position ) ) < BlendSpaceInternal::kCoincidentDistanceSquared )
            {
                outListWeight[sample] = 1.0f;
                return;
            }
        }
        for ( const BlendSpaceTriangle& triangle : _listTriangle )
        {
            const float2  a          = toNormalized( _listSample[triangle._sampleA]._position );
            const float2  b          = toNormalized( _listSample[triangle._sampleB]._position );
            const float2  c          = toNormalized( _listSample[triangle._sampleC]._position );
            const float32 doubleArea = ( b._x - a._x ) * ( c._y - a._y ) - ( c._x - a._x ) * ( b._y - a._y );
            const float32 weightA    = ( ( b._x - point._x ) * ( c._y - point._y ) - ( c._x - point._x ) * ( b._y - point._y ) ) / doubleArea;
            const float32 weightB    = ( ( c._x - point._x ) * ( a._y - point._y ) - ( a._x - point._x ) * ( c._y - point._y ) ) / doubleArea;
            const float32 weightC    = 1.0f - weightA - weightB;
            if ( weightA < BlendSpaceInternal::kInsideTolerance || weightB < BlendSpaceInternal::kInsideTolerance || weightC < BlendSpaceInternal::kInsideTolerance )
                continue;
            const float32 clampedA           = MathUtil::max( weightA, 0.0f );
            const float32 clampedB           = MathUtil::max( weightB, 0.0f );
            const float32 clampedC           = MathUtil::max( weightC, 0.0f );
            const float32 inverseTotal       = 1.0f / ( clampedA + clampedB + clampedC );
            outListWeight[triangle._sampleA] = clampedA * inverseTotal;
            outListWeight[triangle._sampleB] = clampedB * inverseTotal;
            outListWeight[triangle._sampleC] = clampedC * inverseTotal;
            return;
        }
        // 삼각분할 바깥 — 가장 가까운 변 위의 점으로 옮긴다(안쪽 변도 보지만 바깥 점에 가장 가까운 것은 늘 바깥 변이다).
        float32 bestDistanceSquared = MathUtil::kMaxFloat;
        uint32  bestFrom            = 0;
        uint32  bestTo              = 0;
        float32 bestAlpha           = 0.0f;
        for ( const BlendSpaceTriangle& triangle : _listTriangle )
        {
            const uint32 arrEdgeFrom[3] = { triangle._sampleA, triangle._sampleB, triangle._sampleC };
            const uint32 arrEdgeTo[3]   = { triangle._sampleB, triangle._sampleC, triangle._sampleA };
            for ( uint32 edge = 0; edge < 3; ++edge )
            {
                const float2  from = toNormalized( _listSample[arrEdgeFrom[edge]]._position );
                const float2  to   = toNormalized( _listSample[arrEdgeTo[edge]]._position );
                const float2  span{ to._x - from._x, to._y - from._y };
                const float32 alpha           = MathUtil::saturate( ( ( point._x - from._x ) * span._x + ( point._y - from._y ) * span._y ) / ( span._x * span._x + span._y * span._y ) );
                const float32 distanceSquared = float2::getDistanceSquared( point, float2::lerp( from, to, alpha ) );
                if ( distanceSquared < bestDistanceSquared )
                {
                    bestDistanceSquared = distanceSquared;
                    bestFrom            = arrEdgeFrom[edge];
                    bestTo              = arrEdgeTo[edge];
                    bestAlpha           = alpha;
                }
            }
        }
        outListWeight[bestFrom] = 1.0f - bestAlpha;
        outListWeight[bestTo]   = bestAlpha;
    }

    void BlendSpace::computeSampleWeights( const float2& parameter, vector<float32>& outListWeight ) const
    {
        outListWeight.assign( _listSample.size(), 0.0f );
        if ( _listSample.empty() )
            return;
        if ( _listSample.size() == 1 )
        {
            outListWeight.front() = 1.0f;
            return;
        }
        if ( _axisCount == 1 )
        {
            BlendSpaceInternal::computeLinearWeights( _listSample, parameter._x, outListWeight );
            return;
        }
        computeTriangleWeights( parameter, outListWeight );
    }

    float32 BlendSpace::computeBlendedLength( const vector<float32>& listSampleLength, const vector<float32>& listWeight )
    {
        const size_t count       = MathUtil::min( listSampleLength.size(), listWeight.size() );
        float32      length      = 0.0f;
        float32      totalWeight = 0.0f;
        for ( size_t index = 0; index < count; ++index )
        {
            length += listSampleLength[index] * listWeight[index];
            totalWeight += listWeight[index];
        }
        return ( totalWeight > MathUtil::kEpsilon ) ? length / totalWeight : 0.0f;
    }

    void BlendSpace::foldToHeaviest( vector<float32>& inoutListWeight, float32 blendWeight )
    {
        const float32 keep = MathUtil::saturate( blendWeight );
        if ( inoutListWeight.empty() || keep >= 1.0f )
            return;
        size_t heaviest = 0;
        for ( size_t index = 1; index < inoutListWeight.size(); ++index )
        {
            if ( inoutListWeight[index] > inoutListWeight[heaviest] )
                heaviest = index;
        }
        for ( size_t index = 0; index < inoutListWeight.size(); ++index )
        {
            inoutListWeight[index] = inoutListWeight[index] * keep + ( index == heaviest ? 1.0f - keep : 0.0f );
        }
    }
} // namespace sw
