/**
 * @file PhysicsComponentValidation.cpp
 * @brief `BoxCollider2DComponent`, `CharacterController2DComponent`, `CharacterControllerComponent`, `RigidBody2DComponent`, `RigidBodyComponent` 의 프로퍼티 검증 함수(`PROPERTY( Validate = fn )`)입니다. 에디터 데이터 검증이라 `Dev/` 에 두고 배포 구성은 짓지 않는다.
 * @details 생성 코드가 이 함수들을 `SW_DEV_ONLY_FUNCTION` 으로 등록해 배포는 참조하지 않는다(언리얼 `IsDataValid` 가 `WITH_EDITOR` 인 것과 같다).
 */
#include "pch.h"

#include "Engine/Physics/Component/BoxCollider2DComponent.h"
#include "Engine/Physics/Component/CharacterController2DComponent.h"
#include "Engine/Physics/Component/CharacterControllerComponent.h"
#include "Engine/Physics/Component/RigidBody2DComponent.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    void BoxCollider2DComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }

    void CharacterController2DComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }

    void CharacterControllerComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }

    void RigidBody2DComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }

    void RigidBody2DComponent::validateMaterial( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateMaterial( context, "_material", _material );
    }

    void RigidBodyComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }

    void RigidBodyComponent::validateMaterial( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateMaterial( context, "_material", _material );
    }
} // namespace sw
