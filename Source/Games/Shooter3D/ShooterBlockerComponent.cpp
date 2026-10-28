#include "pch.h"

#include "Games/Shooter3D/ShooterBlockerComponent.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"

namespace sw
{
    ShooterBlockerComponent::ShooterBlockerComponent()
        : _halfSize{ 0.5f, 0.5f, 0.5f }
        , _bodyLayer{ "Static" }
    {
    }

    void ShooterBlockerComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        // 강체는 이 상자의 런타임 사본이다 — 다시 시작(핫 리로드)하면 만든 것을 다시 맞춘다. 붙이지 않은 씬 컴포넌트라 월드 배율이 1 이고, 셰이프 반 크기가 곧 상자다.
        RigidBodyComponent* pBody = pOwner->getComponent<RigidBodyComponent>();
        if ( pBody == nullptr )
            pBody = pOwner->addComponent<RigidBodyComponent>();
        if ( pBody == nullptr )
        {
            SW_LOG_ERROR( "Blocker '%#' could not add its static body - shots and ragdolls pass through it", pOwner->getName().c_str() );
            return;
        }
        const ShooterArenaBox box = computeBox();
        PhysicsShapeDesc3D    shape;
        shape._type        = PhysicsShapeType3D::Box;
        shape._halfExtents = _halfSize;
        pBody->setBodyType( PhysicsBodyType::Static );
        pBody->setLayer( _bodyLayer );
        pBody->setShape( shape );
        pBody->setWorldTransform( float4x4::makeTranslation( ( box._min + box._max ) * 0.5f ) );
    }

    ShooterArenaBox ShooterBlockerComponent::computeBox() const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pScene = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        const float3          center = pScene != nullptr ? pScene->getWorldPosition() : float3{ 0.0f, 0.0f, 0.0f };
        ShooterArenaBox       box{};
        box._min = center - _halfSize;
        box._max = center + _halfSize;
        return box;
    }
} // namespace sw
