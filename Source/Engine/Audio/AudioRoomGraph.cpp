#include "pch.h"

#include "Engine/Audio/AudioRoomGraph.h"

namespace sw
{
    AudioRoomGraph::AudioRoomGraph()
        : _listRoom{}
        , _listPortal{}
        , _openPortalOcclusion{ 0.15f }
        , _wallLeak{ 0.0f }
        , _listTransmittance{}
        , _listVisited{}
        , _listViaPortal{}
    {
    }

    uint32 AudioRoomGraph::addRoom( const float3& center, const float3& halfExtents )
    {
        Room room;
        room._center      = center;
        room._halfExtents = float3( MathUtil::abs( halfExtents._x ), MathUtil::abs( halfExtents._y ), MathUtil::abs( halfExtents._z ) );
        room._volume      = 8.0f * room._halfExtents._x * room._halfExtents._y * room._halfExtents._z;
        _listRoom.push_back( room );
        return static_cast<uint32>( _listRoom.size() - 1 );
    }

    uint32 AudioRoomGraph::addPortal( uint32 roomA, uint32 roomB, const float3& position, float32 openness )
    {
        const bool bValidA             = roomA == kOutside || roomA < _listRoom.size();
        const bool bValidB             = roomB == kOutside || roomB < _listRoom.size();
        const bool bInvalidPortalRooms = bValidA == false || bValidB == false || roomA == roomB;
        if ( bInvalidPortalRooms )
            return invalid_index::kUint32;
        Portal portal;
        portal._position = position;
        portal._roomA    = roomA;
        portal._roomB    = roomB;
        portal._openness = MathUtil::saturate( openness );
        _listPortal.push_back( portal );
        return static_cast<uint32>( _listPortal.size() - 1 );
    }

    bool AudioRoomGraph::setPortalOpenness( uint32 portalIndex, float32 openness )
    {
        if ( portalIndex >= _listPortal.size() )
            return false;
        _listPortal[portalIndex]._openness = MathUtil::saturate( openness );
        return true;
    }

    void AudioRoomGraph::setWallLeak( float32 leak )
    {
        _wallLeak = MathUtil::saturate( leak );
    }

    void AudioRoomGraph::clear()
    {
        _listRoom.clear();
        _listPortal.clear();
    }

    uint32 AudioRoomGraph::findRoom( const float3& point ) const
    {
        uint32  found       = kOutside;
        float32 foundVolume = 0.0f;
        for ( uint32 roomIndex = 0; roomIndex < _listRoom.size(); ++roomIndex )
        {
            const Room&  room    = _listRoom[roomIndex];
            const float3 offset  = point - room._center;
            const bool   bInside = MathUtil::abs( offset._x ) <= room._halfExtents._x && MathUtil::abs( offset._y ) <= room._halfExtents._y &&
                                 MathUtil::abs( offset._z ) <= room._halfExtents._z;
            if ( bInside && ( found == kOutside || room._volume < foundVolume ) )
            {
                found       = roomIndex;
                foundVolume = room._volume;
            }
        }
        return found;
    }

    uint32 AudioRoomGraph::toNode( uint32 room ) const
    {
        return room == kOutside ? static_cast<uint32>( _listRoom.size() ) : room;
    }

    float32 AudioRoomGraph::computeBestTransmittance( uint32 fromNode, uint32 targetNode ) const
    {
        const size_t nodeCount = _listRoom.size() + 1;
        _listTransmittance.assign( nodeCount, 0.0f );
        _listVisited.assign( nodeCount, 0 );
        _listViaPortal.assign( nodeCount, invalid_index::kUint32 );
        _listTransmittance[fromNode] = 1.0f;
        const float32 openFactor     = 1.0f - _openPortalOcclusion;
        for ( size_t step = 0; step < nodeCount; ++step )
        {
            // 아직 안 본 노드 가운데 투과율이 가장 큰 것을 확정한다(곱이 줄기만 하므로 최대 곱 길 찾기가 맞다).
            uint32  current     = invalid_index::kUint32;
            float32 currentBest = 0.0f;
            for ( uint32 node = 0; node < nodeCount; ++node )
            {
                if ( _listVisited[node] == 0 && currentBest < _listTransmittance[node] )
                {
                    current     = node;
                    currentBest = _listTransmittance[node];
                }
            }
            if ( current == invalid_index::kUint32 || current == targetNode )
                break;
            _listVisited[current] = 1;
            for ( uint32 portalIndex = 0; portalIndex < _listPortal.size(); ++portalIndex )
            {
                const Portal& portal = _listPortal[portalIndex];
                const uint32  nodeA  = toNode( portal._roomA );
                const uint32  nodeB  = toNode( portal._roomB );
                if ( nodeA != current && nodeB != current )
                    continue;
                const uint32  other         = nodeA == current ? nodeB : nodeA;
                const float32 transmittance = currentBest * portal._openness * openFactor;
                if ( _listTransmittance[other] < transmittance )
                {
                    _listTransmittance[other] = transmittance;
                    _listViaPortal[other]     = portalIndex;
                }
            }
        }
        return _listTransmittance[targetNode];
    }

