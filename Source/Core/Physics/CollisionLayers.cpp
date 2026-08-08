/**
 * @file CollisionLayers.cpp
 */
#include "CollisionLayers.h"

namespace sw
{
	CollisionLayers::CollisionLayers()
	{
		resetDefaults();
	}

	void CollisionLayers::resetDefaults()
	{
		for ( uint32 i = 0; i < kLayerCount; ++i )
			_matrix[i] = 0xffffffffu;
	}

	void CollisionLayers::setLayerCollision( uint8 layerA, uint8 layerB, bool shouldCollide )
	{
		if ( layerA >= kLayerCount || layerB >= kLayerCount )
			return;

		const uint32 bitA = 1u << layerB;
		const uint32 bitB = 1u << layerA;
		if ( shouldCollide )
		{
			_matrix[layerA] |= bitA;
			_matrix[layerB] |= bitB;
		}
		else
		{
			_matrix[layerA] &= ~bitA;
			_matrix[layerB] &= ~bitB;
		}
	}

	bool CollisionLayers::shouldCollide( uint8 layerA, uint8 layerB ) const
	{
		if ( layerA >= kLayerCount || layerB >= kLayerCount )
			return false;
		return ( _matrix[layerA] & ( 1u << layerB ) ) != 0;
	}

	uint32 CollisionLayers::getLayerMask( uint8 layer ) const
	{
		if ( layer >= kLayerCount )
			return 0;
		return _matrix[layer];
	}
} // namespace sw
