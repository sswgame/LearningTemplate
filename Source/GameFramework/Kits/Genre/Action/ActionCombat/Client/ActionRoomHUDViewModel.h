/**
 * @file ActionRoomHUDViewModel.h
 * @brief 액션 룸 HUD 문서(`actionroom.ui.xml`)가 보이는 값입니다 — 글은 문서의 번역 키이고 뷰모델은 보임만 든다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Presentation/WidgetVisibility.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @class ActionRoomHUDViewModel @brief 액션 룸 HUD 의 뷰모델입니다. `ActionRoomHUDComponent` 가 소유하고 `IHUDViewModelBinder` 로 겁니다. 게임 스레드만. */
    REFLECT( DisplayName = "Action Room HUD View Model" )
    class SW_KIT_CLIENT_API ActionRoomHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        ActionRoomHUDViewModel();
        ~ActionRoomHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 문이 잠겼다는 표시를 보이거나 숨깁니다. */
        void setGateLockedShown( bool bShown );
        /** @brief 방을 비웠다는 알림을 보이거나 숨깁니다. */
        void setClearedShown( bool bShown );
        /** @brief 쓰러졌다는 알림을 보이거나 숨깁니다. */
        void setDefeatedShown( bool bShown );

        WidgetVisibility getGateLockedVisibility() const { return _gateLockedVisibility; }
        WidgetVisibility getClearedVisibility() const { return _clearedVisibility; }
        WidgetVisibility getDefeatedVisibility() const { return _defeatedVisibility; }

    private:
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Gate Locked Visibility" )
        WidgetVisibility _gateLockedVisibility;
        PROPERTY( DisplayName = "Cleared Visibility" )
        WidgetVisibility _clearedVisibility;
        PROPERTY( DisplayName = "Defeated Visibility" )
        WidgetVisibility _defeatedVisibility;
    };
} // namespace sw
