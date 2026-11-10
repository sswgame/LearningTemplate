/**
 * @file NileCursorComponent.h
 * @brief 짓기 커서의 모습 — 디렉터가 고른 마우스 아래 칸에 고른 것의 크기만 한 반투명 상자를 둡니다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/GameObjectHandle.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Runtime/Foundation/Framework/GameModeBoundComponent.h"

namespace sw
{
    /**
     * @class NileCursorComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 커서가 땅 밖이면 숨깁니다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "Nile Cursor View", Tooltip = "Shows the build cursor of the Nile game mode" )
    class NileCursorComponent : public GameModeBoundComponent
    {
    public:
        REFLECT_BODY();

        NileCursorComponent();
        virtual ~NileCursorComponent() override = default;

        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

    private:
        PROPERTY( Category = "Cursor", DisplayName = "Height", Tooltip = "Box centre above the ground", Units = m )
        float32 _height;
        PROPERTY( Category = "Cursor", DisplayName = "Thickness", Tooltip = "Box height", Units = m )
        float32 _thickness;
    };
} // namespace sw
