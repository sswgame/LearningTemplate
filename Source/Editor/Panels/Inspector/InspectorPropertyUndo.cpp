#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Editor/Panels/Inspector/Model/InspectorEditRecorder.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct InspectorPropertyUndoInternal
        {
            /** @brief 되돌리기로 남길 수 있는 POD 값의 최대 크기(바이트)입니다. 그보다 큰 값은 위젯 하나의 편집이 아니다. */
            static constexpr size_t kMaxTrackedPODSize = 512;

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

    void InspectorPropertyUndo::trackPOD( const MutableValueAddress value, size_t size, const PropertyInfo& prop )
    {
        // value 는 유효성 가드로만 쓴다. 스냅샷은 값이 아니라 오브젝트 XML 로 뜬다.
        const bool bUntrackableEdit = value.get() == nullptr || size == 0 || size > InspectorPropertyUndoInternal::kMaxTrackedPODSize;
        if ( bUntrackableEdit )
            return;
        InspectorPropertyUndoInternal::trackActiveItemEdit( InspectorPropertyLayout::getPropertyLabel( prop ).c_str() );
    }

    void InspectorPropertyUndo::trackString( string* pPtr, const PropertyInfo& prop )
    {
        if ( pPtr == nullptr )
            return;
        InspectorPropertyUndoInternal::trackActiveItemEdit( InspectorPropertyLayout::getPropertyLabel( prop ).c_str() );
    }

    void InspectorPropertyUndo::trackLastItem( const utf8* pPropertyLabel )
    {
        InspectorPropertyUndoInternal::trackActiveItemEdit( pPropertyLabel );
    }

    void InspectorPropertyUndo::recordImmediateEdit( const PropertyInfo& prop )
    {
        InspectorEditRecorder::recordImmediateEdit( InspectorPropertyLayout::getPropertyLabel( prop ).c_str() );
    }

    void InspectorPropertyUndo::commitFinishedEdits()
    {
        InspectorEditRecorder::commitFinishedEdits();
    }
} // namespace sw::editor
