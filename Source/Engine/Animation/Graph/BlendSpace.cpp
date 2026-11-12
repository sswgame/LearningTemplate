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
            /**
             * @brief 정규화 축 [0, 1] 을 나누는 정수 격자 칸 수입니다(2^14).
             * @details 외접원 판정 값이 칸 수의 네제곱 × 12 이하라 2^14 면 2^60 아래로 int64 에 정확히 담긴다.
             *          축 범위의 1/16384 보다 가까운 두 표본은 같은 자리로 본다(`hasCoincidentSamples`).
             */
            static constexpr int64 kGridSteps = 16384;
            /** @brief 두 삼각형이 맞닿은 변 위의 점이 반올림으로 양쪽에서 다 빠지지 않게 하는 무게중심 여유입니다. */
            static constexpr float32 kInsideTolerance = -1.0e-5f;

            /** @brief 정수 격자의 점입니다. @p _sample 은 원래 표본 번호입니다. */
            struct GridPoint
            {
                int64  _x;
                int64  _y;
                uint32 _sample;
            };

            /** @brief 굽는 중의 삼각형입니다. 꼭짓점은 격자 점 번호이고 반시계 방향입니다. */
            struct GridTriangle
            {
                uint32 _a;
                uint32 _b;
                uint32 _c;
            };

            /** @brief 정규화 좌표 하나를 격자 칸으로 옮깁니다. 범위 밖은 가둡니다. */
            static int64 toGrid( float32 normalized )
            {
                return static_cast<int64>( MathUtil::round( static_cast<float64>( MathUtil::saturate( normalized ) ) * static_cast<float64>( kGridSteps ) ) );
            }

            /** @brief a → b → c 가 반시계면 양수, 한 줄이면 0 입니다(정확한 정수). */
            static int64 orient( const GridPoint& a, const GridPoint& b, const GridPoint& c )
            {
                return ( b._x - a._x ) * ( c._y - a._y ) - ( b._y - a._y ) * ( c._x - a._x );
            }

            /** @brief 반시계 삼각형 a · b · c 의 외접원 안에 d 가 있으면 양수, 원 위면 0 입니다(정확한 정수). */
            static int64 inCircle( const GridPoint& a, const GridPoint& b, const GridPoint& c, const GridPoint& d )
            {
                const int64 adx   = a._x - d._x;
                const int64 ady   = a._y - d._y;
                const int64 bdx   = b._x - d._x;
                const int64 bdy   = b._y - d._y;
                const int64 cdx   = c._x - d._x;
                const int64 cdy   = c._y - d._y;
                const int64 aLift = adx * adx + ady * ady;
                const int64 bLift = bdx * bdx + bdy * bdy;
                const int64 cLift = cdx * cdx + cdy * cdy;
                return aLift * ( bdx * cdy - cdx * bdy ) + bLift * ( cdx * ady - adx * cdy ) + cLift * ( adx * bdy - bdx * ady );
            }

            static bool hasDirectedEdge( const GridTriangle& triangle, uint32 from, uint32 to )
            {
                return ( triangle._a == from && triangle._b == to ) || ( triangle._b == from && triangle._c == to ) || ( triangle._c == from && triangle._a == to );
            }

            /** @brief 반시계 삼각형에서 from → to 변의 맞은편 꼭짓점입니다. */
            static uint32 findOpposite( const GridTriangle& triangle, uint32 from, uint32 to )
            {
                if ( triangle._a != from && triangle._a != to )
                    return triangle._a;
                if ( triangle._b != from && triangle._b != to )
                    return triangle._b;
                return triangle._c;
            }

            /** @brief 한 줄로 늘어선 앞 점들(0 ~ @p apex − 1)과 그 줄 밖의 첫 점 @p apex 로 부채꼴 삼각형을 만듭니다. */
            static void addFan( const vector<GridPoint>& listPoint, uint32 apex, vector<GridTriangle>& inoutListTriangle )
            {
                for ( uint32 index = 0; index + 1 < apex; ++index )
                {
                    if ( orient( listPoint[index], listPoint[index + 1], listPoint[apex] ) > 0 )
                        inoutListTriangle.push_back( GridTriangle{ index, index + 1, apex } );
                    else
                        inoutListTriangle.push_back( GridTriangle{ index + 1, index, apex } );
                }
            }

            /**
             * @brief 점 @p pointIndex 를 지금 삼각분할의 볼록 껍질 바깥에서 잇습니다. 그 점에서 보이는 바깥 변마다 삼각형 하나를 더합니다.
             * @details 점은 x(같으면 y) 순으로 들어오므로 늘 지금 껍질 바깥(또는 그 연장선 위)이다. 바깥 변은 반대 방향 변을 가진 삼각형이 없는 변이다.
             */
            static void addOutsidePoint( const vector<GridPoint>& listPoint, uint32 pointIndex, vector<GridTriangle>& inoutListTriangle )
            {
                const size_t triangleCount = inoutListTriangle.size();
                for ( size_t triangleIndex = 0; triangleIndex < triangleCount; ++triangleIndex )
                {
                    const GridTriangle triangle   = inoutListTriangle[triangleIndex];
                    const uint32       arrFrom[3] = { triangle._a, triangle._b, triangle._c };
                    const uint32       arrTo[3]   = { triangle._b, triangle._c, triangle._a };
                    for ( uint32 edge = 0; edge < 3; ++edge )
                    {
                        if ( orient( listPoint[arrFrom[edge]], listPoint[arrTo[edge]], listPoint[pointIndex] ) >= 0 )
                            continue; // 이 변의 안쪽(또는 연장선 위)이라 보이지 않는다
                        bool bInner = false;
                        for ( size_t otherIndex = 0; otherIndex < triangleCount && bInner == false; ++otherIndex )
                            bInner = hasDirectedEdge( inoutListTriangle[otherIndex], arrTo[edge], arrFrom[edge] );
                        if ( bInner == false )
                            inoutListTriangle.push_back( GridTriangle{ arrTo[edge], arrFrom[edge], pointIndex } );
                    }
                }
            }

            /**
             * @brief 들로네가 될 때까지 변을 뒤집습니다(로슨). 맞닿은 두 삼각형에서 한쪽 맞은편 꼭짓점이 다른 쪽 외접원 안에 확실히 있으면 대각선을 바꾼다.
             * @details 판정이 정확한 정수라 원 위의 네 점(격자)에서 뒤집기를 되풀이하지 않고 끝난다.
             */
            static void makeDelaunay( const vector<GridPoint>& listPoint, vector<GridTriangle>& inoutListTriangle )
            {
                bool bFlipped = true;
                while ( bFlipped )
                {
                    bFlipped = false;
                    for ( size_t triangleIndex = 0; triangleIndex < inoutListTriangle.size(); ++triangleIndex )
                    {
                        for ( uint32 edge = 0; edge < 3 && bFlipped == false; ++edge )
                        {
                            const GridTriangle triangle   = inoutListTriangle[triangleIndex];
                            const uint32       arrFrom[3] = { triangle._a, triangle._b, triangle._c };
                            const uint32       arrTo[3]   = { triangle._b, triangle._c, triangle._a };
                            const uint32       from       = arrFrom[edge];
                            const uint32       to         = arrTo[edge];
                            const uint32       apex       = findOpposite( triangle, from, to );
                            for ( size_t otherIndex = 0; otherIndex < inoutListTriangle.size(); ++otherIndex )
                            {
                                if ( otherIndex == triangleIndex || hasDirectedEdge( inoutListTriangle[otherIndex], to, from ) == false )
                                    continue;
                                const uint32 opposite = findOpposite( inoutListTriangle[otherIndex], to, from );
                                if ( inCircle( listPoint[from], listPoint[to], listPoint[apex], listPoint[opposite] ) > 0 )
                                {
                                    inoutListTriangle[triangleIndex] = GridTriangle{ apex, from, opposite };
                                    inoutListTriangle[otherIndex]    = GridTriangle{ apex, opposite, to };
                                    bFlipped                         = true;
                                }
                                break;
                            }
                        }
                        if ( bFlipped )
                            break;
                    }
                }
            }

            /**
             * @brief 격자 점으로 들로네 삼각분할을 굽습니다. 같은 자리의 점은 하나만 씁니다. 점이 넓이를 이루지 못하면 비웁니다.
             * @details 큰 바깥 삼각형(보이어-왓슨)을 쓰지 않는다 — 그 꼭짓점이 유한하면 껍질 근처의 얇은 삼각형이 빠진다. 대신 x 순 쓸기로 껍질 전체를 덮고 변 뒤집기로 들로네를 맞춘다.
             *          표본 수가 수십 개라 O(n³) 로 충분하다 — 굽기는 그래프를 읽을 때 한 번이다.
             */
            static void triangulate( vector<GridPoint> listPoint, vector<BlendSpaceTriangle>& outListTriangle )
            {
                outListTriangle.clear();
                VectorUtil::stableSort( listPoint, []( const GridPoint& lhs, const GridPoint& rhs )
                {
                    return lhs._x < rhs._x || ( lhs._x == rhs._x && lhs._y < rhs._y );
                } );
                vector<GridPoint> listUnique;
                for ( const GridPoint& point : listPoint )
                {
                    if ( listUnique.empty() || listUnique.back()._x != point._x || listUnique.back()._y != point._y )
                        listUnique.push_back( point );
                }
                uint32 apex = 2;
                while ( apex < listUnique.size() && orient( listUnique[0], listUnique[1], listUnique[apex] ) == 0 )
                    ++apex;
                if ( apex >= listUnique.size() )
                    return; // 모두 한 줄이다
                vector<GridTriangle> listTriangle;
                addFan( listUnique, apex, listTriangle );
                for ( uint32 pointIndex = apex + 1; pointIndex < static_cast<uint32>( listUnique.size() ); ++pointIndex )
                    addOutsidePoint( listUnique, pointIndex, listTriangle );
                makeDelaunay( listUnique, listTriangle );
                for ( const GridTriangle& triangle : listTriangle )
                    outListTriangle.push_back( BlendSpaceTriangle{ listUnique[triangle._a]._sample, listUnique[triangle._b]._sample, listUnique[triangle._c]._sample } );
            }

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
        };
    } // namespace
} // namespace sw

