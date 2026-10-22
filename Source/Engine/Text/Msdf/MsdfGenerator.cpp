#include "pch.h"

#include "Engine/Text/Msdf/MsdfGenerator.h"

#include "Core/Math/MathUtil.h"

#include <limits>

namespace sw
{
    namespace
    {
        struct MsdfGeneratorInternal
        {
            static constexpr float64 kInfinity     = std::numeric_limits<float64>::infinity();
            static constexpr float64 kParamEpsilon = 1e-9;
            /** @brief 닫힌 구간 [0, 1] 에서 가장 가까운 점을 찾을 때 먼저 훑는 표본 수입니다. 표본마다 가장 가까운 곳을 고른 뒤 뉴턴으로 다듬는다. */
            static constexpr int32 kSampleCount = 16;
            static constexpr int32 kNewtonSteps = 8;
            /** @brief 오차 정정이 한 번에 한 번씩 다시 훑는 최대 횟수입니다(접은 텍셀이 새 어긋남을 만들 수 있다). */
            static constexpr int32 kMaxCorrectionPass = 4;

            struct Vec
            {
                float64 _x{ 0.0 };
                float64 _y{ 0.0 };
            };
            static Vec     makeVec( const float2& point ) { return Vec{ static_cast<float64>( point._x ), static_cast<float64>( point._y ) }; }
            static Vec     add( Vec a, Vec b ) { return Vec{ a._x + b._x, a._y + b._y }; }
            static Vec     sub( Vec a, Vec b ) { return Vec{ a._x - b._x, a._y - b._y }; }
            static Vec     scale( Vec a, float64 s ) { return Vec{ a._x * s, a._y * s }; }
            static Vec     lerp( Vec a, Vec b, float64 t ) { return Vec{ a._x + ( b._x - a._x ) * t, a._y + ( b._y - a._y ) * t }; }
            static float64 dot( Vec a, Vec b ) { return a._x * b._x + a._y * b._y; }
            static float64 cross( Vec a, Vec b ) { return a._x * b._y - a._y * b._x; }
            static float64 length( Vec a ) { return MathUtil::sqrt( dot( a, a ) ); }
            static Vec     normalize( Vec a )
            {
                const float64 len = length( a );
                return len > 0.0 ? scale( a, 1.0 / len ) : Vec{};
            }
            static float2 toFloat2( Vec a ) { return float2{ static_cast<float32>( a._x ), static_cast<float32>( a._y ) }; }

            // ---------------------------------------------------------------------------
            // 간선(곡선) 기하
            // ---------------------------------------------------------------------------

            /** @brief 간선 하나의 배정밀도 사본과 경계 상자입니다(훑는 동안 쓴다). */
            struct Curve
            {
                MsdfEdgeKind _kind{ MsdfEdgeKind::Linear };
                uint8        _channels{ MsdfChannel::kWhite };
                Vec          _arrPoint[4]{};
                Vec          _boundsMin{};
                Vec          _boundsMax{};
            };

            static uint32 getPointCount( MsdfEdgeKind kind )
            {
                switch ( kind )
                {
                    case MsdfEdgeKind::Linear:
                        return 2;
                    case MsdfEdgeKind::Quadratic:
                        return 3;
                    case MsdfEdgeKind::Cubic:
                        return 4;
                }
                return 2;
            }

            static Curve makeCurve( const MsdfEdge& edge )
            {
                Curve curve{};
                curve._kind             = edge._kind;
                curve._channels         = edge._channels;
                const uint32 pointCount = getPointCount( edge._kind );
                curve._boundsMin        = makeVec( edge._arrPoint[0] );
                curve._boundsMax        = curve._boundsMin;
                for ( uint32 index = 0; index < pointCount; ++index )
                {
                    const Vec point        = makeVec( edge._arrPoint[index] );
                    curve._arrPoint[index] = point;
                    curve._boundsMin._x    = MathUtil::min( curve._boundsMin._x, point._x );
                    curve._boundsMin._y    = MathUtil::min( curve._boundsMin._y, point._y );
                    curve._boundsMax._x    = MathUtil::max( curve._boundsMax._x, point._x );
                    curve._boundsMax._y    = MathUtil::max( curve._boundsMax._y, point._y );
                }
                return curve;
            }

