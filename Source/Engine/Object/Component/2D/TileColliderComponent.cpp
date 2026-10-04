#include "pch.h"

#include "Engine/Object/Component/2D/TileColliderComponent.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"

namespace sw
{
    TileColliderComponent::TileColliderComponent()
        : _tileType{ 0 }
        , _collisionSide{ 0 }
    {
    }

    void TileColliderComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        setTickGroup( TickGroup::DuringPhysics );
    }

    int32 TileColliderComponent::getTileType() const
    {
        return _tileType;
    }

    void TileColliderComponent::setTileType( int32 type )
    {
        _tileType = type;
    }

    int32 TileColliderComponent::getCollisionSide() const
    {
        return _collisionSide;
    }

    void TileColliderComponent::setCollisionSide( int32 side )
    {
        _collisionSide = side;
    }

    bool TileColliderComponent::isSolid() const
    {
        return getTileType() != 0;
    }

    bool TileColliderComponent::canPassFrom( int32 side ) const
    {
        const int32 tType = getTileType();
        if ( tType == 0 )
            return true;

        const int32 cSide = getCollisionSide();
        if ( cSide == 0 )
            return false;

        return ( ( cSide & ( 1 << side ) ) == 0 );
    }

    bool TileColliderComponent::overlapsBox( const float2& point, const float2& size ) const
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
