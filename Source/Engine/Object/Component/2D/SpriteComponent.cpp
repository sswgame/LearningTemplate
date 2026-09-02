#include "pch.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"

#include "Engine/Object/Component/TagSystem.h"

namespace sw
{
    SpriteComponent::SpriteComponent()
        : _meshName{}
        , _materialName{}
        , _textureName{}
        , _spriteName{}
    {
    }

    void SpriteComponent::onBeginPlay()
    {
        MeshComponent::onBeginPlay();
        setTickGroup( TickGroup::PrePhysics );

        GameObject* pGameObject = getOwner();
        if ( pGameObject != nullptr )
            pGameObject->addTag( "Sprite"_tag );
    }

    void SpriteComponent::onEndPlay()
    {
        MeshComponent::onEndPlay();
    }

    void SpriteComponent::onTick( float32 deltaTime )
    {
        MeshComponent::onTick( deltaTime );
    }

} // namespace sw
