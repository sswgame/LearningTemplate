/**
 * @file ColliderTileComponent.h
 * @brief 2D Tile Collider Component
 */
#pragma once
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

namespace sw
{
	/**
	 * @brief Pure ECS Data Struct for Tile Collider
	 */
	REFLECT()
	struct SW_API ColliderTileData
	{
		REFLECT_BODY();
		int32 tileType{ 0 };
		int32 collisionSide{ 0 };
	};

	REFLECT()
	class SW_API ColliderTileComponent : public Component
	{
	public:
		REFLECT_BODY();
		ColliderTileComponent()												 = default;
		virtual ~ColliderTileComponent() override							 = default;
		ColliderTileComponent( ColliderTileComponent&& ) noexcept			 = default;
		ColliderTileComponent& operator=( ColliderTileComponent&& ) noexcept = default;

		void onBeginPlay() override;
		void onEndPlay() override;
		void onTick( float32 deltaTime ) override;

		int32 getTileType() const;
		void  setTileType( int32 type );

		int32 getCollisionSide() const;
		void  setCollisionSide( int32 side );

		bool isSolid() const;
		bool canPassFrom( int32 side ) const;
		bool checkCollision( const float2& point, const float2& size ) const;

		ColliderTileData* getTileData() const;
	};
} // namespace sw
