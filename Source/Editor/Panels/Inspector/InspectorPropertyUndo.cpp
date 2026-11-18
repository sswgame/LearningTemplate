#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Editor/Panels/Inspector/Model/PropertyGridViewModel.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    bool InspectorPropertyUndo::isActiveItemDrawn()
    {
        // `ActiveIdIsAlive` 는 프레임마다 0 에서 시작해 활성 위젯이 그려질 때(`ItemAdd` → `KeepAliveID`) 그 id 가 된다.
        const ImGuiContext& context = *ImGui::GetCurrentContext();
        return context.ActiveId != 0 && context.ActiveIdIsAlive == context.ActiveId;
    }

    void InspectorPropertyUndo::trackRow( PropertyGridViewModel& model, uint32 rowID, bool bActiveDrawnBefore )
    {
        if ( bActiveDrawnBefore == false && isActiveItemDrawn() )
            model.touchRow( rowID );
    }

    void InspectorPropertyUndo::trackComponentRegion( PropertyGridViewModel& model, EditorPropertyGridScope scope, uint64 objectID, uint64 componentID, const utf8* pLabel )
    {
        model.trackComponentRegion( scope, objectID, componentID, ImGui::IsItemActive(), pLabel != nullptr ? string_view{ pLabel } : string_view{} );
    }
} // namespace sw::editor
