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

    uint32 AudioRoomGraph::addPortal( uint32 roomA, uint32 roomB, float32 openness )
    {
        const bool bValidA             = roomA == kOutside || roomA < _listRoom.size();
        const bool bValidB             = roomB == kOutside || roomB < _listRoom.size();
        const bool bInvalidPortalRooms = bValidA == false || bValidB == false || roomA == roomB;
        if ( bInvalidPortalRooms )
            return invalid_index::kUint32;
        Portal portal;
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

    float32 AudioRoomGraph::getPortalOpenness( uint32 portalIndex ) const
    {
        return portalIndex < _listPortal.size() ? _listPortal[portalIndex]._openness : 0.0f;
    }

    void AudioRoomGraph::setOpenPortalOcclusion( float32 occlusion )
    {
        _openPortalOcclusion = MathUtil::saturate( occlusion );
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
            for ( const Portal& portal : _listPortal )
            {
                const uint32 nodeA = toNode( portal._roomA );
                const uint32 nodeB = toNode( portal._roomB );
                if ( nodeA != current && nodeB != current )
                    continue;
                const uint32  other         = nodeA == current ? nodeB : nodeA;
                const float32 transmittance = currentBest * portal._openness * openFactor;
                if ( _listTransmittance[other] < transmittance )
                    _listTransmittance[other] = transmittance;
            }
        }
        return _listTransmittance[targetNode];
    }

    float32 AudioRoomGraph::computeOcclusion( const float3& listener, const float3& emitter ) const
    {
        if ( _listRoom.empty() )
            return 0.0f;
        const uint32 listenerRoom = findRoom( listener );
        const uint32 emitterRoom  = findRoom( emitter );
        if ( listenerRoom == emitterRoom )
            return 0.0f;
        const float32 transmittance = computeBestTransmittance( toNode( listenerRoom ), toNode( emitterRoom ) );
        return MathUtil::saturate( 1.0f - MathUtil::max( transmittance, _wallLeak ) );
    }
} // namespace sw
