#include "pch.h"

#include "Engine/Object/Component/3D/PointLightComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/LightRegistry.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 이 TU 의 기본값들. **익명 네임스페이스에 벌거벗은 상수로 두면 안 된다** —
         *        유니티 빌드(CI-*)는 여러 .cpp 를 한 TU 로 합치고, 그러면 세 라이트 컴포넌트의
         *        `kDefaultColor` 가 같은 익명 네임스페이스에서 재정의된다(AGENTS.md 의 Internal 규칙).
         */
        struct PointLightComponentInternal
        {
            /// @brief 기본 점광 — 주광(따뜻한 색)과 구분되도록 차가운 색에서 출발한다.
            static constexpr float3  kDefaultColor{ 0.55f, 0.75f, 1.0f };
            static constexpr float32 kDefaultIntensity{ 2.0f };
            static constexpr float32 kDefaultRadius{ 6.0f };
        };
    } // namespace

    PointLightComponent::PointLightComponent()
        : _color{ PointLightComponentInternal::kDefaultColor }
        , _intensity{ PointLightComponentInternal::kDefaultIntensity }
        , _radius{ PointLightComponentInternal::kDefaultRadius }
    {
    }

    void PointLightComponent::setColor( const float3& color )
    {
        _color = color;
        onPropertyChanged( hashed_string( "_color" ) );
    }

    void PointLightComponent::setIntensity( float32 intensity )
    {
        _intensity = MathUtil::max( intensity, 0.0f );
        onPropertyChanged( hashed_string( "_intensity" ) );
    }

    void PointLightComponent::setRadius( float32 radius )
    {
        // 반경 0 은 셰이더에서 0 으로 나누는 자리다 — 아주 작은 값으로 막는다.
        _radius = MathUtil::max( radius, 0.01f );
        onPropertyChanged( hashed_string( "_radius" ) );
    }

    float3 PointLightComponent::getLightPosition() const
    {
        const float4x4 world = getWorldMatrix();
        return float3{ world._41, world._42, world._43 };
    }

    void PointLightComponent::onRegister( GameObjectManager& manager )
    {
        SceneComponent::onRegister( manager );
        manager.getLightRegistry().addPoint( this );
    }

    void PointLightComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getLightRegistry().removePoint( this );
        SceneComponent::onUnregister( manager );
    }
} // namespace sw
