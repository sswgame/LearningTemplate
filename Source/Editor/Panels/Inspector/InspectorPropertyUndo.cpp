#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Editor/Panels/Inspector/Model/InspectorEditRecorder.h"
#include "Editor/Panels/Inspector/Model/PropertyGridViewState.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct InspectorPropertyUndoInternal
        {
            /**
             * @brief 방금 그린 위젯의 활성화~해제 사이를 한 번의 되돌리기로 묶습니다(기록은 `InspectorEditRecorder`).
             * @param pLabel 되돌리기 항목에 붙일 이름(널이면 "Property")
             */
            static void trackActiveItemEdit( const utf8* pLabel )
            {
                const uint32 widgetID = static_cast<uint32>( ImGui::GetItemID() );
                if ( ImGui::IsItemActivated() )
                    InspectorEditRecorder::beginEdit( widgetID, pLabel );
                if ( ImGui::IsItemDeactivatedAfterEdit() )
                    InspectorEditRecorder::finishEdit( widgetID );
            }
        };
    } // namespace

    void InspectorPropertyUndo::trackRowEdit( const PropertyValueRow& row )
    {
        InspectorPropertyUndoInternal::trackActiveItemEdit( row._label.c_str() );
    }

    void InspectorPropertyUndo::trackLastItem( const utf8* pPropertyLabel )
    {
        InspectorPropertyUndoInternal::trackActiveItemEdit( pPropertyLabel );
    }

    void InspectorPropertyUndo::commitFinishedEdits()
    {
        InspectorEditRecorder::commitFinishedEdits();
    }
} // namespace sw::editor
