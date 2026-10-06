#include "pch.h"

#include "Engine/Object/Component/2D/Light2DComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GpuLight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    namespace
    {
        struct Light2DComponentInternal
        {
            static constexpr float3  kPointDefaultColor{ 1.0f, 0.85f, 0.6f };
            static constexpr float32 kPointDefaultIntensity{ 1.0f };
            static constexpr float3  kGlobalDefaultColor{ 1.0f, 1.0f, 1.0f };
            static constexpr float32 kGlobalDefaultIntensity{ 0.25f };
            static constexpr float32 kMinimumRadius{ 0.01f };

            /** @brief 전체 각(라디안)의 반각 cos 입니다. 2π 이상이면 −1(원뿔 없음)입니다. */
            static float32 computeHalfAngleCos( float32 angle )
            {
                const float32 clamped = MathUtil::clamp( angle, 0.0f, 2.0f * MathUtil::kPi );
                return MathUtil::cos( clamped * 0.5f );
            }
        };
    } // namespace

    PointLight2DComponent::PointLight2DComponent()
        : LightComponent( shaderslot::kLightTypePoint2D, Light2DComponentInternal::kPointDefaultColor, Light2DComponentInternal::kPointDefaultIntensity )
        , _innerRadius{ 0.0f }
        , _outerRadius{ 4.0f }
        , _falloffExponent{ 1.0f }
        , _innerAngle{ 2.0f * MathUtil::kPi }
        , _outerAngle{ 2.0f * MathUtil::kPi }
        , _normalMapHeight{ 1.0f }
        , _bCastShadows{ true }
    {
    }

    float32 PointLight2DComponent::computeAttenuation( float32 distance, float32 innerRadius, float32 outerRadius, float32 exponent )
    {
        const float32 outer = MathUtil::max( outerRadius, 1e-4f );
        const float32 inner = MathUtil::clamp( innerRadius, 0.0f, outer );
        if ( distance >= outer )
            return 0.0f;
        const float32 ramp = MathUtil::saturate( ( outer - distance ) / MathUtil::max( outer - inner, 1e-4f ) );
        return MathUtil::pow( ramp, MathUtil::max( exponent, 1e-3f ) );
    }

    void PointLight2DComponent::setRadius( float32 innerRadius, float32 outerRadius )
    {
        _outerRadius = MathUtil::max( outerRadius, Light2DComponentInternal::kMinimumRadius );
        _innerRadius = MathUtil::clamp( innerRadius, 0.0f, _outerRadius );
    }

    void PointLight2DComponent::setConeAngles( float32 innerAngle, float32 outerAngle )
    {
        _outerAngle = MathUtil::clamp( outerAngle, 0.0f, 2.0f * MathUtil::kPi );
        _innerAngle = MathUtil::clamp( innerAngle, 0.0f, _outerAngle );
    }

    void PointLight2DComponent::writeGpuLightKindFields( GpuLight& outLight ) const
    {
        using Internal           = Light2DComponentInternal;
        const float3 position    = getLightPosition();
        const float3 direction   = computeLightDirection( float3{ 1.0f, 0.0f, 0.0f } );
        float2       direction2D = float2{ direction._x, direction._y };
        if ( direction2D.getLengthSquared() <= MathUtil::kEpsilon )
            direction2D = float2{ 1.0f, 0.0f };
        direction2D.normalize();
        // 칸의 뜻은 lighting2d.hlsli 머리 주석의 표 — z 는 2D 거리에 쓰이지 않아 노멀 맵 높이를 싣는다.
        outLight._positionRadius = float4{ position._x, position._y, MathUtil::max( _normalMapHeight, 1e-3f ), _outerRadius };
        outLight._directionType  = float4{ direction2D._x, direction2D._y, _innerRadius, outLight._directionType._w };
        outLight._params         = float4{ _bCastShadows ? 1.0f : 0.0f, Internal::computeHalfAngleCos( _outerAngle ),
                                   Internal::computeHalfAngleCos( _innerAngle ), MathUtil::max( _falloffExponent, 1e-3f ) };
    }

    GlobalLight2DComponent::GlobalLight2DComponent()
        : LightComponent( shaderslot::kLightTypeGlobal2D, Light2DComponentInternal::kGlobalDefaultColor, Light2DComponentInternal::kGlobalDefaultIntensity )
    {
    }

    void GlobalLight2DComponent::writeGpuLightKindFields( GpuLight& /*outLight*/ ) const
    {
        // 색 · 세기(공통 칸)만 읽는다.
    }
} // namespace sw
