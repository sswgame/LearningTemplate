#include "pch.h"

#include "Engine/Object/Component/2D/SpriteComponent.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

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

    bool SpriteComponent::getWorldBounds( float3& outCenter, float32& outRadius ) const
    {
        // 스프라이트는 XY 평면의 단위 사각형이다. Z 스케일은 두께가 없으니 보지 않는다.
        constexpr float32 kUnitQuadHalfDiagonal = 0.70710678f;
        const float4x4    world                 = getWorldMatrix();
        const float3      scale                 = world.getScale();
        outCenter                               = world.getTranslation();
        outRadius                               = kUnitQuadHalfDiagonal * MathUtil::max( scale._x, scale._y );
        return true;
    }

} // namespace sw
