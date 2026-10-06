/**
 * @file VehicleExitComponent.h
 * @brief 탈것 쪽 내리기 버튼 — 탈것 폰의 의도 버튼(`Exit`)이 발동하면 운전석 탑승자가 내립니다(빙의가 탈것에 있으므로 탈것이 버튼을 받는다).
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
     * @class VehicleExitComponent
     * @brief 탈것 오브젝트(폰 + 운전석)에 붙습니다. PrePhysics 틱에서 탈것 폰의 의도만 읽고, 내리기는 틱 뒤로 미룹니다.
     */
    REFLECT( Category = "Vehicle", DisplayName = "Vehicle Exit", Tooltip = "Vehicle side: the Exit intent button gets the driver off" )
    class SW_GF_API VehicleExitComponent : public Component
    {
    public:
        REFLECT_BODY();

        VehicleExitComponent();
        ~VehicleExitComponent() override = default;

        /** @brief 버튼 이름의 자리를 풀고 PrePhysics 에서 틱합니다. */
        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        /** @brief 틱 뒤 — 운전석 탑승자를 내립니다. */
        void exitDriver();

    private:
        PROPERTY( Category = "Vehicle", DisplayName = "Exit Button", Tooltip = "Intent button of the vehicle pawn that gets the driver off" )
        hashed_string _exitButton;

        int32 _exitIndex;
    };
} // namespace sw
