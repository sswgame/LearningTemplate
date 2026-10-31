#include "pch.h"

#include "Engine/Environment/Placement/PlacementRule.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Foundation/EngineParallel.h"

namespace sw
{
    SW_LOG_CALLER( "PlacementScatter" );

    namespace
    {
        struct PlacementRuleInternal
        {
            /** @brief 후보 하나가 꺼내는 난수 슬롯입니다. 슬롯을 추가할 때는 끝에 붙입니다. 앞 슬롯의 값이 바뀌면 같은 시드의 배치가 바뀝니다. */
            enum Lane : uint32
            {
                kLaneU = 0,
                kLaneV,
                kLaneDensity,
                kLaneEntry,
                kLaneScale,
                kLaneYaw,
            };

            /**
             * @brief 최소 거리 다트 던지기의 격자입니다. 영역을 덮는 조밀 배열(셀마다 연결 목록 머리)이라 셀 하나를 찾는 일이 배열 한 칸이다 - 예전의 `unordered_map` 은
             *        Debug 에서 풀 레이어(후보 약 6 만) 한 번 정리에 0.26 초였다. 셀 크기는 최소 거리 / √2 이되 셀 수가 상한을 넘으면 키운다(한 셀에 점이 여럿이 된다 -
             *        그래서 목록이다). 최소 거리 안의 점을 모두 정확한 거리로 보므로 받아들이고 버리는 판정은 셀 크기와 무관하다.
             */
            struct PoissonGrid
            {
                static constexpr uint32 kMaxCellCount = 4u * 1024u * 1024u; ///< 16 MB(int32) - 후보 상한(`kMaxCandidateCount`)과 같은 규모

                float2         _origin{};
                float32        _cellSize{ 1.0f };
                float32        _minDistanceSquare{ 0.0f };
                int32          _cellCountU{ 1 };
                int32          _cellCountV{ 1 };
                int32          _reach{ 2 };
                vector<int32>  _listHead; ///< 셀마다 마지막에 놓인 점(-1 이면 비었다)
                vector<int32>  _listNext; ///< 점마다 같은 셀의 앞 점
                vector<float2> _listPoint;

                void init( const PlacementRegion& region, float32 minDistance )
                {
                    const float32 width  = MathUtil::max( region._max._x - region._min._x, 0.0f );
                    const float32 height = MathUtil::max( region._max._y - region._min._y, 0.0f );
                    _origin              = region._min;
                    _cellSize            = MathUtil::max( minDistance * MathUtil::kInvSqrt2,
                                                          MathUtil::sqrt( width * height / static_cast<float32>( kMaxCellCount ) ) );
                    _minDistanceSquare   = minDistance * minDistance;
                    _cellCountU          = MathUtil::max( 1, static_cast<int32>( MathUtil::ceil( width / _cellSize ) ) );
                    _cellCountV          = MathUtil::max( 1, static_cast<int32>( MathUtil::ceil( height / _cellSize ) ) );
                    _reach               = static_cast<int32>( MathUtil::ceil( minDistance / _cellSize ) );
                    _listHead.assign( static_cast<size_t>( _cellCountU ) * static_cast<size_t>( _cellCountV ), -1 );
                }

                [[nodiscard]] int32 cellU( float32 x ) const { return MathUtil::clamp( static_cast<int32>( MathUtil::floor( ( x - _origin._x ) / _cellSize ) ), 0, _cellCountU - 1 ); }
                [[nodiscard]] int32 cellV( float32 y ) const { return MathUtil::clamp( static_cast<int32>( MathUtil::floor( ( y - _origin._y ) / _cellSize ) ), 0, _cellCountV - 1 ); }