            static Vec getLastPoint( const Curve& curve ) { return curve._arrPoint[getPointCount( curve._kind ) - 1]; }

            static Vec evaluate( const Curve& curve, float64 t )
            {
                const float64 mt = 1.0 - t;
                switch ( curve._kind )
                {
                    case MsdfEdgeKind::Linear:
                        return lerp( curve._arrPoint[0], curve._arrPoint[1], t );
                    case MsdfEdgeKind::Quadratic:
                        return add( add( scale( curve._arrPoint[0], mt * mt ), scale( curve._arrPoint[1], 2.0 * mt * t ) ), scale( curve._arrPoint[2], t * t ) );
                    case MsdfEdgeKind::Cubic:
                        return add( add( scale( curve._arrPoint[0], mt * mt * mt ), scale( curve._arrPoint[1], 3.0 * mt * mt * t ) ),
                                    add( scale( curve._arrPoint[2], 3.0 * mt * t * t ), scale( curve._arrPoint[3], t * t * t ) ) );
                }
                return Vec{};
            }

            static Vec evaluateDerivative( const Curve& curve, float64 t )
            {
                const float64 mt = 1.0 - t;
                switch ( curve._kind )
                {
                    case MsdfEdgeKind::Linear:
                        return sub( curve._arrPoint[1], curve._arrPoint[0] );
                    case MsdfEdgeKind::Quadratic:
                        return scale( add( scale( sub( curve._arrPoint[1], curve._arrPoint[0] ), mt ), scale( sub( curve._arrPoint[2], curve._arrPoint[1] ), t ) ), 2.0 );
                    case MsdfEdgeKind::Cubic:
                        return scale( add( add( scale( sub( curve._arrPoint[1], curve._arrPoint[0] ), mt * mt ), scale( sub( curve._arrPoint[2], curve._arrPoint[1] ), 2.0 * mt * t ) ),
                                           scale( sub( curve._arrPoint[3], curve._arrPoint[2] ), t * t ) ),
                                      3.0 );
                }
                return Vec{};
            }

            static Vec evaluateSecondDerivative( const Curve& curve, float64 t )
            {
                switch ( curve._kind )
                {
                    case MsdfEdgeKind::Linear:
                    {
                        return Vec{};
                    }
                    case MsdfEdgeKind::Quadratic:
                    {
                        return scale( add( sub( curve._arrPoint[2], scale( curve._arrPoint[1], 2.0 ) ), curve._arrPoint[0] ), 2.0 );
                    }
                    case MsdfEdgeKind::Cubic:
                    {
                        const Vec first  = add( sub( curve._arrPoint[2], scale( curve._arrPoint[1], 2.0 ) ), curve._arrPoint[0] );
                        const Vec second = add( sub( curve._arrPoint[3], scale( curve._arrPoint[2], 2.0 ) ), curve._arrPoint[1] );
                        return scale( add( scale( first, 1.0 - t ), scale( second, t ) ), 6.0 );
                    }
                }
                return Vec{};
            }

            /** @brief 간선의 시작(@p bAtEnd 가 false)이나 끝의 진행 방향입니다. 제어점이 겹쳐 0 이면 다음 제어점으로 넘어간다. */
            static Vec findEndDirection( const Curve& curve, bool bAtEnd )
            {
                const uint32 last = getPointCount( curve._kind ) - 1;
                for ( uint32 step = 1; step <= last; ++step )
                {
                    const Vec direction = bAtEnd ? sub( curve._arrPoint[last], curve._arrPoint[last - step] ) : sub( curve._arrPoint[step], curve._arrPoint[0] );
                    if ( dot( direction, direction ) > 0.0 )
                        return direction;
                }
                return Vec{};
            }

