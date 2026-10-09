/**
 * @file MountInteractionComponent.h
 * @brief 탑승자 쪽 타기 버튼 — 폰의 의도 버튼(`Interact`)이 발동하면 가까운 빈 좌석에 탑니다. 승객석에 앉아 있으면 같은 버튼으로 내립니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class MountInteractionComponent
     * @brief 탑승자 폰 오브젝트에 붙습니다. PrePhysics 틱에서 폰의 의도만 읽고(InputMap 을 읽지 않는다), 타기 · 내리기는 틱 뒤로 미룹니다(`executeOrDeferPostTick`).
     * @details 운전석에서 내리는 것은 탈것 쪽(`VehicleExitComponent` — 빙의가 탈것에 있다)이 합니다. 플레이어든 AI 든 같은 버튼 의도로 탑니다.
     *          상호작용 프레임워크(`Interaction/`)의 프롬프트 · 길게 누르기로 옮기는 것은 백로그입니다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Mount Interaction", Tooltip = "Rider side: the Interact intent button gets on the nearest free seat (or off a passenger seat)" )
    class SW_GF_API MountInteractionComponent : public Component
    {
    public:
        REFLECT_BODY();

        MountInteractionComponent();
        ~MountInteractionComponent() override = default;

        /** @brief 버튼 이름의 자리를 풀고 PrePhysics 에서 틱합니다. */
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        /** @brief 틱 뒤 — 앉아 있지 않으면 가까운 빈 좌석에 타고, 승객석이면 내립니다. */
        void toggleMount();

    private:
        PROPERTY( Category = "Vehicle", DisplayName = "Interact Button", Tooltip = "Intent button that gets on or off" )
        hashed_string _interactButton;

        int32 _interactIndex;
    };
} // namespace sw
