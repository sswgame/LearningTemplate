/**
 * @file HidingSpotComponent.h
 * @brief 숨는 곳(사물함 · 옷장)입니다. 스마트 오브젝트 슬롯을 차지하면 숨음 태그를 붙입니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class HidingSpotComponent
     * @brief 숨는 곳입니다(사물함, 옷장, 침대 밑). 같은 오브젝트의 `SmartObjectComponent` 슬롯(Activity.Hide)을 차지하면 `State.Hidden` 태그를 붙입니다.
     *        AI 감각은 그 태그를 보고 대상을 보지 않습니다(`isHidden`). 상호작용(Hide)을 끝내면 들어가고, 다시 끝내면 나옵니다. 센서의 사용 알림을 읽어서 처리합니다.
     */
    REFLECT( Category = "Gimmick", DisplayName = "Hiding Spot", Tooltip = "Claims a smart object slot and tags the occupant as hidden" )
    class SW_GF_API HidingSpotComponent : public Component
    {
    public:
        REFLECT_BODY();

        HidingSpotComponent();
        virtual ~HidingSpotComponent() override = default;

        void onTick( TickContext& context ) override;
        /** @brief 숨깁니다. 슬롯이 없으면 false 입니다. */
        [[nodiscard]] bool enter( GameObject& who );
        /** @brief 나옵니다. 숨어 있지 않았으면 false 입니다. */
        bool exit( GameObject& who );
        /** @brief 숨음 태그를 가지고 있으면 true 입니다. AI 감각이 이 값을 묻습니다. */
        static bool isHidden( const GameObject& object );
    };
} // namespace sw
