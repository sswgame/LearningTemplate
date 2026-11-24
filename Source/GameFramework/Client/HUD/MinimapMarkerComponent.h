/**
 * @file GameFramework/Client/HUD/MinimapMarkerComponent.h
 * @brief 미니맵에 점으로 보일 오브젝트의 표시입니다(색 · 크기 · 범위 밖에서 가장자리에 붙는지).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class MinimapMarkerComponent
     * @brief 씬의 미니맵 표시 목록(`ComponentRegistry`)에 오릅니다. 그리는 쪽은 `MinimapComponent` 입니다.
     */
    REFLECT( Category = "UI", DisplayName = "Minimap Marker", Tooltip = "Shows this object as a dot on the minimap" )
    class SW_GF_API MinimapMarkerComponent : public Component
    {
    public:
        REFLECT_BODY();
        SW_REGISTERED_COMPONENT( MinimapMarkerComponent );

        MinimapMarkerComponent();
        ~MinimapMarkerComponent() override = default;

        const float4& getColor() const { return _color; }
        float32       getSize() const { return _size; }
        bool          isClampedToEdge() const { return _bClampToEdge; }

    private:
        PROPERTY( Category = "Minimap", DisplayName = "Color" )
        float4 _color;
        PROPERTY( Category = "Minimap", DisplayName = "Size", Min = 1.0, Meta = "Units=ui" )
        float32 _size;
        PROPERTY( Category = "Minimap", DisplayName = "Clamp To Edge", Tooltip = "Out of range it stays on the edge (objective); off: it disappears" )
        bool _bClampToEdge;
    };
} // namespace sw
