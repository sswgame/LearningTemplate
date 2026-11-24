#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/LagCompensation.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Network/Replication/InterpolationBuffer.h"

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

            /** @brief 두 선분의 최단 거리 제곱입니다(길이 0 인 선분은 점이다). */
            static float32 computeSegmentDistanceSquared( const float3& startA, const float3& endA, const float3& startB, const float3& endB )
            {
                const float3  alongA      = endA - startA;
                const float3  alongB      = endB - startB;
                const float3  between     = startA - startB;
                const float32 lengthA     = alongA.getLengthSquared();
                const float32 lengthB     = alongB.getLengthSquared();
                const float32 projectionB = alongB.dot( between );
                float32       ratioA      = 0.0f;
                float32       ratioB      = 0.0f;
                if ( lengthA > MathUtil::kEpsilon )
                {
                    const float32 projectionA = alongA.dot( between );
                    const float32 crossing    = alongA.dot( alongB );
                    const float32 determinant = lengthA * lengthB - crossing * crossing;
                    ratioA                    = determinant > MathUtil::kEpsilon ? MathUtil::saturate( ( crossing * projectionB - projectionA * lengthB ) / determinant ) : 0.0f; // 나란하면 아무 점에서 시작한다
                    ratioB                    = lengthB > MathUtil::kEpsilon ? MathUtil::saturate( ( crossing * ratioA + projectionB ) / lengthB ) : 0.0f;
                    ratioA                    = MathUtil::saturate( ( crossing * ratioB - projectionA ) / lengthA ); // 자른 B 에 가장 가까운 A 의 점
                }
                else if ( lengthB > MathUtil::kEpsilon )
                {
                    ratioB = MathUtil::saturate( projectionB / lengthB );
                }
                return ( startA + alongA * ratioA - startB - alongB * ratioB ).getLengthSquared();
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

    Ray LagBodyPose::moveRayTo( const LagBodyPose& now, const Ray& ray ) const
    {
        // 행 벡터 규약 — 과거 바디의 로컬로 옮긴 뒤 지금 바디의 월드로.
        const quaternion turn = quaternion::concatenate( _rotation.inverse(), now._rotation );
        Ray              moved;
        moved._origin    = now._position + float3::transform( ray._origin - _position, turn );
        moved._direction = float3::transform( ray._direction, turn );
        return moved;
    }

    bool LagRecord::mayIntersect( const Ray& ray, float32 maxDistance ) const
    {
        constexpr float32 kBodyMargin = 0.5f; // 바디 중심에서 셰이프 끝까지(머리 · 몸통 · 팔다리가 이 안이다)
        float3            center      = _position + float3{ 0.0f, _height * 0.5f, 0.0f };
        float32           radius      = MathUtil::max( _radius, _height * 0.5f );
        for ( const LagBodyPose& body : _listBody )
        {
            radius = MathUtil::max( radius, float3::getDistance( body._position, center ) + kBodyMargin );
        }
        float32 distance = 0.0f;
        return Ray::intersectSphere( ray, center, radius, maxDistance, distance );
    }

    bool LagCompensationHistory::sampleAt( float64 tick, uint32 entityID, LagRecord& outRecord ) const
    {
        if ( _listFrame.hasNewest() == false )
            return false;
        const uint32                   newestTick  = _listFrame.getNewestTick();
        const uint32                   oldestTick  = _listFrame.computeOldestTick();
        const float64                  clamped     = MathUtil::clamp( tick, static_cast<float64>( oldestTick ), static_cast<float64>( newestTick ) );
        const uint32                   lowTick     = static_cast<uint32>( clamped );
        const uint32                   highTick    = MathUtil::min( newestTick, lowTick + 1u );
        const vector<LagRecord>* const pLow        = _listFrame.find( lowTick );
        const vector<LagRecord>* const pHigh       = _listFrame.find( highTick );
        const LagRecord*               pLowRecord  = pLow != nullptr ? findRecord( *pLow, entityID ) : nullptr;
        const LagRecord*               pHighRecord = pHigh != nullptr ? findRecord( *pHigh, entityID ) : nullptr;
        NetSampleRatio                 ratio       = NetSampleRatio::make( lowTick, highTick, clamped );
        if ( ratio.isPresent( pLowRecord != nullptr, pHighRecord != nullptr ) == false )
            return false;
        if ( pLowRecord == nullptr || pHighRecord == nullptr )
        {
            outRecord = pLowRecord != nullptr ? *pLowRecord : *pHighRecord;
            return true;
        }
        ratio.breakAt( pLowRecord->_teleportCount, pHighRecord->_teleportCount );
        ratio.breakAt( pLowRecord->_frameNumber, pHighRecord->_frameNumber ); // 바닥을 옮겨 탄 두 자리는 기준이 다르다
        outRecord              = *pLowRecord;
        outRecord._frameNumber = ratio.choose( pLowRecord->_frameNumber, pHighRecord->_frameNumber );
        outRecord._position    = ratio.interpolate( pLowRecord->_position, pHighRecord->_position );
        if ( pLowRecord->_listBody.size() != pHighRecord->_listBody.size() )
        {
            outRecord._listBody = ratio.choose( pLowRecord->_listBody, pHighRecord->_listBody ); // 바디가 다시 만들어졌다 — 이을 짝이 없다
            return true;
        }
        for ( size_t bodyIndex = 0; bodyIndex < outRecord._listBody.size(); ++bodyIndex )
        {
            outRecord._listBody[bodyIndex]._position = ratio.interpolate( pLowRecord->_listBody[bodyIndex]._position, pHighRecord->_listBody[bodyIndex]._position );
            outRecord._listBody[bodyIndex]._rotation = ratio.interpolate( pLowRecord->_listBody[bodyIndex]._rotation, pHighRecord->_listBody[bodyIndex]._rotation );
        }
        return true;
    }

    void LagCompensationHistory::collectAt( float64 tick, uint32 ignoreEntityID, vector<LagRecord>& outListRecord ) const
    {
        outListRecord.clear();
        const vector<LagRecord>* pNewest = _listFrame.hasNewest() ? _listFrame.find( _listFrame.getNewestTick() ) : nullptr;
        if ( pNewest == nullptr )
            return;
        for ( const LagRecord& current : *pNewest )
        {
            LagRecord past;
            if ( current._entityID != ignoreEntityID && sampleAt( tick, current._entityID, past ) )
                outListRecord.push_back( std::move( past ) );
        }
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

    uint32 LagCompensationHistory::raycastAt( float64 tick, const float3& origin, const float3& direction, float32 maxDistance, uint32 ignoreEntityID,
                                              float32& outDistance ) const
    {
        outDistance                    = maxDistance;
        uint32            bestID       = 0;
        float32           bestDistance = maxDistance;
        vector<LagRecord> listPast; // 지금 있는 몸을 그 시각으로 되감는다
        collectAt( tick, ignoreEntityID, listPast );
        for ( const LagRecord& past : listPast )
        {
            float32 hitDistance = 0.0f;
            if ( intersectBody( past, origin, direction, hitDistance ) && hitDistance < bestDistance )
            {
                bestDistance = hitDistance;
                bestID       = past._entityID;
            }
        }
        outDistance = bestDistance;
        return bestID;
    }

    void LagCompensationHistory::collectTouchedAt( float64 tick, const float3& start, const float3& end, float32 radius, uint32 ignoreEntityID, vector<uint32>& outListEntityID ) const
    {
        vector<LagRecord> listPast;
        collectAt( tick, ignoreEntityID, listPast );
        collectTouched( listPast, start, end, radius, outListEntityID );
    }

    void LagCompensationHistory::collectTouched( const vector<LagRecord>& listRecord, const float3& start, const float3& end, float32 radius, vector<uint32>& outListEntityID )
    {
        for ( const LagRecord& past : listRecord )
        {
            const float3  bottom     = past._height > 0.0f ? past._position + float3{ 0.0f, past._radius, 0.0f } : past._position;
            const float32 axisLength = MathUtil::max( 0.0f, past._height - past._radius * 2.0f );
            const float32 reach      = past._radius + radius;
            if ( LagCompensationInternal::computeSegmentDistanceSquared( start, end, bottom, bottom + float3{ 0.0f, axisLength, 0.0f } ) <= reach * reach )
                outListEntityID.push_back( past._entityID );
        }
    }
} // namespace sw