                /** @brief @p position 에서 최소 거리 안에 이미 놓인 점이 있으면 true 입니다. */
                [[nodiscard]] bool isTooClose( const float2& position ) const
                {
                    const int32   centerU = cellU( position._x );
                    const int32   centerV = cellV( position._y );
                    const int32   minV    = MathUtil::max( centerV - _reach, 0 );
                    const int32   maxV    = MathUtil::min( centerV + _reach, _cellCountV - 1 );
                    const int32   minU    = MathUtil::max( centerU - _reach, 0 );
                    const int32   maxU    = MathUtil::min( centerU + _reach, _cellCountU - 1 );
                    const int32*  pHead   = _listHead.data();
                    const int32*  pNext   = _listNext.data();
                    const float2* pPoint  = _listPoint.data();
                    for ( int32 v = minV; v <= maxV; ++v )
                    {
                        for ( int32 u = minU; u <= maxU; ++u )
                        {
                            for ( int32 index = pHead[static_cast<size_t>( v ) * static_cast<size_t>( _cellCountU ) + static_cast<size_t>( u )]; index >= 0; index = pNext[index] )
                            {
                                const float32 deltaU = pPoint[index]._x - position._x;
                                const float32 deltaV = pPoint[index]._y - position._y;
                                if ( deltaU * deltaU + deltaV * deltaV < _minDistanceSquare )
                                    return true;
                            }
                        }
                    }
                    return false;
                }

                void add( const float2& position )
                {
                    const size_t cell = static_cast<size_t>( cellV( position._y ) ) * static_cast<size_t>( _cellCountU ) + static_cast<size_t>( cellU( position._x ) );
                    _listNext.push_back( _listHead[cell] );
                    _listHead[cell] = static_cast<int32>( _listPoint.size() );
                    _listPoint.push_back( position );
                }
            };

            /** @brief 밀도 × 면적의 후보 수입니다. 영역이 비었거나 밀도가 0 이면 0 이고, 상한을 넘으면 잘라 @p outClamped 를 켭니다. */
            static uint32 computeCandidateCount( const PlacementRule& rule, const PlacementRegion& region, bool& outClamped )
            {
                outClamped         = false;
                const float32 area = ( region._max._x - region._min._x ) * ( region._max._y - region._min._y );
                if ( area <= 0.0f || rule._density <= 0.0f )
                    return 0;
                const float64 wantedCount = static_cast<float64>( rule._density ) * static_cast<float64>( area );
                if ( wantedCount < static_cast<float64>( PlacementScatter::kMaxCandidateCount ) )
                    return static_cast<uint32>( MathUtil::ceil( wantedCount ) );
                outClamped = true;
                return PlacementScatter::kMaxCandidateCount;
            }

            /** @brief 후보 @p candidateIndex 의 평면 위치입니다. 판정과 최소 거리가 같은 식으로 다시 구한다. */
            static float2 computeCandidatePosition( const PlacementRule& rule, const PlacementRegion& region, uint32 candidateIndex )
            {
                const float2 size{ region._max._x - region._min._x, region._max._y - region._min._y };
                return float2{ region._min._x + PlacementScatter::toUnit( PlacementScatter::hashCandidate( rule._seed, candidateIndex, kLaneU ) ) * size._x,
                               region._min._y + PlacementScatter::toUnit( PlacementScatter::hashCandidate( rule._seed, candidateIndex, kLaneV ) ) * size._y };
            }

