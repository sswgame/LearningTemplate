/**
 * @file LightComponentValidation.cpp
 * @brief `LightProbeVolumeComponent`, `PointLightComponent`, `ReflectionProbeComponent` 의 프로퍼티 검증 함수(`PROPERTY( Validate = fn )`)입니다. 에디터 데이터 검증이라 `Dev/` 에 두고 배포 구성은 짓지 않는다.
 * @details 생성 코드가 이 함수들을 `SW_DEV_ONLY_FUNCTION` 으로 등록해 배포는 참조하지 않는다(언리얼 `IsDataValid` 가 `WITH_EDITOR` 인 것과 같다).
 */
#include "pch.h"

#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Visual/Component/LightProbeVolumeComponent.h"
#include "Engine/Visual/Component/PointLightComponent.h"
#include "Engine/Visual/Component/ReflectionProbeComponent.h"

namespace sw
{
    void LightProbeVolumeComponent::validateProbeData( ValidationContext& context ) const
    {
        const bool bNoExtent = _extent._x <= 0.0f && _extent._y <= 0.0f && _extent._z <= 0.0f;
        if ( bNoExtent )
            context.addError( "Extent is 0 on every axis - the volume holds a single probe" );
        if ( _probeDataPath.empty() && _bRealtime == SW_FALSE )
            context.addWarning( "Light probe volume is not baked - it gives no indirect light" );
    }

    void PointLightComponent::validateRadius( ValidationContext& context ) const
    {
        // 세터는 0 을 막지만 데이터(씬 · 프리팹)의 0 은 그대로 들어온다 — 셰이더가 반경으로 나누는 자리다.
        if ( _radius > 0.0f )
            return;
        fixed_string<constant::buffer::kMax64> valueText;
        formatstring( valueText.data(), valueText.capacity(), "%#", kDefaultRadius );
        fixed_string<constant::buffer::kMax64> description;
        formatstring( description.data(), description.capacity(), "Set Radius to the default %# m", kDefaultRadius );
        context.addErrorWithFix( "Radius is 0 - the light reaches nothing and the shader divides by it", ValidationPropertyEdit{ hashed_string{}, valueText.c_str(), description.c_str() } );
    }

    void ReflectionProbeComponent::validateProbeData( ValidationContext& context ) const
    {
        const bool bNoExtent = _extent._x <= 0.0f || _extent._y <= 0.0f || _extent._z <= 0.0f;
        if ( bNoExtent )
            context.addError( "Extent is 0 on some axis - no surface is inside the probe box" );
        if ( _probeDataPath.empty() )
            context.addWarning( "Reflection probe is not baked - it gives no reflection" );
    }
} // namespace sw
