#include "pch.h"

#include "Engine/Object/Component/3D/DirectionalLightComponent.h"

#include "Core/Math/MathUtil.h"

namespace sw
{
    namespace
    {
        /// @brief 기본 주광 — 예전 FrameRendererConstants 의 상수와 같은 값에서 출발한다.
        constexpr float3  kDefaultColor{ 1.0f, 0.82f, 0.62f };
        constexpr float32 kDefaultIntensity{ 1.35f };
        constexpr float32 kDefaultAmbient{ 0.28f };
        constexpr float32 kDefaultShadowExtent{ 2.0f / 0.9f };
        constexpr float32 kDefaultShadowDistance{ 2.0f };
        /// @brief 회전이 없을 때의 기본 빛 방향(위에서 비스듬히).
        constexpr float3 kDefaultDirection{ -0.35f, -0.85f, -0.25f };
    } // namespace

    DirectionalLightComponent::DirectionalLightComponent()
        : _color{ kDefaultColor }
        , _intensity{ kDefaultIntensity }
        , _ambient{ kDefaultAmbient }
        , _shadowExtent{ kDefaultShadowExtent }
        , _shadowDistance{ kDefaultShadowDistance }
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
            return float3{ kDefaultDirection }.normalize();

        const float4x4 world   = getWorldMatrix();
        float3         forward = float3{ world._31, world._32, world._33 };
        if ( forward.getLengthSquared() <= MathUtil::Epsilon )
            return float3{ kDefaultDirection }.normalize();
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

        const float32 extent = _shadowExtent * 2.0f;
        return float4x4::createLookAt( eye, float3::Zero, up ) *
               float4x4::createOrthographic( extent, extent, -_shadowDistance, _shadowDistance );
    }
} // namespace sw
