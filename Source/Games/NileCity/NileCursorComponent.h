/**
 * @file NileCursorComponent.h
 * @brief 짓기 커서의 모습 — 디렉터가 고른 마우스 아래 칸에 고른 것의 크기만 한 반투명 상자를 둡니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @class NileCursorComponent
     * @brief `TickGroup::PostUpdate` 에서 디렉터를 읽기만 하고 자기 오브젝트의 메시에만 씁니다. 커서가 땅 밖이면 숨깁니다.
     */
    REFLECT( Category = "CityBuilder", DisplayName = "Nile Cursor View", Tooltip = "Shows the build cursor of the Nile director" )
    class NileCursorComponent : public Component
    {
    public:
        REFLECT_BODY();

        NileCursorComponent();
        virtual ~NileCursorComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

    private:
        PROPERTY( Category = "Cursor", DisplayName = "Director", Tooltip = "Object with the NileDirectorComponent" )
        GameObjectHandle _director;
        PROPERTY( Category = "Cursor", DisplayName = "Height", Tooltip = "Box centre above the ground", Meta = "Units=m" )
        float32 _height;
        PROPERTY( Category = "Cursor", DisplayName = "Thickness", Tooltip = "Box height", Meta = "Units=m" )
        float32 _thickness;
    };
} // namespace sw
