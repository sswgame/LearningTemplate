#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/LagCompensation.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    LagCompensationHistory::LagCompensationHistory( int32 capacity )
        : _listFrame{}
    {
        _listFrame.initialize( MathUtil::max( 2, capacity ) );
    }

    void LagCompensationHistory::record( uint32 tick, const vector<LagRecord>& listRecord ) { _listFrame.acquire( tick ) = listRecord; }

    const LagRecord* LagCompensationHistory::findRecord( const vector<LagRecord>& listRecord, uint32 entityID )
    {
        for ( const LagRecord& record : listRecord )
        {
            if ( record._entityID == entityID )
                return &record;
        }
        return nullptr;
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

    uint32 LagCompensationHistory::raycastAt( float32 tick, const float3& origin, const float3& direction, float32 maxDistance, uint32 ignoreEntityID,
                                              float32& outDistance ) const
    {
        const vector<LagRecord>* pNewest = _listFrame.hasNewest() ? _listFrame.find( _listFrame.getNewestTick() ) : nullptr;
        if ( pNewest == nullptr )
            return 0;
        uint32  bestID       = 0;
        float32 bestDistance = maxDistance;
        // 지금 있는 몸들을 그 시각으로 되감아 본다.
        for ( const LagRecord& current : *pNewest )
        {
            if ( current._entityID == ignoreEntityID )
                continue;
            LagRecord past;
            if ( sampleAt( tick, current._entityID, past ) == false )
                continue;
            // 광선 · 구.
            const float3  toCenter = past._position - origin;
            const float32 along    = toCenter.dot( direction );
            if ( along < 0.0f )
                continue;
            const float32 closestSquared = toCenter.getLengthSquared() - along * along;
            const float32 radiusSquared  = past._radius * past._radius;
            if ( closestSquared > radiusSquared )
                continue;
            const float32 hitDistance = along - MathUtil::sqrt( radiusSquared - closestSquared );
            if ( hitDistance >= 0.0f && hitDistance < bestDistance )
            {
                bestDistance = hitDistance;
                bestID       = current._entityID;
            }
        }
        outDistance = bestDistance;
        return bestID;
    }
} // namespace sw
