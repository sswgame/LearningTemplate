/**
 * @file SkirmishDragComponent.h
 * @brief 끌어 고르기 상자의 모습 — 디렉터가 끄는 동안 땅 위 사각형만 한 반투명 상자를 보이고, 놓으면 숨깁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class SkirmishDragComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다.
     */
    REFLECT( Category = "RealTimeStrategy", DisplayName = "Skirmish Drag Box", Tooltip = "Shows the drag-select box of the skirmish director" )
    class SkirmishDragComponent : public Component
    {
    public:
        REFLECT_BODY();

        SkirmishDragComponent();
        virtual ~SkirmishDragComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        PROPERTY( Category = "Drag", DisplayName = "Director", Tooltip = "Object with the SkirmishDirectorComponent" )
        GameObjectHandle _director;
    };
} // namespace sw
