#include "pch.h"

#include "Engine/Environment/Placement/PlacementRule.h"

#include "Core/Container/unordered_map.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

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

            /** @brief 최소 거리 격자의 셀 키입니다. 셀 크기가 최소 거리 / √2 라 셀 하나에 점이 많아야 하나입니다. */
            static int64 makeCellKey( int32 cellU, int32 cellV )
            {
                // 음수 셀(-1 등)을 부호 있는 채로 밀면 정의되지 않은 동작이다. 부호 없는 32 비트로 바꿔서 민다(값은 같다).
                return static_cast<int64>( ( static_cast<uint64>( static_cast<uint32>( cellU ) ) << 32 ) | static_cast<uint64>( static_cast<uint32>( cellV ) ) );
            }

            /** @brief @p position 에서 @p minDistance 안에 이미 놓인 점이 있으면 true 를 반환합니다. 셀 반경 2 까지 봅니다. */
            static bool isTooClose( const unordered_map<int64, int32>& mapCellToPoint, const vector<float2>& listPoint, const float2& position,
                                    float32 cellSize, float32 minDistance )
            {
                const int32   cellU             = static_cast<int32>( MathUtil::floor( position._x / cellSize ) );
                const int32   cellV             = static_cast<int32>( MathUtil::floor( position._y / cellSize ) );
                const float32 minDistanceSquare = minDistance * minDistance;
                for ( int32 offsetV = -2; offsetV <= 2; ++offsetV )
                {
                    for ( int32 offsetU = -2; offsetU <= 2; ++offsetU )
                    {
                        const auto iter = mapCellToPoint.find( makeCellKey( cellU + offsetU, cellV + offsetV ) );
                        if ( iter == mapCellToPoint.end() )
                            continue;
                        const float2& other  = listPoint[static_cast<size_t>( iter->second )];
                        const float32 deltaU = other._x - position._x;
                        const float32 deltaV = other._y - position._y;
                        if ( deltaU * deltaU + deltaV * deltaV < minDistanceSquare )
                            return true;
                    }
                }
                return false;
            }

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
                const float32 arrWeight[4] = { sample._layerWeight._x, sample._layerWeight._y, sample._layerWeight._z, sample._layerWeight._w };
                const bool    bLayerFilter = 0 <= rule._layerIndex && rule._layerIndex < 4;
                if ( bLayerFilter && arrWeight[rule._layerIndex] < rule._layerMinWeight )
                    return;
                const bool bDensityLayer = 0 <= rule._densityLayerIndex && rule._densityLayerIndex < 4;
                if ( bDensityLayer && PlacementScatter::toUnit( PlacementScatter::hashCandidate( rule._seed, candidateIndex, kLaneDensity ) ) >= arrWeight[rule._densityLayerIndex] )
                    return;
                outCandidate._normal  = sample._normal;
                outCandidate._height  = sample._height;
                outCandidate._bPassed = SW_TRUE;
            }
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
        for ( uint32 candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex )
        {
            const float2 position = Internal::computeCandidatePosition( rule, region, candidateIndex );
            Internal::evaluateCandidate( rule, pSurface, listExclusion, position, candidateIndex, outListCandidate[candidateIndex] );
        }
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
        for ( uint32 candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex )
        {
            const float2 position = Internal::computeCandidatePosition( rule, region, candidateIndex );
            if ( dirtyArea.contains( position ) )
                Internal::evaluateCandidate( rule, pSurface, listExclusion, position, candidateIndex, inoutListCandidate[candidateIndex] );
        }
        return true;
    }

    void PlacementScatter::resolveCandidates( const PlacementRule& rule, const PlacementRegion& region, const vector<PlacementCandidate>& listCandidate,
                                              const vector<float32>& listEntryWeight, vector<PlacementInstance>& outListInstance )
    {
        using Internal = PlacementRuleInternal;
        outListInstance.clear();

        // 최소 거리 격자: 셀 = 최소 거리 / √2 라 셀 하나에 점이 많아야 하나다. 셀 반경 2 를 보면 최소 거리 안의 모든 점을 본다.
        const bool                  bPoisson = rule._minDistance > 0.0f;
        const float32               cellSize = bPoisson ? rule._minDistance * MathUtil::kInvSqrt2 : 1.0f;
        unordered_map<int64, int32> mapCellToPoint;
        vector<float2>              listPoint;

        const float32 scaleMin       = MathUtil::min( rule._scaleMin, rule._scaleMax );
        const float32 scaleMax       = MathUtil::max( rule._scaleMin, rule._scaleMax );
        const uint32  candidateCount = static_cast<uint32>( listCandidate.size() );
        for ( uint32 candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex )
        {
            const PlacementCandidate& candidate = listCandidate[candidateIndex];
            if ( candidate._bPassed == SW_FALSE )
                continue;
            const float2 position = Internal::computeCandidatePosition( rule, region, candidateIndex );
            if ( bPoisson && Internal::isTooClose( mapCellToPoint, listPoint, position, cellSize, rule._minDistance ) )
                continue;

            const int32 entryIndex = pickEntry( listEntryWeight, toUnit( hashCandidate( rule._seed, candidateIndex, Internal::kLaneEntry ) ) );
            if ( entryIndex < 0 )
                return; // 비중 합이 0 이면 놓을 것이 없다.

            if ( bPoisson )
            {
                const int32 cellU = static_cast<int32>( MathUtil::floor( position._x / cellSize ) );
                const int32 cellV = static_cast<int32>( MathUtil::floor( position._y / cellSize ) );
                mapCellToPoint.emplace( Internal::makeCellKey( cellU, cellV ), static_cast<int32>( listPoint.size() ) );
                listPoint.push_back( position );
            }

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