            /** @brief 후보 하나를 최소 거리 앞까지 판정합니다. 통과하지 못하면 `_bPassed` 만 끈다. */
            static void evaluateCandidate( const PlacementRule& rule, const IPlacementSurface* pSurface, const vector<PlacementExclusion>& listExclusion,
                                           const float2& position, uint32 candidateIndex, PlacementCandidate& outCandidate )
            {
                outCandidate = PlacementCandidate{};
                if ( PlacementScatter::isExcluded( listExclusion, position ) )
                    return;

                PlacementSurfaceSample sample;
                if ( pSurface != nullptr && ( pSurface->sampleSurface( position, sample ) == false || sample._bValid == SW_FALSE ) )
                    return;

                const float32 slope = PlacementScatter::computeSlope( sample._normal );
                if ( slope < rule._slopeMin || rule._slopeMax < slope )
                    return;
                if ( sample._height < rule._heightMin || rule._heightMax < sample._height )
                    return;
                const bool bLayerFilter = 0 <= rule._layerIndex && rule._layerIndex < static_cast<int32>( SurfaceLayerWeights::kLayerCount );
                if ( bLayerFilter && sample._layerWeight.get( static_cast<uint32>( rule._layerIndex ) ) < rule._layerMinWeight )
                    return;
                const bool bDensityLayer = 0 <= rule._densityLayerIndex && rule._densityLayerIndex < static_cast<int32>( SurfaceLayerWeights::kLayerCount );
                if ( bDensityLayer && PlacementScatter::toUnit( PlacementScatter::hashCandidate( rule._seed, candidateIndex, kLaneDensity ) ) >= sample._layerWeight.get( static_cast<uint32>( rule._densityLayerIndex ) ) )
                    return;
                outCandidate._normal  = sample._normal;
                outCandidate._height  = sample._height;
                outCandidate._bPassed = SW_TRUE;
            }

            /** @brief 후보 구간 하나를 판정합니다(`sampleCandidates` · `resampleCandidates` 가 `runParallelByCost` 로 나눈다). `_pDirtyArea` 가 있으면 그 안의 후보만. */
            struct SampleJob
            {
                const PlacementRule*              _pRule{ nullptr };
                const PlacementRegion*            _pRegion{ nullptr };
                const IPlacementSurface*          _pSurface{ nullptr };
                const vector<PlacementExclusion>* _pExclusion{ nullptr };
                PlacementCandidate*               _pOut{ nullptr };
                const PlacementRegion*            _pDirtyArea{ nullptr };