namespace sw
{
    BlendSpace::BlendSpace()
        : _listSample{}
        , _listTriangle{}
        , _axisMinimum{ 0.0f, 0.0f }
        , _axisMaximum{ 0.0f, 0.0f }
        , _axisCount{ 1 }
    {
    }

    void BlendSpace::setAxisCount( uint32 axisCount )
    {
        _axisCount = ( axisCount == 2 ) ? 2u : 1u;
        rebuildTriangulation();
    }

    void BlendSpace::setAxisRange( const float2& minimum, const float2& maximum )
    {
        _axisMinimum = minimum;
        _axisMaximum = maximum;
        rebuildTriangulation();
    }

    bool BlendSpace::hasValidAxisRange() const
    {
        const bool bValidX = _axisMaximum._x > _axisMinimum._x;
        return _axisCount == 1 ? bValidX : bValidX && _axisMaximum._y > _axisMinimum._y;
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

    bool BlendSpace::hasCoincidentSamples() const
    {
        using Internal = BlendSpaceInternal;
        if ( hasValidAxisRange() == false )
            return false;
        for ( size_t index = 0; index < _listSample.size(); ++index )
        {
            const float2 lhs = toNormalized( _listSample[index]._position );
            for ( size_t otherIndex = index + 1; otherIndex < _listSample.size(); ++otherIndex )
            {
                const float2 rhs    = toNormalized( _listSample[otherIndex]._position );
                const bool   bSameX = Internal::toGrid( lhs._x ) == Internal::toGrid( rhs._x );
                const bool   bSameY = _axisCount == 1 || Internal::toGrid( lhs._y ) == Internal::toGrid( rhs._y );
                if ( bSameX && bSameY )
                    return true;
            }
        }
        return false;
    }

    bool BlendSpace::hasSampleOutsideAxisRange() const
    {
        for ( const BlendSpaceSample& sample : _listSample )
        {
            const bool bOutsideX = sample._position._x < _axisMinimum._x || sample._position._x > _axisMaximum._x;
            const bool bOutsideY = _axisCount == 2 && ( sample._position._y < _axisMinimum._y || sample._position._y > _axisMaximum._y );
            if ( bOutsideX || bOutsideY )
                return true;
        }
        return false;
    }

    void BlendSpace::rebuildTriangulation()
    {
        using Internal = BlendSpaceInternal;
        _listTriangle.clear();
        if ( _axisCount != 2 || _listSample.size() < 3 || hasValidAxisRange() == false )
            return;
        vector<Internal::GridPoint> listPoint;
        listPoint.reserve( _listSample.size() );
        for ( uint32 sample = 0; sample < static_cast<uint32>( _listSample.size() ); ++sample )
        {
            const float2 normalized = toNormalized( _listSample[sample]._position );
            listPoint.push_back( Internal::GridPoint{ Internal::toGrid( normalized._x ), Internal::toGrid( normalized._y ), sample } );
        }
        Internal::triangulate( std::move( listPoint ), _listTriangle );
    }

    float2 BlendSpace::toNormalized( const float2& position ) const
    {
        return float2{ ( position._x - _axisMinimum._x ) / ( _axisMaximum._x - _axisMinimum._x ), ( position._y - _axisMinimum._y ) / ( _axisMaximum._y - _axisMinimum._y ) };
    }

    void BlendSpace::computeTriangleWeights( const float2& parameter, vector<float32>& outListWeight ) const
    {
        // 언리얼 Blend Space(들로네 삼각분할 + 무게중심) · Godot BlendSpace2D(삼각형, 바깥은 가장 가까운 변)를 따른다.
        // 유니티 Freeform Cartesian · Directional 은 경사 대역 가중이라 모든 표본이 켜질 수 있어, 샘플 수 상한(셋)을 위해 택하지 않는다.
        using Internal = BlendSpaceInternal;
        SW_ASSERT( hasArea() ); // 표본이 둘 이상인 2D 는 그래프 로드가 넓이를 요구한다
        if ( hasArea() == false )
            return;
        // 언리얼처럼 파라미터를 축 범위로 가둔다.
        const float2 unclamped = toNormalized( parameter );
        const float2 point{ MathUtil::saturate( unclamped._x ), MathUtil::saturate( unclamped._y ) };

        // 표본과 같은 격자 칸이면 그 표본 하나다(무게중심의 반올림으로 이웃이 아주 조금 켜지지 않게).
        for ( size_t sample = 0; sample < _listSample.size(); ++sample )
        {
            const float2 samplePoint = toNormalized( _listSample[sample]._position );
            if ( Internal::toGrid( point._x ) == Internal::toGrid( samplePoint._x ) && Internal::toGrid( point._y ) == Internal::toGrid( samplePoint._y ) )
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
            if ( weightA < Internal::kInsideTolerance || weightB < Internal::kInsideTolerance || weightC < Internal::kInsideTolerance )
                continue;
            // 변 위의 점은 맞은편 꼭짓점 몫이 반올림으로 아주 작게 남는다 — 여유 안의 몫은 0 으로 둬 켜지는 표본을 늘리지 않는다.
            const float32 clampedA           = weightA > -Internal::kInsideTolerance ? weightA : 0.0f;
            const float32 clampedB           = weightB > -Internal::kInsideTolerance ? weightB : 0.0f;
            const float32 clampedC           = weightC > -Internal::kInsideTolerance ? weightC : 0.0f;
            const float32 inverseTotal       = 1.0f / ( clampedA + clampedB + clampedC );
            outListWeight[triangle._sampleA] = clampedA * inverseTotal;
            outListWeight[triangle._sampleB] = clampedB * inverseTotal;
            outListWeight[triangle._sampleC] = clampedC * inverseTotal;
            return;
        }
        // 볼록 껍질 바깥 — 가장 가까운 변 위의 점으로 옮긴다(안쪽 변도 보지만 바깥 점에 가장 가까운 것은 늘 바깥 변이다).
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
