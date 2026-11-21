/**
 * @file OneWayPlatformComponent.h
 * @brief 한쪽에서만 막는 발판입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class OneWayPlatformComponent
     * @brief 한쪽에서만 막는 발판입니다. 이동 몸이 `isSolidFor( 속도 )` 로 막는지 묻습니다. 위로 뛰어오르면 통과하고, 내려오면 착지합니다. 아래 키로 내려가는 것은 `_bDropThrough` 로 허용합니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "One-Way Platform", Tooltip = "Solid only against movement opposite to its normal" )
    class SW_GF_API OneWayPlatformComponent : public Component
    {
    public:
        REFLECT_BODY();

        OneWayPlatformComponent();
        virtual ~OneWayPlatformComponent() override = default;

        /** @brief @p velocity 로 움직이는 몸을 막으면 true 입니다. 법선을 거슬러(법선과 속도의 내적이 0 보다 작을 때) 올 때만 막습니다. 아래로 내려가기를 허용했고 @p bDropRequested 가 true 이면 막지 않습니다. */
        bool isSolidFor( const float3& velocity, bool bDropRequested ) const;

    private:
        PROPERTY( Category = "One Way", DisplayName = "Normal", Tooltip = "Side you can stand on (world)" )
        float3 _normal;
        PROPERTY( Category = "One Way", DisplayName = "Drop Through", Tooltip = "Pressing down drops through" )
        bool _bDropThrough;
    };
} // namespace sw
