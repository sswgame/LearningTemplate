#include "pch.h"

#include "Engine/Object/Component/2D/ColliderTileComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    ColliderTileComponent::ColliderTileComponent()
        : _tileType{ 0 }
        , _collisionSide{ 0 }
    {
    }

    void ColliderTileComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::DuringPhysics );

        GameObject* pGameObject = getOwner();
        if ( pGameObject != nullptr )
            pGameObject->addTag( "TileCollider"_tag );
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
        return _tileType;
    }

    void ColliderTileComponent::setTileType( int32 type )
    {
        _tileType = type;
    }

    int32 ColliderTileComponent::getCollisionSide() const
    {
        return _collisionSide;
    }

    void ColliderTileComponent::setCollisionSide( int32 side )
    {
        _collisionSide = side;
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
            return false;

        return ( ( cSide & ( 1 << side ) ) == 0 );
    }

    bool ColliderTileComponent::checkCollision( const float2& point, const float2& size ) const
    {
        if ( isSolid() == false )
            return false;

        GameObject* pGameObject = getOwner();
        if ( pGameObject == nullptr )
            return false;

        SceneComponent* pSceneComp = pGameObject->getPrimarySceneComponent();
        if ( pSceneComp == nullptr )
            return false;

        const float3     pos = pSceneComp->getWorldPosition();
        constexpr float2 tileSize{ 32.0f, 32.0f };

        const float2 tileMin{ pos._x, pos._y };
        const float2 tileMax{ pos._x + tileSize._x, pos._y + tileSize._y };

        const float2 otherMin = point;
        const float2 otherMax{ point._x + size._x, point._y + size._y };

        return ( tileMin._x < otherMax._x && tileMax._x > otherMin._x &&
                 tileMin._y < otherMax._y && tileMax._y > otherMin._y );
    }
} // namespace sw
