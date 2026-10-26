#include "pch.h"

#include "Editor/Panels/Inspector/InspectorPropertyUndo.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/Inspector/Model/InspectorPropertyLayout.h"

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
            EditorSelection*         _pPreviewSelection{ nullptr }; ///< 미리보기 창의 선택을 고쳤으면 그 선택(기록은 그 창의 스택). 메인이면 nullptr
        };

        /** @brief 인스펙터 되돌리기 추적의 상태입니다. 에디터 범위의 상태 저장소가 소유한다 — 에디터가 사라지면 붙잡은 스냅샷도 함께 사라진다. */
        struct InspectorPropertyUndoState
        {
            unordered_map<ImGuiID, PendingEdit> _mapPending;   ///< 편집 중인 위젯 표
            vector<PendingEdit>                 _listFinished; ///< 위젯이 풀린 편집
        };

        /** @brief 편집 중인 위젯 표입니다. trackPod 와 trackString 이 **같은 테이블을 씁니다**(각자 맵을 들면 한쪽만 고칠 때 갈라집니다). */
        unordered_map<ImGuiID, PendingEdit>& getPendingEdits()
        {
            return getEditorState<InspectorPropertyUndoState>()._mapPending;
        }

        /** @brief 위젯이 풀린 편집입니다. 기록은 같은 프레임의 편집 통지(다중 선택의 나머지에 값 입히기)가 끝난 뒤 `commitFinishedEdits` 가 한다. */
        vector<PendingEdit>& getFinishedEdits()
        {
            return getEditorState<InspectorPropertyUndoState>()._listFinished;
        }

        /** @brief 선택한 오브젝트 전부의 "편집 전" 스냅샷을 찍습니다. @p pLabel 이 널이면 "Property" 입니다. */
        PendingEdit captureSelectionBeforeEdit( EditorContext& context, const utf8* pLabel )
        {
            // 인스펙터가 겨누는 선택이다 — 미리보기 창의 선택이면 그 씬의 오브젝트다.
            EditorSelection* pFocused = editor::getFocusedSelection();
            if ( pFocused == nullptr ) // 컨텍스트가 있으면 메인 선택이 있다 — 겨누는 창이 없어도 그것을 돌려준다
                ::sw::internal::reportMissingRequiredService( SW_FUNCTION_SIGNATURE );
            EditorSelection&    selection = *pFocused;
            vector<GameObject*> listSelected;
            selection.getSelectedObjects( listSelected );
            if ( listSelected.empty() && selection.isPreviewScope() == false )
            {
                GameObject* pSelected = context.getSubsystem<EditorWorkspace>().getSelectedObject();
                if ( pSelected != nullptr )
                    listSelected.push_back( pSelected );
            }
            PendingEdit pending;
            for ( GameObject* pSelected : listSelected )
            {
                pending._listTarget.push_back( pSelected->getHandle() );
                pending._listBefore.push_back( EditorTransaction::captureSnapshot( pSelected ) );
            }
            pending._label             = ( pLabel != nullptr ) ? pLabel : "Property";
            pending._pPreviewSelection = selection.isPreviewScope() ? &selection : nullptr;
            return pending;
        }

        /**
         * @brief ImGui 위젯의 활성화~해제 사이를 한 번의 Undo 로 묶습니다.
         * @param pLabel Undo 항목에 붙일 이름 (널이면 "Property")
         */
        void trackActiveItemEdit( const utf8* pLabel )
        {
            EditorContext&                       editorContext = editor::getSubsystem<EditorContext>();
            unordered_map<ImGuiID, PendingEdit>& mapPending    = getPendingEdits();

            const ImGuiID id = ImGui::GetItemID();
            if ( ImGui::IsItemActivated() )
                mapPending[id] = captureSelectionBeforeEdit( editorContext, pLabel );

            if ( ImGui::IsItemDeactivatedAfterEdit() == false )
                return;

            const auto iter = mapPending.find( id );
            if ( iter == mapPending.end() )
                return;

            getFinishedEdits().push_back( std::move( iter->second ) );
            mapPending.erase( iter );
        }
    } // namespace

    void InspectorPropertyUndo::trackPod( const MutableValueAddress value, size_t size, const PropertyInfo& prop )
    {
        // value 는 유효성 가드로만 쓴다. 스냅샷은 값이 아니라 오브젝트 XML 로 뜬다.
        const bool bUntrackableEdit = value.get() == nullptr || size == 0 || size > 512;
        if ( bUntrackableEdit )
            return;
        trackActiveItemEdit( InspectorPropertyLayout::getPropertyLabel( prop ).c_str() );
    }

    void InspectorPropertyUndo::trackString( string* pPtr, const PropertyInfo& prop )
    {
        if ( pPtr == nullptr )
            return;
        trackActiveItemEdit( InspectorPropertyLayout::getPropertyLabel( prop ).c_str() );
    }

    void InspectorPropertyUndo::trackLastItem( const utf8* pPropertyLabel )
    {
        trackActiveItemEdit( pPropertyLabel );
    }

    void InspectorPropertyUndo::recordImmediateEdit( const PropertyInfo& prop )
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        getFinishedEdits().push_back( captureSelectionBeforeEdit( editorContext, InspectorPropertyLayout::getPropertyLabel( prop ).c_str() ) );
    }

    void InspectorPropertyUndo::commitFinishedEdits()
    {
        vector<PendingEdit>& listFinished = getFinishedEdits();
        // 플레이 중 편집은 기록하지 않는다 — 편집 내역은 맡겨 두었고(`CommandStack::parkHistory`) Stop 이 스냅샷으로 되돌린다.
        if ( EditorPlaySession::areSceneEditsAllowed() == false )
        {
            listFinished.clear();
            return;
        }
        for ( const PendingEdit& edit : listFinished )
        {
            const string label = InspectorPropertyLayout::makeUndoLabel( edit._label.c_str() );
            if ( edit._pPreviewSelection != nullptr )
            {
                // 미리보기 창의 편집은 그 창의 스택에 남긴다. 그 사이 창이 닫혔거나 다른 선택을 겨누면 선택이 없을 수 있어 남기지 않는다.
                if ( editor::getFocusedSelection() != edit._pPreviewSelection )
                    continue;
                for ( size_t index = 0; index < edit._listTarget.size(); ++index )
                {
                    GameObject* pTarget = edit._pPreviewSelection->findObject( edit._listTarget[index] );
                    if ( pTarget != nullptr )
                        edit._pPreviewSelection->recordModify( *pTarget, edit._listBefore[index], EditorTransaction::captureSnapshot( pTarget ), label );
                }
                continue;
            }
            const bool bCompound = edit._listTarget.size() > 1;
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
