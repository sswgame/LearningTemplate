#include "pch.h"

#include "Engine/Object/Component/3D/SpotLightComponent.h"

#include "Core/Math/MathUtil.h"

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
        struct SpotLightComponentInternal
        {
            /// @brief 기본 스포트 값입니다. 점광(차가운 색) · 주광(따뜻한 색)과 구분되도록 중간 색에서 출발합니다.
            static constexpr float3  kDefaultColor{ 1.0f, 0.95f, 0.7f };
            static constexpr float32 kDefaultIntensity{ 3.0f };
            static constexpr float32 kDefaultRadius{ 8.0f };
            /// @brief 기본 원뿔 각도입니다. 안쪽 15도, 바깥 30도(라디안).
            static constexpr float32 kDefaultInnerCone{ 0.262f };
            static constexpr float32 kDefaultOuterCone{ 0.524f };
            /// @brief 로컬 기본 방향입니다. 아래를 비춥니다. 월드 회전이 이것을 돌립니다.
            static constexpr float3 kDefaultDirection{ 0.0f, -1.0f, 0.0f };
            /// @brief 원뿔 반각의 상한입니다. 90도를 넘으면 원뿔이 뒤집혀 "빛이 뒤로도 나갑니다".
            static constexpr float32 kMaxConeAngle{ 1.5533f }; // 89도
        };
    } // namespace

    SpotLightComponent::SpotLightComponent()
        : LightComponent( shaderslot::kLightTypeSpot, SpotLightComponentInternal::kDefaultColor, SpotLightComponentInternal::kDefaultIntensity )
        , _radius{ SpotLightComponentInternal::kDefaultRadius }
        , _innerConeAngle{ SpotLightComponentInternal::kDefaultInnerCone }
        , _outerConeAngle{ SpotLightComponentInternal::kDefaultOuterCone }
    {
    }

    void SpotLightComponent::setRadius( float32 radius )
    {
        // 반경 0 은 셰이더에서 0 으로 나누는 자리다. 아주 작은 값으로 막는다.
        _radius = MathUtil::max( radius, 0.01f );
        onPropertyChanged( hashed_string( "_radius" ) );
    }

    void SpotLightComponent::setInnerConeAngle( float32 radians )
    {
        // 안쪽이 바깥쪽보다 크면 감쇠 분모가 음수가 되어 원뿔이 뒤집힌다. 여기서 자른다.
        _innerConeAngle = MathUtil::clamp( radians, 0.0f, _outerConeAngle );
        onPropertyChanged( hashed_string( "_innerConeAngle" ) );
    }

    void SpotLightComponent::setOuterConeAngle( float32 radians )
    {
        _outerConeAngle = MathUtil::clamp( radians, 0.0f, SpotLightComponentInternal::kMaxConeAngle );
        if ( _innerConeAngle > _outerConeAngle )
            _innerConeAngle = _outerConeAngle;
        onPropertyChanged( hashed_string( "_outerConeAngle" ) );
    }

    float3 SpotLightComponent::getLightDirection() const
    {
        return computeLightDirection( SpotLightComponentInternal::kDefaultDirection );
    }
} // namespace sw