            // ---------------------------------------------------------------------------
            // 부호 있는 거리
            // ---------------------------------------------------------------------------

            /** @brief 점에서 간선까지의 부호 있는 참 거리(안쪽이 +, 간선 진행 방향 왼쪽)와 의사 거리 확장에 쓰는 값입니다. */
            struct EdgeHit
            {
                float64 _distance{ kInfinity };
                float64 _orthogonality{ kInfinity }; ///< 같은 거리일 때 작은 쪽이 이긴다(끝점 접선에 수직에 가까운 쪽).
                float64 _param{ 0.0 };               ///< 가장 가까운 점의 매개변수입니다(선분은 [0, 1] 밖일 수 있다).
            };

            /** @brief 베지어 간선에서 @p point 에 가장 가까운 매개변수([0, 1])입니다. 표본으로 훑고 뉴턴으로 다듬는다. */
            static float64 findClosestParam( const Curve& curve, Vec point )
            {
                float64 bestParam    = 0.0;
                float64 bestDistance = kInfinity;
                for ( int32 index = 0; index <= kSampleCount; ++index )
                {
                    const float64 t        = static_cast<float64>( index ) / static_cast<float64>( kSampleCount );
                    const Vec     offset   = sub( evaluate( curve, t ), point );
                    const float64 distance = dot( offset, offset );
                    if ( distance < bestDistance )
                    {
                        bestDistance = distance;
                        bestParam    = t;
                    }
                }
                const float64 lower = MathUtil::max( 0.0, bestParam - 1.0 / static_cast<float64>( kSampleCount ) );
                const float64 upper = MathUtil::min( 1.0, bestParam + 1.0 / static_cast<float64>( kSampleCount ) );
                float64       t     = bestParam;
                for ( int32 step = 0; step < kNewtonSteps; ++step )
                {
                    const Vec     offset = sub( evaluate( curve, t ), point );
                    const Vec     first  = evaluateDerivative( curve, t );
                    const float64 slope  = dot( offset, first );
                    const float64 curv   = dot( first, first ) + dot( offset, evaluateSecondDerivative( curve, t ) );
                    if ( MathUtil::abs( curv ) < 1e-12 )
                        break;
                    const float64 next = MathUtil::clamp( t - slope / curv, lower, upper );
                    if ( MathUtil::abs( next - t ) < 1e-10 )
                    {
                        t = next;
                        break;
                    }
                    t = next;
                }
                const Vec     refinedOffset = sub( evaluate( curve, t ), point );
                const float64 refined       = dot( refinedOffset, refinedOffset );
                return refined <= bestDistance ? t : bestParam;
            }

            static EdgeHit evaluateEdge( const Curve& curve, Vec point )
            {
                EdgeHit hit{};
                if ( curve._kind == MsdfEdgeKind::Linear )
                {
                    const Vec     edge     = sub( curve._arrPoint[1], curve._arrPoint[0] );
                    const Vec     toward   = sub( point, curve._arrPoint[0] );
                    const float64 length2  = dot( edge, edge );
                    const float64 t        = length2 > 0.0 ? dot( toward, edge ) / length2 : 0.0;
                    const float64 clamped  = MathUtil::clamp( t, 0.0, 1.0 );
                    const Vec     offset   = sub( point, lerp( curve._arrPoint[0], curve._arrPoint[1], clamped ) );
                    const float64 distance = length( offset );
                    hit._distance          = cross( edge, offset ) >= 0.0 ? distance : -distance;
                    hit._orthogonality     = ( t > 0.0 && t < 1.0 ) ? 0.0 : MathUtil::abs( dot( normalize( edge ), normalize( offset ) ) );
                    hit._param             = t;
                    return hit;
                }
                const float64 t         = findClosestParam( curve, point );
                const Vec     offset    = sub( point, evaluate( curve, t ) );
                const bool    bStart    = t <= kParamEpsilon;
                const bool    bEnd      = t >= 1.0 - kParamEpsilon;
                Vec           direction = bStart ? findEndDirection( curve, false ) : ( bEnd ? findEndDirection( curve, true ) : evaluateDerivative( curve, t ) );
                if ( dot( direction, direction ) <= 0.0 )
                    direction = findEndDirection( curve, bEnd );
                const float64 distance = length( offset );
                hit._distance          = cross( direction, offset ) >= 0.0 ? distance : -distance;
                hit._orthogonality     = ( bStart || bEnd ) ? MathUtil::abs( dot( normalize( direction ), normalize( offset ) ) ) : 0.0;
                hit._param             = t;
                return hit;
            }

