/**
 * @file LightComponent.cpp
 * @brief 빛 공통 기반 구현입니다(색 · 세기 · 위치 · 방향 · 등록부 등록).
 */
#include "pch.h"

#include "Engine/Object/Component/3D/LightComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GpuLight.h"
#include "Engine/Object/GameObject/ComponentRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

namespace sw
{
    LightComponent::LightComponent( uint32 lightType, const float3& defaultColor, float32 defaultIntensity )
        : _color{ defaultColor }
        , _intensity{ defaultIntensity }
        , _lightType{ lightType }
    {
    }

    void LightComponent::setColor( const float3& color )
    {
        _color = color;
        onPropertyChanged( hashed_string( "_color" ) );
    }

    void LightComponent::setIntensity( float32 intensity )
    {
        _intensity = MathUtil::max( intensity, 0.0f );
        onPropertyChanged( hashed_string( "_intensity" ) );
    }

    float3 LightComponent::getLightPosition() const
    {
        const float4x4 world = getWorldMatrix();
        return float3{ world._41, world._42, world._43 };
    }

    void LightComponent::writeGpuLight( GpuLight& outLight ) const
    {
        outLight                 = GpuLight{};
        outLight._colorIntensity = float4{ _color._x, _color._y, _color._z, _intensity };
        outLight._directionType  = float4{ 0.0f, 0.0f, 0.0f, static_cast<float32>( _lightType ) };
        writeGpuLightKindFields( outLight );
    }

    float3 LightComponent::computeLightDirection( const float3& defaultLocalDirection ) const
    {
        // 기본 방향을 월드 행렬의 3x3 으로 돌린다(행 벡터 규약). 회전이 없으면 그대로, 부모가 돌면 따라 돈다.
        // 반환 변수는 하나다. 경로마다 다른 객체를 돌려주면 복사 생략(NRVO)이 걸리지 않는다(-Wnrvo).
        const float3 localDirection = float3{ defaultLocalDirection }.normalize();
        float3       worldDirection = float3::transformVector( localDirection, getWorldMatrix() );
        if ( worldDirection.getLengthSquared() <= MathUtil::Epsilon )
            worldDirection = localDirection;
        else
            worldDirection.normalize();
        return worldDirection;
    }

    void LightComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getComponentRegistry().add<LightComponent>( this, getLightType() ); // 칸은 종류(방향광이 0 — 종류 순서로 돌면 앞에 온다)
    }

    void LightComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getComponentRegistry().remove<LightComponent>( this, getLightType() );
        SceneComponent::onUnregister( manager );
    }
} // namespace sw
