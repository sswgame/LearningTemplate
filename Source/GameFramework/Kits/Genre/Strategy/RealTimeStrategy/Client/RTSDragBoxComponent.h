/**
 * @file RTSDragBoxComponent.h
 * @brief RTS 키트의 클라이언트 쪽 끌어 고르기 상자의 모습 — 명령 입력(`RTSCommandInputComponent`)이 끄는 동안 땅 위 사각형만 한 반투명 상자를 보이고, 놓으면 숨깁니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/GameMode/GameFrameworkComponent.h"

namespace sw
{
    /**
     * @class RTSDragBoxComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터 오브젝트의 명령 입력을 읽기만 하고 자기 오브젝트의 메시에만 씁니다.
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "RTS Drag Box", Tooltip = "Shows the drag-select box of the RTS Command Input on the game mode object" )
    class SW_KIT_CLIENT_API RTSDragBoxComponent : public GameFrameworkComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 명령 입력은 틱 뒤에 끌기를 정한다 — 여기서 읽는 것은 지난 프레임의 끌기다.

        RTSDragBoxComponent();
        virtual ~RTSDragBoxComponent() override = default;

        void onTick( TickContext& context ) override;
    };
} // namespace sw