            /** @brief 가장 가까운 점이 간선 끝점이면 그 끝의 접선 직선까지의 거리(의사 거리)로 바꿉니다 — 모서리 바깥에서 모서리가 날카롭게 보이는 근거다. */
            static float64 toPseudoDistance( const Curve& curve, Vec point, const EdgeHit& hit )
            {
                if ( hit._param <= kParamEpsilon )
                {
                    const Vec     direction = normalize( findEndDirection( curve, false ) );
                    const Vec     toward    = sub( point, curve._arrPoint[0] );
                    const float64 along     = dot( toward, direction );
                    if ( along < 0.0 )
                    {
                        const float64 pseudo = cross( direction, toward );
                        if ( MathUtil::abs( pseudo ) <= MathUtil::abs( hit._distance ) )
                            return pseudo;
                    }
                }
                else if ( hit._param >= 1.0 - kParamEpsilon )
                {
                    const Vec     direction = normalize( findEndDirection( curve, true ) );
                    const Vec     toward    = sub( point, getLastPoint( curve ) );
                    const float64 along     = dot( toward, direction );
                    if ( along > 0.0 )
                    {
                        const float64 pseudo = cross( direction, toward );
                        if ( MathUtil::abs( pseudo ) <= MathUtil::abs( hit._distance ) )
                            return pseudo;
                    }
                }
                return hit._distance;
            }

            static bool isCloser( const EdgeHit& candidate, const EdgeHit& best )
            {
                const float64 candidateAbs = MathUtil::abs( candidate._distance );
                const float64 bestAbs      = MathUtil::abs( best._distance );
                return candidateAbs < bestAbs || ( candidateAbs == bestAbs && candidate._orthogonality < best._orthogonality );
            }

            static float64 computeBoundsDistance( const Curve& curve, Vec point )
            {
                const float64 dx = MathUtil::max( MathUtil::max( curve._boundsMin._x - point._x, 0.0 ), point._x - curve._boundsMax._x );
                const float64 dy = MathUtil::max( MathUtil::max( curve._boundsMin._y - point._y, 0.0 ), point._y - curve._boundsMax._y );
                return MathUtil::sqrt( dx * dx + dy * dy );
            }

            // ---------------------------------------------------------------------------
            // 간선 나누기 · 칠하기
            // ---------------------------------------------------------------------------

            /** @brief 2차 베지어의 블로섬 b(u, v)입니다. 구간 [t0, t1] 의 제어점이 b(t0, t0), b(t0, t1), b(t1, t1) 이다. */
            static Vec blossom2( const Vec* pPoint, float64 u, float64 v )
            {
                return lerp( lerp( pPoint[0], pPoint[1], u ), lerp( pPoint[1], pPoint[2], u ), v );
            }

            /** @brief 3차 베지어의 블로섬 b(u, v, w)입니다. */
            static Vec blossom3( const Vec* pPoint, float64 u, float64 v, float64 w )
            {
                const Vec q0 = lerp( pPoint[0], pPoint[1], u );
                const Vec q1 = lerp( pPoint[1], pPoint[2], u );
                const Vec q2 = lerp( pPoint[2], pPoint[3], u );
                return lerp( lerp( q0, q1, v ), lerp( q1, q2, v ), w );
            }

