/**
 * @file LaunchPadComponent.h
 * @brief 겹치기 시작한 것을 쏘는 스프링 · 점프대입니다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class LaunchPadComponent
     * @brief 스프링과 점프대입니다. 겹치기 시작한 것(태그로 거름)을 `_launchVelocity`(월드)로 쏘고
     *        `GimmickLaunchEvent` 를 냅니다. 캐릭터 컨트롤러와 이동 몸이 이 이벤트를 받습니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Launch Pad", Tooltip = "Spring / jump pad: launches whatever starts overlapping" )
    class SW_GF_API LaunchPadComponent : public Component
    {
    public:
        REFLECT_BODY();

        LaunchPadComponent();
        virtual ~LaunchPadComponent() override = default;

        /** @brief 겹침 진입 사건을 구독합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief @p target 을 쏩니다. 겹침이 부르며, 테스트와 스크립트도 부를 수 있습니다. */
        void  launch( GameObject& target );
        int32 getLaunchCount() const { return _launchCount; }

    private:
        /** @brief 겹치기 시작한 몸(`GameObjectEvents::_overlapBegin`, 태그로 거름)을 쏩니다. */
        void handleOverlapBegin( const OverlapInfo& overlap );

        Subscription _overlapSubscription; ///< 겹침 진입 구독(`handleOverlapBegin`)
        PROPERTY( Category = "Launch", DisplayName = "Launch Velocity", Units = "m/s" )
        float3 _launchVelocity;
        PROPERTY( Category = "Launch", DisplayName = "Required Tags" )
        TagContainer _requiredTags;
        PROPERTY( RuntimeState, Category = "Launch", DisplayName = "Launch Count", Tooltip = "Runtime" )
        int32 _launchCount;
    };
} // namespace sw
