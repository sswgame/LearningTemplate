#pragma once
/**
 * @file CollisionLayers.h
 * @brief 32-layer collision filter matrix.
 */
#include "Core/Common/CommonHeaders.h"
#include "Core/Common/CommonMacros.h"
#include "Core/Common/Types.h"

namespace sw
{
	/**
	 * @class CollisionLayers
	 * @brief Symmetric layer-vs-layer collision enable matrix (32 layers).
	 */
	class SW_API CollisionLayers
	{
	public:
		static constexpr uint32 kLayerCount = 32;

		CollisionLayers();

		/** @brief Enable/disable collision between layers a and b (symmetric). */
		void setLayerCollision( uint8 layerA, uint8 layerB, bool shouldCollide );

		bool shouldCollide( uint8 layerA, uint8 layerB ) const;

		/** @brief Bitmask of layers that collide with @p layer. */
		uint32 getLayerMask( uint8 layer ) const;

		/** @brief Reset to all-vs-all enabled. */
		void resetDefaults();

	private:
		uint32 _matrix[kLayerCount]{};
	};
} // namespace sw