                void runRange( uint32 start, uint32 end )
                {
                    for ( uint32 candidateIndex = start; candidateIndex < end; ++candidateIndex )
                    {
                        const float2 position = computeCandidatePosition( *_pRule, *_pRegion, candidateIndex );
                        if ( _pDirtyArea != nullptr && _pDirtyArea->contains( position ) == false )
                            continue;
                        evaluateCandidate( *_pRule, _pSurface, *_pExclusion, position, candidateIndex, _pOut[candidateIndex] );
                    }
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 PlacementScatter::hashCandidate( uint32 seed, uint32 candidateIndex, uint32 lane )
    {
        // murmur3 의 마무리 섞기를 세 값에 차례로 적용한다. 이웃한 번호와 셀이 이웃한 값을 받지 않는다.
        uint32 hash = seed * 0x9E3779B1u ^ candidateIndex * 0x85EBCA77u ^ lane * 0xC2B2AE3Du;
        hash ^= hash >> 16;
        hash *= 0x85EBCA6Bu;
        hash ^= hash >> 13;
        hash *= 0xC2B2AE35u;
        hash ^= hash >> 16;
        return hash;
    }

    float32 PlacementScatter::toUnit( uint32 hash )
    {
        return static_cast<float32>( hash >> 8 ) * ( 1.0f / 16777216.0f );
    }

    float32 PlacementScatter::computeSlope( const float3& normal )
    {
        const float32 length = normal.getLength();
        if ( length <= MathUtil::kEpsilon )
            return 0.0f;
        return MathUtil::acos( MathUtil::clamp( normal._y / length, -1.0f, 1.0f ) );
    }

    bool PlacementScatter::isExcluded( const vector<PlacementExclusion>& listExclusion, const float2& planePosition )
    {
        for ( const PlacementExclusion& exclusion : listExclusion )
        {
            const float32 deltaU = planePosition._x - exclusion._center._x;
            const float32 deltaV = planePosition._y - exclusion._center._y;
            switch ( exclusion._shape )
            {
                case PlacementExclusionShape::Circle:
                {
                    if ( deltaU * deltaU + deltaV * deltaV < exclusion._radius * exclusion._radius )
                        return true;
                    break;
                }
                case PlacementExclusionShape::Rect:
                {
                    if ( MathUtil::abs( deltaU ) < exclusion._halfExtent._x && MathUtil::abs( deltaV ) < exclusion._halfExtent._y )
                        return true;
                    break;
                }
            }
        }
        return false;
    }

    int32 PlacementScatter::pickEntry( const vector<float32>& listEntryWeight, float32 unit )
    {
        if ( listEntryWeight.empty() )
            return 0;
        float32 totalWeight = 0.0f;
        for ( const float32 weight : listEntryWeight )
        {
            totalWeight += MathUtil::max( weight, 0.0f );
        }
        if ( totalWeight <= 0.0f )
            return -1;
        float32 cumulative = 0.0f;
        int32   lastIndex  = -1;
        for ( int32 entryIndex = 0; entryIndex < static_cast<int32>( listEntryWeight.size() ); ++entryIndex )
        {
            const float32 weight = listEntryWeight[static_cast<size_t>( entryIndex )];
            if ( weight <= 0.0f )
                continue;
            cumulative += weight;
            lastIndex = entryIndex;
            if ( unit < cumulative / totalWeight )
                return entryIndex;
        }
        return lastIndex;
    }

    void PlacementScatter::scatter( const PlacementRule& rule, const PlacementRegion& region, const IPlacementSurface* pSurface,
                                    const vector<PlacementExclusion>& listExclusion, const vector<float32>& listEntryWeight,
                                    vector<PlacementInstance>& outListInstance )
    {
        vector<PlacementCandidate> listCandidate;
        sampleCandidates( rule, region, pSurface, listExclusion, listCandidate );
        resolveCandidates( rule, region, listCandidate, listEntryWeight, outListInstance );
    }

    void PlacementScatter::sampleCandidates( const PlacementRule& rule, const PlacementRegion& region, const IPlacementSurface* pSurface,
                                             const vector<PlacementExclusion>& listExclusion, vector<PlacementCandidate>& outListCandidate )
    {
        using Internal = PlacementRuleInternal;
        bool         bClamped{ false };
        const uint32 candidateCount = Internal::computeCandidateCount( rule, region, bClamped );
        if ( bClamped )
            SW_LOG_WARNING( "Placement asks for more than %# candidates (density %# over the region) - clamped", kMaxCandidateCount, rule._density );
        outListCandidate.resize( candidateCount );
        // 후보마다 독립이다(위치는 (시드, 번호) 해시, 판정은 읽기 전용 표면 · 제외 영역) - 구간으로 나눠 워커에 돌린다. 풀 레이어(후보 약 6 만)가 Debug 에서 0.2 초였다.
        // 결과 칸은 부르는 스레드에서 원시 포인터로 집는다(워커가 목록에 쓰기 접근을 하지 않게 - 경합 감지기는 다른 칸이어도 쓰기로 본다).
        Internal::SampleJob job{ &rule, &region, pSurface, &listExclusion, outListCandidate.data(), nullptr };
        engine::runParallelByCost( candidateCount, SW_DELEGATE_METHOD( ParallelBlockDelegate, &Internal::SampleJob::runRange, &job ) );
    }

    bool PlacementScatter::resampleCandidates( const PlacementRule& rule, const PlacementRegion& region, const IPlacementSurface* pSurface,
                                               const vector<PlacementExclusion>& listExclusion, const PlacementRegion& dirtyArea,
                                               vector<PlacementCandidate>& inoutListCandidate )
    {
        using Internal = PlacementRuleInternal;
        bool         bClamped{ false };
        const uint32 candidateCount = Internal::computeCandidateCount( rule, region, bClamped );
        if ( inoutListCandidate.size() != candidateCount )
            return false;
        Internal::SampleJob job{ &rule, &region, pSurface, &listExclusion, inoutListCandidate.data(), &dirtyArea };
        engine::runParallelByCost( candidateCount, SW_DELEGATE_METHOD( ParallelBlockDelegate, &Internal::SampleJob::runRange, &job ) );
        return true;
    }

    void PlacementScatter::resolveCandidates( const PlacementRule& rule, const PlacementRegion& region, const vector<PlacementCandidate>& listCandidate,
                                              const vector<float32>& listEntryWeight, vector<PlacementInstance>& outListInstance )
    {
        using Internal = PlacementRuleInternal;
        outListInstance.clear();

        const bool            bPoisson = rule._minDistance > 0.0f;
        Internal::PoissonGrid grid;
        if ( bPoisson )
            grid.init( region, rule._minDistance );

        const float32 scaleMin       = MathUtil::min( rule._scaleMin, rule._scaleMax );
        const float32 scaleMax       = MathUtil::max( rule._scaleMin, rule._scaleMax );
        const uint32  candidateCount = static_cast<uint32>( listCandidate.size() );
        for ( uint32 candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex )
        {
            const PlacementCandidate& candidate = listCandidate[candidateIndex];
            if ( candidate._bPassed == SW_FALSE )
                continue;
            const float2 position = Internal::computeCandidatePosition( rule, region, candidateIndex );
            if ( bPoisson && grid.isTooClose( position ) )
                continue;

            const int32 entryIndex = pickEntry( listEntryWeight, toUnit( hashCandidate( rule._seed, candidateIndex, Internal::kLaneEntry ) ) );
            if ( entryIndex < 0 )
                return; // 비중 합이 0 이면 놓을 것이 없다.

            if ( bPoisson )
                grid.add( position );

            PlacementInstance instance;
            instance._planePosition = position;
            instance._height        = candidate._height + rule._heightOffset;
            instance._normal        = candidate._normal;
            instance._scale         = scaleMin + toUnit( hashCandidate( rule._seed, candidateIndex, Internal::kLaneScale ) ) * ( scaleMax - scaleMin );
            instance._yaw           = rule._yawMin + toUnit( hashCandidate( rule._seed, candidateIndex, Internal::kLaneYaw ) ) * ( rule._yawMax - rule._yawMin );
            instance._alignToNormal = MathUtil::saturate( rule._alignToNormal );
            instance._hash          = hashCandidate( rule._seed, candidateIndex, 0xA5A5A5A5u );
            instance._entryIndex    = entryIndex;
            outListInstance.push_back( instance );
        }
    }

    float4x4 PlacementScatter::makeWorldMatrix( const PlacementInstance& instance, const float3& origin )
    {
        const float3   position{ origin._x + instance._planePosition._x, instance._height, origin._z + instance._planePosition._y };
        const float4x4 yawScale = float4x4::makeTrs( float3::Zero, float3{ 0.0f, instance._yaw, 0.0f }, float3{ instance._scale, instance._scale, instance._scale } );
        if ( instance._alignToNormal <= 0.0f )
            return yawScale * float4x4::makeTranslation( position );

        // 위쪽을 (위쪽에서 노멀로 가는 회전)만큼 alignToNormal 로 돌린다. 요는 기울이기 전에 적용한다(기울어진 축이 아니라 표면 위에서 돈다).
        const float3  normal = instance._normal.normalize();
        const float3  target = ( float3::Up * ( 1.0f - instance._alignToNormal ) + normal * instance._alignToNormal ).normalize();
        const float3  axis   = float3::Up.cross( target );
        const float32 sine   = axis.getLength();
        if ( sine <= MathUtil::kEpsilon )
            return yawScale * float4x4::makeTranslation( position );
        const float32 angle = MathUtil::atan2( sine, float3::Up.dot( target ) );
        return yawScale * float4x4::makeFromAxisAngle( axis * ( 1.0f / sine ), angle ) * float4x4::makeTranslation( position );
    }
} // namespace sw
