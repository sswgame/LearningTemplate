/**
 * @file TerrainComponentValidation.cpp
 * @brief `TerrainColliderComponent`, `TerrainComponent` 의 프로퍼티 검증 함수(`PROPERTY( Validate = fn )`)입니다. 에디터 데이터 검증이라 `Dev/` 에 두고 배포 구성은 짓지 않는다.
 * @details 생성 코드가 이 함수들을 `SW_DEV_ONLY_FUNCTION` 으로 등록해 배포는 참조하지 않는다(언리얼 `IsDataValid` 가 `WITH_EDITOR` 인 것과 같다).
 */
#include "pch.h"

#include "Engine/Environment/Terrain/TerrainColliderComponent.h"
#include "Engine/Environment/Terrain/TerrainComponent.h"
#include "Engine/Physics/Scene/PhysicsNameValidation.h"
#include "Engine/Reflection/ReflectionValidation.h"

namespace sw
{
    void TerrainColliderComponent::validateLayer( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateLayer( context, "_layer", _layer );
    }

    void TerrainColliderComponent::validateMaterial( ValidationContext& context ) const
    {
        PhysicsNameValidation::validateMaterial( context, "_material", _material );
    }

    void TerrainComponent::validatePlacement( ValidationContext& context ) const
    {
        const float4x4    world        = getWorldMatrix();
        const float3      axisX        = float3::transformVector( float3{ 1.0f, 0.0f, 0.0f }, world );
        const float3      axisY        = float3::transformVector( float3{ 0.0f, 1.0f, 0.0f }, world );
        const float3      axisZ        = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world );
        constexpr float32 kTolerance   = 1.0e-3f;
        const bool        bAxisAligned = ( axisX - float3{ 1.0f, 0.0f, 0.0f } ).getLength() <= kTolerance && ( axisY - float3{ 0.0f, 1.0f, 0.0f } ).getLength() <= kTolerance &&
                                  ( axisZ - float3{ 0.0f, 0.0f, 1.0f } ).getLength() <= kTolerance;
        if ( bAxisAligned == false )
            context.addWarning( "The terrain uses only its world position - rotation and scale on it or its parents are not applied (drawing, collision, layer and surface queries stay axis aligned)" );
    }
} // namespace sw