            /** @brief 간선의 매개변수 구간 [t0, t1] 만 떼어 낸 간선입니다(블로섬 식). */
            static MsdfEdge subdivide( const MsdfEdge& edge, float64 t0, float64 t1 )
            {
                MsdfEdge part{};
                part._kind     = edge._kind;
                part._channels = edge._channels;
                Vec arrPoint[4]{};
                for ( uint32 index = 0; index < getPointCount( edge._kind ); ++index )
                {
                    arrPoint[index] = makeVec( edge._arrPoint[index] );
                }
                switch ( edge._kind )
                {
                    case MsdfEdgeKind::Linear:
                    {
                        part._arrPoint[0] = toFloat2( lerp( arrPoint[0], arrPoint[1], t0 ) );
                        part._arrPoint[1] = toFloat2( lerp( arrPoint[0], arrPoint[1], t1 ) );
                        break;
                    }
                    case MsdfEdgeKind::Quadratic:
                    {
                        part._arrPoint[0] = toFloat2( blossom2( arrPoint, t0, t0 ) );
                        part._arrPoint[1] = toFloat2( blossom2( arrPoint, t0, t1 ) );
                        part._arrPoint[2] = toFloat2( blossom2( arrPoint, t1, t1 ) );
                        break;
                    }
                    case MsdfEdgeKind::Cubic:
                    {
                        part._arrPoint[0] = toFloat2( blossom3( arrPoint, t0, t0, t0 ) );
                        part._arrPoint[1] = toFloat2( blossom3( arrPoint, t0, t0, t1 ) );
                        part._arrPoint[2] = toFloat2( blossom3( arrPoint, t0, t1, t1 ) );
                        part._arrPoint[3] = toFloat2( blossom3( arrPoint, t1, t1, t1 ) );
                        break;
                    }
                }
                return part;
            }

            static void splitInThirds( const MsdfEdge& edge, MsdfEdge& outFirst, MsdfEdge& outSecond, MsdfEdge& outThird )
            {
                outFirst  = subdivide( edge, 0.0, 1.0 / 3.0 );
                outSecond = subdivide( edge, 1.0 / 3.0, 2.0 / 3.0 );
                outThird  = subdivide( edge, 2.0 / 3.0, 1.0 );
            }

            /** @brief 이웃 간선의 방향 @p a(앞 간선 끝) · @p b(뒤 간선 시작)가 모서리를 이루는지 봅니다. */
            static bool isCorner( Vec a, Vec b, float64 crossThreshold )
            {
                return dot( a, b ) <= 0.0 || MathUtil::abs( cross( a, b ) ) > crossThreshold;
            }

            /** @brief msdfgen 의 `switchColor` 입니다 — 다음 색을 고릅니다. @p banned 가 한 채널만 겹치면 그 채널을 뺀 나머지 둘로 간다. */
            static void switchColor( uint8& inoutColor, uint64& inoutSeed, uint8 banned = 0 )
            {
                const uint8 combined      = static_cast<uint8>( inoutColor & banned );
                const bool  bSingleBanned = combined == MsdfChannel::kRed || combined == MsdfChannel::kGreen || combined == MsdfChannel::kBlue;
                if ( bSingleBanned )
                {
                    inoutColor = static_cast<uint8>( combined ^ MsdfChannel::kWhite );
                    return;
                }
                if ( inoutColor == 0 || inoutColor == MsdfChannel::kWhite )
                {
                    static constexpr uint8 kArrStart[3] = { MsdfChannel::kCyan, MsdfChannel::kMagenta, MsdfChannel::kYellow };
                    inoutColor                          = kArrStart[inoutSeed % 3];
                    inoutSeed /= 3;
                    return;
                }
                const uint32 shifted = static_cast<uint32>( inoutColor ) << ( 1u + static_cast<uint32>( inoutSeed & 1u ) );
                inoutColor           = static_cast<uint8>( ( shifted | ( shifted >> 3u ) ) & MsdfChannel::kWhite );
                inoutSeed >>= 1u;
            }

