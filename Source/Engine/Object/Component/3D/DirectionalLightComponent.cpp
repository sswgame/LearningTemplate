#include "pch.h"

#include "Engine/Object/Component/3D/DirectionalLightComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/LightRegistry.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 이 TU 의 기본값 모음입니다. **익명 네임스페이스에 상수를 그냥 두면 안 됩니다.**
         *        유니티 빌드(CI-*)는 여러 .cpp 를 한 TU 로 합치고, 그러면 세 라이트 컴포넌트의
         *        `kDefaultColor` 가 같은 익명 네임스페이스에서 재정의됩니다(AGENTS.md 의 Internal 규칙).
         */
        struct DirectionalLightComponentInternal
        {
            /// @brief 기본 주광 값입니다. 예전 FrameRendererConstants 의 상수와 같은 값에서 출발합니다.
            static constexpr float3  kDefaultColor{ 1.0f, 0.82f, 0.62f };
            static constexpr float32 kDefaultIntensity{ 1.35f };
            static constexpr float32 kDefaultAmbient{ 0.28f };
            static constexpr float32 kDefaultShadowExtent{ 2.0f / 0.9f };
            static constexpr float32 kDefaultShadowDistance{ 2.0f };
            /// @brief 회전이 없을 때의 기본 빛 방향입니다(위에서 비스듬히).
            static constexpr float3 kDefaultDirection{ -0.35f, -0.85f, -0.25f };
        };
    } // namespace

    DirectionalLightComponent::DirectionalLightComponent()
        : _color{ DirectionalLightComponentInternal::kDefaultColor }
        , _intensity{ DirectionalLightComponentInternal::kDefaultIntensity }
        , _ambient{ DirectionalLightComponentInternal::kDefaultAmbient }
        , _shadowExtent{ DirectionalLightComponentInternal::kDefaultShadowExtent }
        , _shadowDistance{ DirectionalLightComponentInternal::kDefaultShadowDistance }
        , _bCastShadow{ SW_TRUE }
        , _reservedLight{ 0 }
    {
    }

    float3 DirectionalLightComponent::getLightDirection() const
    {
        // 회전을 주지 않았으면(항등) 전방은 +Z 라 위에서 내리쬐는 그림이 안 된다.
        // 기본 방향을 쓰고, 회전이 있으면 그 회전을 적용한다.
        const float3 localRotation = getLocalRotation();
        if ( localRotation.getLengthSquared() <= MathUtil::Epsilon )
            return float3{ DirectionalLightComponentInternal::kDefaultDirection }.normalize();

        const float4x4 world   = getWorldMatrix();
        float3         forward = float3{ world._31, world._32, world._33 };
        if ( forward.getLengthSquared() <= MathUtil::Epsilon )
            return float3{ DirectionalLightComponentInternal::kDefaultDirection }.normalize();
        return forward.normalize();
    }

    void DirectionalLightComponent::setColor( const float3& color )
    {
        _color = color;
        onPropertyChanged( hashed_string( "_color" ) );
    }

    void DirectionalLightComponent::setIntensity( float32 intensity )
    {
        _intensity = MathUtil::max( intensity, 0.0f );
        onPropertyChanged( hashed_string( "_intensity" ) );
    }

    void DirectionalLightComponent::setAmbient( float32 ambient )
    {
        _ambient = MathUtil::max( ambient, 0.0f );
        onPropertyChanged( hashed_string( "_ambient" ) );
    }

    void DirectionalLightComponent::setShadowExtent( float32 extent )
    {
        _shadowExtent = MathUtil::max( extent, 0.01f );
        onPropertyChanged( hashed_string( "_shadowExtent" ) );
    }

    void DirectionalLightComponent::setShadowDistance( float32 distance )
    {
        _shadowDistance = MathUtil::max( distance, 0.01f );
        onPropertyChanged( hashed_string( "_shadowDistance" ) );
    }

    void DirectionalLightComponent::setCastShadow( bool bCastShadow )
    {
        _bCastShadow = bCastShadow ? SW_TRUE : SW_FALSE;
        onPropertyChanged( hashed_string( "_bCastShadow" ) );
    }

    float4x4 DirectionalLightComponent::buildShadowViewProj() const
    {
        const float3 lightDir = getLightDirection();

        // 라이트를 빛이 오는 쪽에 두고 빛 방향으로 원점을 내려다본다. up 이 빛과 거의 나란하면
        // side 축이 사라지므로 다른 축으로 갈아탄다.
        const float3 up  = MathUtil::abs( lightDir._y ) > 0.99f ? float3::Forward : float3::Up;
        const float3 eye = lightDir * -_shadowDistance;

        // **깊이 범위는 눈을 기준으로 잡는다.** 예전에는 `(-거리, +거리)` 였는데, 눈이 원점에서 거리만큼
        // 떨어져 원점을 보고 있으므로 씬의 뷰 z 는 거리 언저리다. `createOrthographic` 은
        // `z' = (z_view - near) / (far - near)` 라 그 범위에서는 씬 전체가 z' ≈ 1(원평면)로 뭉개진다.
        // 그러면 깊이 비교가 늘 "가려지지 않음" 이 되어 **그림자가 한 번도 진 적이 없었다**.
        // 원점에서 반경 `_shadowExtent` 안의 점은 뷰 z 가 [거리 - 반경, 거리 + 반경] 이므로 그대로 쓴다.
        // (거리가 반경보다 작으면 near 가 음수가 되는데, 직교 투영에는 문제가 되지 않는다. 선형 사상일 뿐이다.)
        const float32 extent    = _shadowExtent * 2.0f;
        const float32 nearPlane = _shadowDistance - _shadowExtent;
        const float32 farPlane  = _shadowDistance + _shadowExtent;
        return float4x4::createLookAt( eye, float3::Zero, up ) *
               float4x4::createOrthographic( extent, extent, nearPlane, farPlane );
    }

    void DirectionalLightComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getLightRegistry().addDirectional( this );
    }

    void DirectionalLightComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getLightRegistry().removeDirectional( this );
        SceneComponent::onUnregister( manager );
    }
} // namespace sw
