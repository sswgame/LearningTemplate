/**
 * @file BoostPadComponent.h
 * @brief 밟기 시작한 것에 부스트 이벤트를 내는 부스트 패드입니다.
 */
#pragma once
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/Component/TagSystem.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @class BoostPadComponent 는 밟기 시작한 것(태그로 거름)에 `GimmickBoostEvent` 를 냅니다. 차량 몸이 이 이벤트를 받습니다. */
    REFLECT( Category = "Gimmick", DisplayName = "Boost Pad", Tooltip = "Sends a boost event to whatever starts overlapping" )
    class SW_GF_API BoostPadComponent : public Component
    {
    public:
        REFLECT_BODY();

        BoostPadComponent();
        virtual ~BoostPadComponent() override = default;

        /** @brief 겹침 진입 사건을 구독합니다. */
        void  onRegister( GameObjectManager& manager ) override;
        int32 getBoostCount() const { return _boostCount; }

    private:
        /** @brief 밟기 시작한 몸(`GameObjectEvents::_overlapBegin`, 태그로 거름)에 부스트 이벤트를 냅니다. */
        void handleOverlapBegin( const OverlapInfo& overlap );

        Subscription _overlapSubscription; ///< 겹침 진입 구독(`handleOverlapBegin`)
        PROPERTY( Category = "Boost", DisplayName = "Duration", Min = 0.0, Units = s )
        float32 _duration;
        PROPERTY( Category = "Boost", DisplayName = "Strength", Min = 0.0, Tooltip = "Speed multiplier or added speed, as the vehicle reads it" )
        float32 _strength;
        PROPERTY( Category = "Boost", DisplayName = "Required Tags" )
        TagContainer _requiredTags;
        PROPERTY( RuntimeState, Category = "Boost", DisplayName = "Boost Count", Tooltip = "Runtime" )
        int32 _boostCount;
    };
} // namespace sw