            static void colorContour( MsdfContour& inoutContour, float64 crossThreshold, uint64& inoutSeed )
            {
                vector<MsdfEdge>& listEdge = inoutContour._listEdge;
                if ( listEdge.empty() )
                    return;
                // 1) 모서리: 앞 간선의 끝 방향과 뒤 간선의 시작 방향이 꺾이는 간선 번호
                vector<size_t> listCorner;
                Vec            previousDirection = normalize( findEndDirection( makeCurve( listEdge.back() ), true ) );
                for ( size_t index = 0; index < listEdge.size(); ++index )
                {
                    const Curve curve = makeCurve( listEdge[index] );
                    if ( isCorner( previousDirection, normalize( findEndDirection( curve, false ) ), crossThreshold ) )
                        listCorner.push_back( index );
                    previousDirection = normalize( findEndDirection( curve, true ) );
                }
                // 2) 모서리가 없으면 모두 흰색(일반 SDF 와 같다)
                if ( listCorner.empty() )
                {
                    for ( MsdfEdge& edge : listEdge )
                    {
                        edge._channels = MsdfChannel::kWhite;
                    }
                    return;
                }
                // 3) 모서리가 하나(물방울 모양): 세 구역으로 칠한다. 간선이 셋보다 적으면 삼등분한다.
                if ( listCorner.size() == 1 )
                {
                    uint8 arrColor[3] = { MsdfChannel::kWhite, MsdfChannel::kWhite, 0 };
                    switchColor( arrColor[0], inoutSeed );
                    arrColor[2] = arrColor[0];
                    switchColor( arrColor[2], inoutSeed );
                    const size_t corner = listCorner[0];
                    const size_t count  = listEdge.size();
                    if ( count >= 3 )
                    {
                        for ( size_t index = 0; index < count; ++index )
                        {
                            const int32 colorIndex                         = static_cast<int32>( 3.0 + 2.875 * static_cast<float64>( index ) / static_cast<float64>( count - 1 ) - 1.4375 + 0.5 ) - 3;
                            listEdge[( corner + index ) % count]._channels = arrColor[colorIndex + 1];
                        }
                        return;
                    }
                    MsdfEdge arrPart[6]{};
                    if ( count == 1 )
                    {
                        splitInThirds( listEdge[0], arrPart[0], arrPart[1], arrPart[2] );
                        arrPart[0]._channels = arrColor[0];
                        arrPart[1]._channels = arrColor[1];
                        arrPart[2]._channels = arrColor[2];
                        listEdge.assign( arrPart, arrPart + 3 );
                        return;
                    }
                    // 간선 둘: 모서리 쪽 간선의 조각이 먼저 오게 놓고 두 색씩 나눈다.
                    splitInThirds( listEdge[0], arrPart[3 * corner], arrPart[3 * corner + 1], arrPart[3 * corner + 2] );
                    splitInThirds( listEdge[1], arrPart[3 - 3 * corner], arrPart[4 - 3 * corner], arrPart[5 - 3 * corner] );
                    arrPart[0]._channels = arrPart[1]._channels = arrColor[0];
                    arrPart[2]._channels = arrPart[3]._channels = arrColor[1];
                    arrPart[4]._channels = arrPart[5]._channels = arrColor[2];
                    listEdge.assign( arrPart, arrPart + 6 );
                    return;
                }
                // 4) 모서리가 여럿: 모서리를 지날 때마다 색을 바꾼다. 마지막 구역은 처음 색과 겹치지 않게 한다.
                const size_t cornerCount = listCorner.size();
                const size_t count       = listEdge.size();
                const size_t start       = listCorner[0];
                size_t       spline      = 0;
                uint8        color       = MsdfChannel::kWhite;
                switchColor( color, inoutSeed );
                const uint8 initialColor = color;
                for ( size_t index = 0; index < count; ++index )
                {
                    const size_t edgeIndex = ( start + index ) % count;
                    if ( spline + 1 < cornerCount && listCorner[spline + 1] == edgeIndex )
                    {
                        ++spline;
                        switchColor( color, inoutSeed, spline == cornerCount - 1 ? initialColor : static_cast<uint8>( 0 ) );
                    }
                    listEdge[edgeIndex]._channels = color;
                }
            }

