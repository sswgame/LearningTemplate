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
            /** @brief 후보 하나가 꺼내는 난수 칸입니다. 칸을 더할 때는 끝에 붙인다 — 앞 칸의 값이 바뀌면 같은 씨앗의 배치가 바뀐다. */
            enum Lane : uint32
            {
                kLaneU = 0,
                kLaneV,
                kLaneDensity,
                kLaneEntry,
                kLaneScale,
                kLaneYaw,
            };

            /** @brief 최소 거리 격자의 칸 키입니다. 칸 크기가 최소 거리 / √2 라 칸 하나에 점은 많아야 하나입니다. */
            static int64 makeCellKey( int32 cellU, int32 cellV )
            {
                // 음수 칸(-1 …)을 부호 있는 채로 밀면 정의되지 않은 동작이다 — 부호 없는 32 비트로 바꿔 민다(값은 같다).
                return static_cast<int64>( ( static_cast<uint64>( static_cast<uint32>( cellU ) ) << 32 ) | static_cast<uint64>( static_cast<uint32>( cellV ) ) );
            }

            /** @brief @p position 에서 @p minDistance 안에 이미 놓인 점이 있으면 true 입니다. 칸 반경 2 까지 봅니다. */
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    uint32 PlacementScatter::hashCandidate( uint32 seed, uint32 candidateIndex, uint32 lane )
    {
        // murmur3 의 마무리 섞기를 세 값에 차례로 건다. 이웃한 번호 · 칸이 이웃한 값을 받지 않는다.
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
        using Internal = PlacementRuleInternal;
        outListInstance.clear();
        const float2  size{ region._max._x - region._min._x, region._max._y - region._min._y };
        const float32 area = size._x * size._y;
        if ( area <= 0.0f || rule._density <= 0.0f )
            return;

        const float64 wantedCount = static_cast<float64>( rule._density ) * static_cast<float64>( area );
        uint32        candidateCount{ kMaxCandidateCount };
        if ( wantedCount < static_cast<float64>( kMaxCandidateCount ) )
            candidateCount = static_cast<uint32>( MathUtil::ceil( wantedCount ) );
        else
            SW_LOG_WARNING( "Placement asks for %# candidates - clamped to %#", static_cast<uint64>( wantedCount ), kMaxCandidateCount );

        // 최소 거리 격자: 칸 = 최소 거리 / √2 라 칸 하나에 점이 많아야 하나다. 칸 반경 2 를 보면 최소 거리 안의 모든 점을 본다.
        const bool                  bPoisson = rule._minDistance > 0.0f;
        const float32               cellSize = bPoisson ? rule._minDistance * MathUtil::kInvSqrt2 : 1.0f;
        unordered_map<int64, int32> mapCellToPoint;
        vector<float2>              listPoint;

        const float32 scaleMin = MathUtil::min( rule._scaleMin, rule._scaleMax );
        const float32 scaleMax = MathUtil::max( rule._scaleMin, rule._scaleMax );
        for ( uint32 candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex )
        {
            const float2 position{ region._min._x + toUnit( hashCandidate( rule._seed, candidateIndex, Internal::kLaneU ) ) * size._x,
                                   region._min._y + toUnit( hashCandidate( rule._seed, candidateIndex, Internal::kLaneV ) ) * size._y };
            if ( isExcluded( listExclusion, position ) )
                continue;

            PlacementSurfaceSample sample;
            if ( pSurface != nullptr && ( pSurface->sampleSurface( position, sample ) == false || sample._bValid == SW_FALSE ) )
                continue;

            const float32 slope = computeSlope( sample._normal );
            if ( slope < rule._slopeMin || rule._slopeMax < slope )
                continue;
            if ( sample._height < rule._heightMin || rule._heightMax < sample._height )
                continue;
            const float32 arrWeight[4] = { sample._layerWeight._x, sample._layerWeight._y, sample._layerWeight._z, sample._layerWeight._w };
            const bool    bLayerFilter = 0 <= rule._layerIndex && rule._layerIndex < 4;
            if ( bLayerFilter && arrWeight[rule._layerIndex] < rule._layerMinWeight )
                continue;
            const bool bDensityLayer = 0 <= rule._densityLayerIndex && rule._densityLayerIndex < 4;
            if ( bDensityLayer && toUnit( hashCandidate( rule._seed, candidateIndex, Internal::kLaneDensity ) ) >= arrWeight[rule._densityLayerIndex] )
                continue;
            if ( bPoisson && Internal::isTooClose( mapCellToPoint, listPoint, position, cellSize, rule._minDistance ) )
                continue;

            const int32 entryIndex = pickEntry( listEntryWeight, toUnit( hashCandidate( rule._seed, candidateIndex, Internal::kLaneEntry ) ) );
            if ( entryIndex < 0 )
                return; // 비중 합이 0 — 놓을 것이 없다

            if ( bPoisson )
            {
                const int32 cellU = static_cast<int32>( MathUtil::floor( position._x / cellSize ) );
                const int32 cellV = static_cast<int32>( MathUtil::floor( position._y / cellSize ) );
                mapCellToPoint.emplace( Internal::makeCellKey( cellU, cellV ), static_cast<int32>( listPoint.size() ) );
                listPoint.push_back( position );
            }

            PlacementInstance instance;
            instance._planePosition = position;
            instance._height        = sample._height + rule._heightOffset;
            instance._normal        = sample._normal;
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
        const float4x4 yawScale = float4x4::createTrs( float3::Zero, float3{ 0.0f, instance._yaw, 0.0f }, float3{ instance._scale, instance._scale, instance._scale } );
        if ( instance._alignToNormal <= 0.0f )
            return yawScale * float4x4::createTranslation( position );

        // 위쪽을 (위쪽 → 노멀) 로 alignToNormal 만큼 돌린다. 요는 기울이기 전에 건다(기울어진 축이 아니라 표면 위에서 돈다).
        const float3  normal = instance._normal.normalize();
        const float3  target = ( float3::Up * ( 1.0f - instance._alignToNormal ) + normal * instance._alignToNormal ).normalize();
        const float3  axis   = float3::Up.cross( target );
        const float32 sine   = axis.getLength();
        if ( sine <= MathUtil::kEpsilon )
            return yawScale * float4x4::createTranslation( position );
        const float32 angle = MathUtil::atan2( sine, float3::Up.dot( target ) );
        return yawScale * float4x4::createFromAxisAngle( axis * ( 1.0f / sine ), angle ) * float4x4::createTranslation( position );
    }
} // namespace sw
