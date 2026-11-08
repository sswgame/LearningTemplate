#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/LagCompensation.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        struct LagCompensationInternal
        {
            /** @brief 광선과 구의 첫 교차 거리입니다. 출발점이 구 안이거나 구가 뒤에 있으면 false 입니다. */
            [[nodiscard]] static bool intersectSphere( const float3& center, float32 radius, const float3& origin, const float3& direction, float32& outDistance )
            {
                const float3  toCenter = center - origin;
                const float32 along    = toCenter.dot( direction );
                if ( along < 0.0f )
                    return false;
                const float32 closestSquared = toCenter.getLengthSquared() - along * along;
                const float32 radiusSquared  = radius * radius;
                if ( closestSquared > radiusSquared )
                    return false;
                outDistance = along - MathUtil::sqrt( radiusSquared - closestSquared );
                return outDistance >= 0.0f;
            }
        };
    } // namespace

    LagCompensationHistory::LagCompensationHistory( int32 capacity )
        : _listFrame{}
    {
        _listFrame.initialize( MathUtil::max( 2, capacity ) );
    }

    void LagCompensationHistory::record( uint32 tick, const vector<LagRecord>& listRecord )
    {
        _listFrame.acquire( tick ) = listRecord;
    }

    const LagRecord* LagCompensationHistory::findRecord( const vector<LagRecord>& listRecord, uint32 entityID )
    {
        return VectorUtil::findBy<&LagRecord::_entityID>( listRecord, entityID );
    }

    bool LagCompensationHistory::sampleAt( float32 tick, uint32 entityID, LagRecord& outRecord ) const
    {
        if ( _listFrame.hasNewest() == false )
            return false;
        const uint32                   newestTick  = _listFrame.getNewestTick();
        const uint32                   oldestTick  = _listFrame.computeOldestTick();
        const float32                  clamped     = MathUtil::clamp( tick, static_cast<float32>( oldestTick ), static_cast<float32>( newestTick ) );
        const uint32                   lowTick     = static_cast<uint32>( clamped );
        const uint32                   highTick    = MathUtil::min( newestTick, lowTick + 1u );
        const vector<LagRecord>* const pLow        = _listFrame.find( lowTick );
        const vector<LagRecord>* const pHigh       = _listFrame.find( highTick );
        const LagRecord*               pLowRecord  = pLow != nullptr ? findRecord( *pLow, entityID ) : nullptr;
        const LagRecord*               pHighRecord = pHigh != nullptr ? findRecord( *pHigh, entityID ) : nullptr;
        if ( pLowRecord == nullptr && pHighRecord == nullptr )
            return false;
        if ( pLowRecord == nullptr || pHighRecord == nullptr )
        {
            outRecord = pLowRecord != nullptr ? *pLowRecord : *pHighRecord;
            return true;
        }
        const float32 alpha = clamped - static_cast<float32>( lowTick );
        outRecord           = *pLowRecord;
        outRecord._position = pLowRecord->_position + ( pHighRecord->_position - pLowRecord->_position ) * alpha;
        return true;
    }

    bool LagCompensationHistory::intersectBody( const LagRecord& body, const float3& origin, const float3& direction, float32& outDistance )
    {
        using Internal         = LagCompensationInternal;
        const float32 diameter = body._radius * 2.0f;
        if ( body._height <= 0.0f )
            return Internal::intersectSphere( body._position, body._radius, origin, direction, outDistance );
        const float3 bottom = body._position + float3{ 0.0f, body._radius, 0.0f };
        if ( body._height <= diameter )
            return Internal::intersectSphere( bottom, body._radius, origin, direction, outDistance );
        // 세로 원기둥(XZ 의 원) 옆면을 먼저 본다. 교차가 두 반구 사이 높이면 그것이 첫 교차다.
        const float32 axisLength = body._height - diameter;
        const float3  toOrigin   = origin - bottom;
        const float32 quadA      = direction._x * direction._x + direction._z * direction._z;
        if ( quadA > MathUtil::kEpsilon )
        {
            const float32 quadB        = toOrigin._x * direction._x + toOrigin._z * direction._z;
            const float32 quadC        = toOrigin._x * toOrigin._x + toOrigin._z * toOrigin._z - body._radius * body._radius;
            const float32 discriminant = quadB * quadB - quadA * quadC;
            if ( discriminant >= 0.0f )
            {
                const float32 distance = ( -quadB - MathUtil::sqrt( discriminant ) ) / quadA;
                const float32 height   = toOrigin._y + direction._y * distance;
                if ( distance >= 0.0f && 0.0f <= height && height <= axisLength )
                {
                    outDistance = distance;
                    return true;
                }
            }
        }
        // 옆면이 아니면 두 반구 중 가까운 쪽이다.
        float32    bottomDistance = 0.0f;
        float32    topDistance    = 0.0f;
        const bool bBottomHit     = Internal::intersectSphere( bottom, body._radius, origin, direction, bottomDistance );
        const bool bTopHit        = Internal::intersectSphere( bottom + float3{ 0.0f, axisLength, 0.0f }, body._radius, origin, direction, topDistance );
        if ( bBottomHit == false && bTopHit == false )
            return false;
        if ( bBottomHit && bTopHit )
            outDistance = MathUtil::min( bottomDistance, topDistance );
        else
            outDistance = bBottomHit ? bottomDistance : topDistance;
        return true;
    }

    uint32 LagCompensationHistory::raycastAt( float32 tick, const float3& origin, const float3& direction, float32 maxDistance, uint32 ignoreEntityID,
                                              float32& outDistance ) const
    {
        outDistance                      = maxDistance;
        const vector<LagRecord>* pNewest = _listFrame.hasNewest() ? _listFrame.find( _listFrame.getNewestTick() ) : nullptr;
        if ( pNewest == nullptr )
            return 0;
        uint32  bestID       = 0;
        float32 bestDistance = maxDistance;
        // 지금 있는 몸을 그 시각으로 되감는다.
        for ( const LagRecord& current : *pNewest )
        {
            if ( current._entityID == ignoreEntityID )
                continue;
            LagRecord past;
            if ( sampleAt( tick, current._entityID, past ) == false )
                continue;
            float32 hitDistance = 0.0f;
            if ( intersectBody( past, origin, direction, hitDistance ) && hitDistance < bestDistance )
            {
                bestDistance = hitDistance;
                bestID       = current._entityID;
            }
        }
        outDistance = bestDistance;
        return bestID;
    }
} // namespace sw
