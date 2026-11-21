/**
 * @file BoostPadComponent.h
 * @brief 밟기 시작한 탈것에 부스트를 거는 부스트 패드입니다.
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
    /**
     * @class BoostPadComponent
     * @brief 밟기 시작한 몸(태그로 거름)이 부스트 계약(`IBoostReceiver` — 바디 없는 탈것 등)을 내보내면 `_duration` 초 부스트를 겁니다. 계약이 없는 몸은 지나가고, 거절하면 경고를 남깁니다.
     * @details 부스트 세기는 받는 쪽 튜닝(탈것의 부스트 모듈)이 정한다 — 패드는 시간만 준다(트랙 패드 `KartTrack::applyBoostPad` 와 같은 길).
     */
    REFLECT( Category = "Gimmick", DisplayName = "Boost Pad", Tooltip = "Boosts whatever starts overlapping and accepts boosts (IBoostReceiver; the receiver tuning sets the strength)" )
    class SW_GF_API BoostPadComponent : public Component
    {
    public:
        REFLECT_BODY();

        BoostPadComponent();
        virtual ~BoostPadComponent() override = default;

        /** @brief 겹침 진입 사건을 구독합니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief @p target 이 부스트 계약을 내보내면 부스트를 겁니다. 겹침이 부르며, 테스트와 스크립트도 부를 수 있습니다. */
        void  boost( GameObject& target );
        int32 getBoostCount() const { return _boostCount; }

    private:
        /** @brief 밟기 시작한 몸(`GameObjectEvents::_overlapBegin`, 태그로 거름)에 `boost` 를 부릅니다. */
        void handleOverlapBegin( const OverlapInfo& overlap );

        Subscription _overlapSubscription; ///< 겹침 진입 구독(`handleOverlapBegin`)
        PROPERTY( Category = "Boost", DisplayName = "Duration", Min = 0.0, Units = s )
        float32 _duration;
        PROPERTY( Category = "Boost", DisplayName = "Required Tags" )
        TagContainer _requiredTags;
        PROPERTY( RuntimeState, Category = "Boost", DisplayName = "Boost Count", Tooltip = "Runtime" )
        int32 _boostCount;
    };
} // namespace sw