            // ---------------------------------------------------------------------------
            // 오차 정정
            // ---------------------------------------------------------------------------

            static float32 getMedian( float32 a, float32 b, float32 c ) { return MathUtil::max( MathUtil::min( a, b ), MathUtil::min( MathUtil::max( a, b ), c ) ); }

            /** @brief 텍셀 @p a 와 @p b(RGBA 의 RGB) 사이를 보간했을 때 중앙값이 두 끝의 안팎과 어긋나는 거짓 윤곽이 생기면 true 입니다. */
            static bool hasInterpolationArtifact( const float32* pA, const float32* pB )
            {
                const float32 medianA    = getMedian( pA[0], pA[1], pA[2] );
                const float32 medianB    = getMedian( pB[0], pB[1], pB[2] );
                const bool    bInsideA   = medianA > 0.5f;
                const bool    bInsideB   = medianB > 0.5f;
                const bool    bOnContour = medianA == 0.5f || medianB == 0.5f;
                if ( bOnContour || bInsideA != bInsideB )
                    return false; // 윤곽이 두 텍셀 사이에 있다 — 어긋남을 따질 수 없다
                for ( const float32 t : { 0.25f, 0.5f, 0.75f } )
                {
                    const float32 mixed = getMedian( pA[0] + ( pB[0] - pA[0] ) * t, pA[1] + ( pB[1] - pA[1] ) * t, pA[2] + ( pB[2] - pA[2] ) * t );
                    if ( ( mixed > 0.5f ) != bInsideA )
                        return true;
                }
                return false;
            }

            static void correctArtifacts( vector<float32>& inoutListField, uint32 width, uint32 height )
            {
                static constexpr int32 kArrNeighbor[4][2] = {
                    { 1, 0},
                    { 0, 1},
                    { 1, 1},
                    {-1, 1}
                };
                vector<uint8> listMarked( static_cast<size_t>( width ) * height, 0 );
                for ( int32 pass = 0; pass < kMaxCorrectionPass; ++pass )
                {
                    bool bFound = false;
                    for ( uint32 row = 0; row < height; ++row )
                    {
                        for ( uint32 column = 0; column < width; ++column )
                        {
                            const size_t index = static_cast<size_t>( row ) * width + column;
                            for ( const auto& neighbor : kArrNeighbor )
                            {
                                const int32 nextColumn = static_cast<int32>( column ) + neighbor[0];
                                const int32 nextRow    = static_cast<int32>( row ) + neighbor[1];
                                const bool  bColumnOut = nextColumn < 0 || nextColumn >= static_cast<int32>( width );
                                const bool  bRowOut    = nextRow < 0 || nextRow >= static_cast<int32>( height );
                                if ( bColumnOut || bRowOut )
                                    continue;
                                const size_t other = static_cast<size_t>( nextRow ) * width + static_cast<uint32>( nextColumn );
                                if ( hasInterpolationArtifact( &inoutListField[index * 4], &inoutListField[other * 4] ) )
                                {
                                    listMarked[index] = 1;
                                    listMarked[other] = 1;
                                    bFound            = true;
                                }
                            }
                        }
                    }
                    if ( bFound == false )
                        return;
                    // 표시한 텍셀은 세 채널을 중앙값으로 접는다(일반 SDF). 접은 텍셀끼리는 거짓 윤곽이 없다.
                    for ( size_t index = 0; index < listMarked.size(); ++index )
                    {
                        if ( listMarked[index] == 0 )
                            continue;
                        float32*      pTexel = &inoutListField[index * 4];
                        const float32 median = getMedian( pTexel[0], pTexel[1], pTexel[2] );
                        pTexel[0] = pTexel[1] = pTexel[2] = median;
                        listMarked[index]                 = 0;
                    }
                }
            }

