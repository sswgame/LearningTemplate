#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObjectPtr.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 편집이 시작된 위젯 하나의 "편집 전" 스냅샷. */
        struct PendingEdit
        {
            string        _beforeXml;
            GameObjectPtr _pObj;
            string        _label;
        };

        /**
         * @brief ImGui 위젯의 활성화~해제 사이를 한 번의 Undo 로 묶습니다.
         * @details trackPod 와 trackString 이 **같은 표를 쓴다.** 예전엔 두 함수가 각자 제 static 맵을
         *          들고 같은 38 줄을 그대로 복사하고 있었다 — 지금은 동작이 같지만 한쪽만 고치면 갈라진다.
         * @param pLabel Undo 항목에 붙일 이름 (널이면 "Property")
         */
        void trackActiveItemEdit( const utf8* pLabel )
        {
            static unordered_map<ImGuiID, PendingEdit> s_mapPending;

            EditorContext* pContext = EditorContext::get();
            if ( pContext == nullptr )
                return;

            const ImGuiID id = ImGui::GetItemID();
            if ( ImGui::IsItemActivated() )
            {
                PendingEdit pending;
                pending._pObj      = pContext->getWorkspace().getSelectedObject();
                pending._beforeXml = EditorTransaction::captureSnapshot( pending._pObj );
                pending._label     = ( pLabel != nullptr ) ? pLabel : "Property";
                s_mapPending[id]   = std::move( pending );
            }

            if ( ImGui::IsItemDeactivatedAfterEdit() == false )
                return;

            const auto iter = s_mapPending.find( id );
            if ( iter == s_mapPending.end() )
                return;

            const string afterXml = EditorTransaction::captureSnapshot( iter->second._pObj );
            const string label    = string( "Edit " ) + iter->second._label;
            EditorTransaction::recordModify( iter->second._pObj, iter->second._beforeXml, afterXml, label );
            s_mapPending.erase( iter );
        }
    } // namespace

    void InspectorPropertyUndo::trackPod( void* pData, size_t size, const utf8* pLabel )
    {
        // pData 는 유효성 가드로만 쓴다 — 스냅샷은 값이 아니라 오브젝트 XML 로 뜬다.
        if ( pData == nullptr || size == 0 || size > 512 )
            return;
        trackActiveItemEdit( pLabel );
    }

    void InspectorPropertyUndo::trackString( string* pPtr, const utf8* pLabel )
    {
        if ( pPtr == nullptr )
            return;
        trackActiveItemEdit( pLabel );
    }
} // namespace sw::editor
