#include "pch.h"

#include "GameFramework/Kits/Network/NetClientServer/LagCompensation.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    LagCompensationHistory::LagCompensationHistory( int32 capacity )
        : _listFrame{}
        , _newestTick{ 0 }
        , _bHasFrame{ false }
    {
        _listFrame.resize( static_cast<size_t>( MathUtil::max( 2, capacity ) ) );
    }

    void LagCompensationHistory::record( uint32 tick, const vector<LagRecord>& listRecord )
    {
        Frame& frame      = _listFrame[static_cast<size_t>( tick % _listFrame.size() )];
        frame._tick       = tick;
        frame._listRecord = listRecord;
        if ( _bHasFrame == false || tick > _newestTick )
            _newestTick = tick;
        _bHasFrame = true;
    }

    const LagCompensationHistory::Frame* LagCompensationHistory::findFrame( uint32 tick ) const
    {
        const Frame& frame = _listFrame[static_cast<size_t>( tick % _listFrame.size() )];
        return frame._tick == tick ? &frame : nullptr;
    }

    const LagRecord* LagCompensationHistory::findRecord( const Frame& frame, uint32 entityId )
    {
        for ( const LagRecord& record : frame._listRecord )
        {
            if ( record._entityId == entityId )
                return &record;
        }
        return nullptr;
    }

    bool LagCompensationHistory::sampleAt( float32 tick, uint32 entityId, LagRecord& outRecord ) const
    {
        if ( _bHasFrame == false )
            return false;
        const uint32     oldestTick  = _newestTick >= _listFrame.size() - 1 ? _newestTick - static_cast<uint32>( _listFrame.size() - 1 ) : 0u;
        const float32    clamped     = MathUtil::clamp( tick, static_cast<float32>( oldestTick ), static_cast<float32>( _newestTick ) );
        const uint32     lowTick     = static_cast<uint32>( clamped );
        const uint32     highTick    = MathUtil::min( _newestTick, lowTick + 1u );
        const Frame*     pLow        = findFrame( lowTick );
        const Frame*     pHigh       = findFrame( highTick );
        const LagRecord* pLowRecord  = pLow != nullptr ? findRecord( *pLow, entityId ) : nullptr;
        const LagRecord* pHighRecord = pHigh != nullptr ? findRecord( *pHigh, entityId ) : nullptr;
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

    uint32 LagCompensationHistory::raycastAt( float32 tick, const float3& origin, const float3& direction, float32 maxDistance, uint32 ignoreEntityId,
                                              float32& outDistance ) const
    {
        const Frame* pNewest = _bHasFrame ? findFrame( _newestTick ) : nullptr;
        if ( pNewest == nullptr )
            return 0;
        uint32  bestId       = 0;
        float32 bestDistance = maxDistance;
        // 지금 있는 몸들을 그 시각으로 되감아 본다.
        for ( const LagRecord& current : pNewest->_listRecord )
        {
            if ( current._entityId == ignoreEntityId )
                continue;
            LagRecord past;
            if ( sampleAt( tick, current._entityId, past ) == false )
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
                bestId       = current._entityId;
            }
        }
        outDistance = bestDistance;
        return bestId;
    }
} // namespace sw
