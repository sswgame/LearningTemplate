#include "pch.h"

#include "Engine/Object/Component/3D/SpotLightComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/LightRegistry.h"

namespace sw
{
    namespace
    {
        /// @brief 기본 스포트 — 점광(차가운 색)·주광(따뜻한 색)과 구분되도록 중간 색에서 출발한다.
        constexpr float3  kDefaultColor{ 1.0f, 0.95f, 0.7f };
        constexpr float32 kDefaultIntensity{ 3.0f };
        constexpr float32 kDefaultRadius{ 8.0f };
        /// @brief 기본 원뿔 — 안쪽 15도, 바깥 30도 (라디안).
        constexpr float32 kDefaultInnerCone{ 0.262f };
        constexpr float32 kDefaultOuterCone{ 0.524f };
        /// @brief 회전이 없을 때의 기본 방향 — 아래를 비춘다.
        constexpr float3 kDefaultDirection{ 0.0f, -1.0f, 0.0f };
        /// @brief 원뿔 반각 상한. 90도를 넘으면 원뿔이 뒤집혀 "빛이 뒤로도 나간다".
        constexpr float32 kMaxConeAngle{ 1.5533f }; // 89도
    } // namespace

    SpotLightComponent::SpotLightComponent()
        : _color{ kDefaultColor }
        , _intensity{ kDefaultIntensity }
        , _radius{ kDefaultRadius }
        , _innerConeAngle{ kDefaultInnerCone }
        , _outerConeAngle{ kDefaultOuterCone }
    {
    }

    void SpotLightComponent::setColor( const float3& color )
    {
        _color = color;
        onPropertyChanged( hashed_string( "_color" ) );
    }

    void SpotLightComponent::setIntensity( float32 intensity )
    {
        _intensity = MathUtil::max( intensity, 0.0f );
        onPropertyChanged( hashed_string( "_intensity" ) );
    }

    void SpotLightComponent::setRadius( float32 radius )
    {
        // 반경 0 은 셰이더에서 0 으로 나누는 자리다 — 아주 작은 값으로 막는다.
        _radius = MathUtil::max( radius, 0.01f );
        onPropertyChanged( hashed_string( "_radius" ) );
    }

    void SpotLightComponent::setInnerConeAngle( float32 radians )
    {
        // 안쪽이 바깥쪽보다 크면 감쇠 분모가 음수가 되어 원뿔이 뒤집힌다 — 여기서 자른다.
        _innerConeAngle = MathUtil::clamp( radians, 0.0f, _outerConeAngle );
        onPropertyChanged( hashed_string( "_innerConeAngle" ) );
    }

    void SpotLightComponent::setOuterConeAngle( float32 radians )
    {
        _outerConeAngle = MathUtil::clamp( radians, 0.0f, kMaxConeAngle );
        if ( _innerConeAngle > _outerConeAngle )
            _innerConeAngle = _outerConeAngle;
        onPropertyChanged( hashed_string( "_outerConeAngle" ) );
    }

    float3 SpotLightComponent::getLightPosition() const
    {
        const float4x4 world = getWorldMatrix();
        return float3{ world._41, world._42, world._43 };
    }

    float3 SpotLightComponent::getLightDirection() const
    {
        // 회전을 주지 않았으면(항등) 전방은 +Z 라 카메라 쪽으로 쏘게 된다 — 기본은 아래를 비춘다.
        // 규약은 DirectionalLightComponent 와 같다(전방 벡터가 곧 빛이 나아가는 방향).
        const float3 localRotation = getLocalRotation();
        if ( localRotation.getLengthSquared() <= MathUtil::Epsilon )
            return float3{ kDefaultDirection }.normalize();

        const float4x4 world   = getWorldMatrix();
        float3         forward = float3{ world._31, world._32, world._33 };
        if ( forward.getLengthSquared() <= MathUtil::Epsilon )
            return float3{ kDefaultDirection }.normalize();
        return forward.normalize();
    }

    void SpotLightComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getLightRegistry().addSpot( this );
    }

    void SpotLightComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getLightRegistry().removeSpot( this );
        SceneComponent::onUnregister( manager );
    }
} // namespace sw
