#include "pch.h"

#include "Engine/Object/Component/2D/ColliderTileComponent.h"
#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
	void ColliderTileComponent::onBeginPlay()
	{
		Component::onBeginPlay();
		setTickGroup( TickGroup::DuringPhysics );

		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
		{
			pGameObject->addTag( "TileCollider"_tag );
			if ( pGameObject->getComponent<ColliderTileData>() == nullptr )
				pGameObject->addComponent<ColliderTileData>();
		}
	}

	void ColliderTileComponent::onEndPlay()
	{
		Component::onEndPlay();
	}

	void ColliderTileComponent::onTick( float32 deltaTime )
	{
		Component::onTick( deltaTime );
	}

	int32 ColliderTileComponent::getTileType() const
	{
		const ColliderTileData* pData = getTileData();
		if ( pData != nullptr )
			return pData->tileType;
		return 0;
	}

	void ColliderTileComponent::setTileType( int32 type )
	{
		ColliderTileData* pData = getTileData();
		if ( pData != nullptr )
			pData->tileType = type;
	}

	int32 ColliderTileComponent::getCollisionSide() const
	{
		const ColliderTileData* pData = getTileData();
		if ( pData != nullptr )
			return pData->collisionSide;
		return 0;
	}

	void ColliderTileComponent::setCollisionSide( int32 side )
	{
		ColliderTileData* pData = getTileData();
		if ( pData != nullptr )
			pData->collisionSide = side;
	}

	bool ColliderTileComponent::isSolid() const
	{
		return getTileType() != 0;
	}

	bool ColliderTileComponent::canPassFrom( int32 side ) const
	{
		const int32 tType = getTileType();
		if ( tType == 0 )
			return true;

		const int32 cSide = getCollisionSide();
		if ( cSide == 0 )
			return false; // Solid on all sides

		return ( ( cSide & ( 1 << side ) ) == 0 );
	}

	bool ColliderTileComponent::checkCollision( const float2& point, const float2& size ) const
	{
		if ( isSolid() == false )
			return false;

		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
		{
			SceneComponent* pSceneComp = pGameObject->getPrimarySceneComponent();
			if ( pSceneComp != nullptr )
			{
				const float3	 pos = pSceneComp->getWorldPosition();
				constexpr float2 tileSize{ 32.0f, 32.0f };

				const float2 tileMin{ pos._x, pos._y };
				const float2 tileMax{ pos._x + tileSize._x, pos._y + tileSize._y };

				const float2 otherMin = point;
				const float2 otherMax{ point._x + size._x, point._y + size._y };

				return ( tileMin._x < otherMax._x && tileMax._x > otherMin._x &&
						 tileMin._y < otherMax._y && tileMax._y > otherMin._y );
			}
		}
		return false;
	}

	ColliderTileData* ColliderTileComponent::getTileData() const
	{
		GameObject* pGameObject = getOwner();
		if ( pGameObject != nullptr )
			return pGameObject->getComponent<ColliderTileData>().get();
		return nullptr;
	}
} // namespace sw
