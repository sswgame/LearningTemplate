#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Editor/Panels/Inspector/Model/PropertyGridModel.h"

#include <imgui.h>

namespace sw::editor
{
    void InspectorPropertyUndo::trackRowEdit( PropertyGridModel& model )
    {
        // 해제는 편집 여부와 무관하게 받는다(`IsItemDeactivated`) — 구간이 없으면 할 일이 없고, 있으면 전 · 후가 같을 때 마감이 버린다.
        model.trackWidget( static_cast<uint32>( ImGui::GetItemID() ), ImGui::IsItemActive(), ImGui::IsItemDeactivated() );
    }

    void InspectorPropertyUndo::trackComponentRegion( PropertyGridModel& model, EditorPropertyGridScope scope, uint64 objectID, uint64 componentID, const utf8* pLabel )
    {
        const uint32 widgetID = static_cast<uint32>( ImGui::GetItemID() );
        if ( ImGui::IsItemActivated() )
            model.openComponentRegionSpan( widgetID, scope, objectID, componentID, pLabel != nullptr ? string_view{ pLabel } : string_view{} );
        model.trackWidget( widgetID, ImGui::IsItemActive(), ImGui::IsItemDeactivated() );
    }
} // namespace sw::editor
