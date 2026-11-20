#include "pch.h"

#include "Engine/Physics/Collision/CollisionLayers.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    CollisionLayers::CollisionLayers()
        : _arrMatrix{}
        , _arrOverlap{}
    {
        resetDefaults();
    }

    void CollisionLayers::setLayerCollision( uint8 layerA, uint8 layerB, bool shouldCollide )
    {
        setResponse( layerA, layerB, shouldCollide ? CollisionResponse::Block : CollisionResponse::Ignore );
    }

    void CollisionLayers::setResponse( uint8 layerA, uint8 layerB, CollisionResponse response )
    {
        if ( layerA >= kLayerCount || layerB >= kLayerCount )
            return;
        const uint32 bitA      = 1u << layerB;
        const uint32 bitB      = 1u << layerA;
        const bool   bCollides = response != CollisionResponse::Ignore;
        const bool   bOverlaps = response == CollisionResponse::Overlap;
        _arrMatrix[layerA]     = bCollides ? ( _arrMatrix[layerA] | bitA ) : ( _arrMatrix[layerA] & ~bitA );
        _arrMatrix[layerB]     = bCollides ? ( _arrMatrix[layerB] | bitB ) : ( _arrMatrix[layerB] & ~bitB );
        _arrOverlap[layerA]    = bOverlaps ? ( _arrOverlap[layerA] | bitA ) : ( _arrOverlap[layerA] & ~bitA );
        _arrOverlap[layerB]    = bOverlaps ? ( _arrOverlap[layerB] | bitB ) : ( _arrOverlap[layerB] & ~bitB );
    }

    CollisionResponse CollisionLayers::getResponse( uint8 layerA, uint8 layerB ) const
    {
        if ( layerA >= kLayerCount || layerB >= kLayerCount || ( _arrMatrix[layerA] & ( 1u << layerB ) ) == 0 )
            return CollisionResponse::Ignore;
        return ( _arrOverlap[layerA] & ( 1u << layerB ) ) != 0 ? CollisionResponse::Overlap : CollisionResponse::Block;
    }

    bool CollisionLayers::shouldCollide( uint8 layerA, uint8 layerB ) const
    {
        if ( layerA >= kLayerCount || layerB >= kLayerCount )
            return false;
        return ( _arrMatrix[layerA] & ( 1u << layerB ) ) != 0;
    }

    uint32 CollisionLayers::getLayerMask( uint8 layer ) const
    {
        if ( layer >= kLayerCount )
            return 0;
        return _arrMatrix[layer];
    }

    uint32 CollisionLayers::getOverlapMask( uint8 layer ) const
    {
        if ( layer >= kLayerCount )
            return 0;
        return _arrOverlap[layer];
    }

    void CollisionLayers::resetDefaults()
    {
        for ( uint32 layerIndex = 0; layerIndex < kLayerCount; ++layerIndex )
        {
            _arrMatrix[layerIndex]  = MathUtil::kMaxUInt32;
            _arrOverlap[layerIndex] = 0;
        }
    }
} // namespace sw
