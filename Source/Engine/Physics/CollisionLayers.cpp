#include "pch.h"

#include "Engine/Physics/CollisionLayers.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
	CollisionLayers::CollisionLayers()
		: _arrMatrix{} { resetDefaults(); }

	void CollisionLayers::setLayerCollision( uint8 layerA, uint8 layerB, bool shouldCollide )
	{
		if ( layerA >= kLayerCount || layerB >= kLayerCount )
			return;

		const uint32 bitA = 1u << layerB;
		const uint32 bitB = 1u << layerA;
		if ( shouldCollide )
		{
			_arrMatrix[layerA] |= bitA;
			_arrMatrix[layerB] |= bitB;
		}
		else
		{
			_arrMatrix[layerA] &= ~bitA;
			_arrMatrix[layerB] &= ~bitB;
		}
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

	void CollisionLayers::resetDefaults()
	{
		for ( uint32 layerIndex = 0; layerIndex < kLayerCount; ++layerIndex )
		{
			_arrMatrix[layerIndex] = MathUtil::MaxUInt32;
		}
	}
} // namespace sw
