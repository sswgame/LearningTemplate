#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/InspectorPropertyLayout.h"

#include "Engine/Object/GameObject/GameObject.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 편집이 시작된 위젯 하나의 "편집 전" 스냅샷입니다. 대상은 위젯이 풀릴 때까지 여러 프레임을 넘기므로 핸들로 듭니다. */
        struct PendingEdit
        {
            vector<ObjectSnapshot>   _listBefore;
            vector<GameObjectHandle> _listTarget; ///< 선택한 오브젝트 전부(다중 선택이면 인스펙터가 같은 값을 나머지에 입힌다)
            string                   _label;
        };

        /** @brief 편집 중인 위젯 표입니다. trackPod 와 trackString 이 **같은 표를 씁니다**(각자 맵을 들면 한쪽만 고칠 때 갈라집니다). */
        unordered_map<ImGuiID, PendingEdit>& getPendingEdits()
        {
            static unordered_map<ImGuiID, PendingEdit> s_mapPending;
            return s_mapPending;
        }

        /** @brief 위젯이 풀린 편집입니다. 기록은 같은 프레임의 편집 통지(다중 선택의 나머지에 값 입히기)가 끝난 뒤 `commitFinishedEdits` 가 한다. */
        vector<PendingEdit>& getFinishedEdits()
        {
            static vector<PendingEdit> s_listFinished;
            return s_listFinished;
        }

        /**
         * @brief ImGui 위젯의 활성화~해제 사이를 한 번의 Undo 로 묶습니다.
         * @param pLabel Undo 항목에 붙일 이름 (널이면 "Property")
         */
        void trackActiveItemEdit( const utf8* pLabel )
        {
            unordered_map<ImGuiID, PendingEdit>& s_mapPending = getPendingEdits();

            EditorContext* pContext = EditorContext::get();
            if ( pContext == nullptr )
                return;

            const ImGuiID id = ImGui::GetItemID();
            if ( ImGui::IsItemActivated() )
            {
                vector<GameObject*> listSelected;
                pContext->getEditorSelection().getSelectedObjects( listSelected );
                if ( listSelected.empty() )
                {
                    GameObject* pSelected = pContext->getWorkspace().getSelectedObject();
                    if ( pSelected != nullptr )
                        listSelected.push_back( pSelected );
                }
                PendingEdit pending;
                for ( GameObject* pSelected : listSelected )
                {
                    pending._listTarget.push_back( pSelected->getHandle() );
                    pending._listBefore.push_back( EditorTransaction::captureSnapshot( pSelected ) );
                }
                pending._label   = ( pLabel != nullptr ) ? pLabel : "Property";
                s_mapPending[id] = std::move( pending );
            }

            if ( ImGui::IsItemDeactivatedAfterEdit() == false )
                return;

            const auto iter = s_mapPending.find( id );
            if ( iter == s_mapPending.end() )
                return;

            getFinishedEdits().push_back( std::move( iter->second ) );
            s_mapPending.erase( iter );
        }
    } // namespace

    void InspectorPropertyUndo::trackPod( void* pData, size_t size, const PropertyInfo& prop )
    {
        // pData 는 유효성 가드로만 쓴다. 스냅샷은 값이 아니라 오브젝트 XML 로 뜬다.
        if ( pData == nullptr || size == 0 || size > 512 )
            return;
        trackActiveItemEdit( InspectorPropertyLayout::getPropertyLabel( prop ) );
    }

    void InspectorPropertyUndo::trackString( string* pPtr, const PropertyInfo& prop )
    {
        if ( pPtr == nullptr )
            return;
        trackActiveItemEdit( InspectorPropertyLayout::getPropertyLabel( prop ) );
    }

    void InspectorPropertyUndo::trackLastItem( const utf8* pPropertyLabel )
    {
        trackActiveItemEdit( pPropertyLabel );
    }

    void InspectorPropertyUndo::commitFinishedEdits()
    {
        vector<PendingEdit>& listFinished = getFinishedEdits();
        // 플레이 중 편집은 기록하지 않는다 — 편집 내역은 맡겨 두었고(`CommandStack::parkHistory`) Stop 이 스냅샷으로 되돌린다.
        if ( EditorUtil::areSceneEditsAllowed() == false )
        {
            listFinished.clear();
            return;
        }
        for ( const PendingEdit& edit : listFinished )
        {
            const string label     = InspectorPropertyLayout::makeUndoLabel( edit._label.c_str() );
            const bool   bCompound = edit._listTarget.size() > 1;
            if ( bCompound )
                EditorTransaction::beginTransaction( label ); // 여러 오브젝트의 같은 편집은 되돌리기 한 번이다
            for ( size_t index = 0; index < edit._listTarget.size(); ++index )
            {
                GameObject*          pTarget       = editor::findGameObject( edit._listTarget[index] );
                const ObjectSnapshot afterSnapshot = EditorTransaction::captureSnapshot( pTarget );
                EditorTransaction::recordModify( pTarget, edit._listBefore[index], afterSnapshot, label );
            }
            if ( bCompound )
                EditorTransaction::endTransaction();
        }
        listFinished.clear();
    }
} // namespace sw::editor
