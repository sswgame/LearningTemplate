#include "pch.h"

#include "Engine/Object/Component/3D/PointLightComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/GPULight.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    namespace
    {
        /**
         * @brief 이 TU 의 기본값 모음입니다. **익명 네임스페이스에 상수를 그냥 두면 안 됩니다.**
         *        유니티 빌드(CI-*)는 여러 .cpp 를 한 TU 로 합치고, 그러면 세 라이트 컴포넌트의
         *        `kDefaultColor` 가 같은 익명 네임스페이스에서 재정의됩니다(AGENTS.md 의 Internal 규칙).
         */
        struct PointLightComponentInternal
        {
            /// @brief 기본 점광 값입니다. 주광(따뜻한 색)과 구분되도록 차가운 색에서 출발합니다.
            static constexpr float3  kDefaultColor{ 0.55f, 0.75f, 1.0f };
            static constexpr float32 kDefaultIntensity{ 2.0f };
            static constexpr float32 kDefaultRadius{ 6.0f };
        };
    } // namespace

    PointLightComponent::PointLightComponent()
        : LightComponent( shaderslot::kLightTypePoint, PointLightComponentInternal::kDefaultColor, PointLightComponentInternal::kDefaultIntensity )
        , _radius{ PointLightComponentInternal::kDefaultRadius }
    {
    }

    void PointLightComponent::setRadius( float32 radius )
    {
        // 반경 0 은 셰이더에서 0 으로 나누는 자리다. 아주 작은 값으로 막는다.
        _radius = MathUtil::max( radius, 0.01f );
        onPropertyChanged( hashed_string( "_radius" ) );
    }

    void PointLightComponent::writeGPULightKindFields( GPULight& outLight ) const
    {
        const float3 position    = getLightPosition();
        outLight._positionRadius = float4{ position._x, position._y, position._z, _radius };
    }
} // namespace sw