            static uint8 quantize( float32 value ) { return static_cast<uint8>( MathUtil::round( MathUtil::clamp( value, 0.0f, 1.0f ) * 255.0f ) ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void MsdfGenerator::colorEdges( MsdfShape& inoutShape, float32 cornerAngle, uint64 seed )
    {
        const float64 crossThreshold = MathUtil::sin( static_cast<float64>( cornerAngle ) );
        for ( MsdfContour& contour : inoutShape._listContour )
        {
            MsdfGeneratorInternal::colorContour( contour, crossThreshold, seed );
        }
    }

    void MsdfGenerator::computeBitmap( const MsdfShape& shape, const MsdfParams& params, vector<uint8>& outBytes )
    {
        using Internal          = MsdfGeneratorInternal;
        const size_t texelCount = static_cast<size_t>( params._width ) * params._height;
        outBytes.assign( texelCount * 4, static_cast<uint8>( 0 ) );
        if ( texelCount == 0 )
            return;

        vector<Internal::Curve> listCurve;
        for ( const MsdfContour& contour : shape._listContour )
        {
            for ( const MsdfEdge& edge : contour._listEdge )
            {
                listCurve.push_back( Internal::makeCurve( edge ) );
            }
        }

        const float64   range     = MathUtil::max( static_cast<float64>( params._rangePx ), 1e-6 );
        const float64   invRange2 = 1.0 / ( 2.0 * range );
        vector<float32> field( texelCount * 4, 0.0f );
        for ( uint32 row = 0; row < params._height; ++row )
        {
            for ( uint32 column = 0; column < params._width; ++column )
            {
                const Internal::Vec    point{ static_cast<float64>( params._origin._x ) + static_cast<float64>( column ) + 0.5,
                                           static_cast<float64>( params._origin._y ) - static_cast<float64>( row ) - 0.5 };
                Internal::EdgeHit      arrBest[3]{};
                const Internal::Curve* arrBestCurve[3] = { nullptr, nullptr, nullptr };
                Internal::EdgeHit      allBest{};
                for ( const Internal::Curve& curve : listCurve )
                {
                    // 이 간선이 낄 채널들의 지금 최선보다 경계 상자가 멀면 이길 수 없다(의사 거리는 이긴 뒤에만 쓰므로 경계 거리로 걸러도 정확하다).
                    float64 threshold = 0.0;
                    for ( uint32 channel = 0; channel < 3; ++channel )
                    {
                        if ( ( curve._channels & ( 1u << channel ) ) != 0 )
                            threshold = MathUtil::max( threshold, MathUtil::abs( arrBest[channel]._distance ) );
                    }
                    if ( Internal::computeBoundsDistance( curve, point ) > threshold )
                        continue;
                    const Internal::EdgeHit hit = Internal::evaluateEdge( curve, point );
                    if ( Internal::isCloser( hit, allBest ) )
                        allBest = hit;
                    for ( uint32 channel = 0; channel < 3; ++channel )
                    {
                        if ( ( curve._channels & ( 1u << channel ) ) != 0 && Internal::isCloser( hit, arrBest[channel] ) )
                        {
                            arrBest[channel]      = hit;
                            arrBestCurve[channel] = &curve;
                        }
                    }
                }
                float32* pTexel = &field[( static_cast<size_t>( row ) * params._width + column ) * 4];
                for ( uint32 channel = 0; channel < 3; ++channel )
                {
                    const float64 distance = arrBestCurve[channel] != nullptr ? Internal::toPseudoDistance( *arrBestCurve[channel], point, arrBest[channel] ) : -range;
                    pTexel[channel]        = static_cast<float32>( 0.5 + distance * invRange2 );
                }
                const float64 trueDistance = allBest._distance == Internal::kInfinity ? -range : allBest._distance;
                pTexel[3]                  = static_cast<float32>( 0.5 + trueDistance * invRange2 );
            }
        }

        Internal::correctArtifacts( field, params._width, params._height );
        for ( size_t index = 0; index < field.size(); ++index )
        {
            outBytes[index] = Internal::quantize( field[index] );
        }
    }
} // namespace sw