    AudioPropagation AudioRoomGraph::computePropagation( const float3& listener, const float3& emitter ) const
    {
        if ( _listRoom.empty() )
            return AudioPropagation::makeOcclusion( 0.0f );
        const uint32 listenerRoom = findRoom( listener );
        const uint32 emitterRoom  = findRoom( emitter );
        if ( listenerRoom == emitterRoom )
            return AudioPropagation::makeOcclusion( 0.0f );
        const uint32     listenerNode  = toNode( listenerRoom );
        const uint32     emitterNode   = toNode( emitterRoom );
        const float32    transmittance = computeBestTransmittance( listenerNode, emitterNode );
        AudioPropagation propagation   = AudioPropagation::makeOcclusion( MathUtil::saturate( 1.0f - MathUtil::max( transmittance, _wallLeak ) ) );
        // 벽으로 새는 소리가 문을 돌아 오는 소리보다 크면 소리는 에미터 쪽에서 곧게 온다(회절 없음).
        const bool bThroughPortals = 0.0f < transmittance && _wallLeak <= transmittance;
        if ( bThroughPortals == false )
            return propagation;

        // 에미터 쪽에서 거슬러 올라가며 지나는 문을 모은다. 노드 수보다 길 수 없다.
        float3  previous      = emitter;
        float3  nearestPortal = emitter;
        float3  outgoing      = float3( 0.0f, 0.0f, 0.0f );
        float32 pathLength    = 0.0f;
        float32 bendRadians   = 0.0f;
        uint32  node          = emitterNode;
        for ( size_t hop = 0; hop <= _listRoom.size() && node != listenerNode; ++hop )
        {
            const uint32 portalIndex = _listViaPortal[node];
            if ( portalIndex == invalid_index::kUint32 )
                return propagation; // 길 기록이 끊겼다 — 길 찾기가 목표에 닿지 않은 경우라 회절을 내지 않는다
            const Portal& portal  = _listPortal[portalIndex];
            float3        segment = previous - portal._position; // 문 → 소리 쪽(다음 점)
            const float32 length  = segment.getLength();
            pathLength += length;
            if ( length > 1e-4f )
            {
                segment /= length;
                if ( outgoing.getLengthSquared() > 0.0f )
                    bendRadians += MathUtil::acos( MathUtil::clamp( segment.dot( outgoing ), -1.0f, 1.0f ) );
                outgoing = segment;
            }
            previous           = portal._position;
            nearestPortal      = portal._position;
            const uint32 nodeA = toNode( portal._roomA );
            node               = nodeA == node ? toNode( portal._roomB ) : nodeA;
        }
        float3        toPortal       = nearestPortal - listener;
        const float32 listenerLength = toPortal.getLength();
        pathLength += listenerLength;
        if ( listenerLength <= 1e-4f )
            return propagation; // 리스너가 문 위에 있다 — 방향을 정할 수 없고 꺾임도 없다
        toPortal /= listenerLength;
        if ( outgoing.getLengthSquared() > 0.0f )
            bendRadians += MathUtil::acos( MathUtil::clamp( outgoing.dot( toPortal ), -1.0f, 1.0f ) );
        propagation._bRedirected      = true;
        propagation._apparentPosition = listener + toPortal * pathLength;
        propagation._diffraction      = MathUtil::saturate( bendRadians / MathUtil::kPi );
        return propagation;
    }
} // namespace sw
