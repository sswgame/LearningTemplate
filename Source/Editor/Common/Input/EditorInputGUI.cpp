/**
 * @file EditorInputGUI.cpp
 * @brief 맥락 메뉴(R1 · R2 · R3 · R6)와 끌어 놓기(`{종류, id}` 한 형식)의 ImGui 어댑터입니다.
 */
#include "pch.h"

#include "Editor/Common/Input/EditorInputGUI.h"

#include "Core/Container/string.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorInputGUIInternal
        {
            /** @brief 열린 맥락 메뉴 하나의 상태입니다. 팝업은 여러 프레임 열려 있으므로 연 순간의 대상을 들고 있는다(에디터 범위 상태 저장소가 소유). */
            struct ContextMenuState
            {
                uint32              _popupKey{ 0 }; ///< 연 팝업의 ImGui id(0 = 없음)
                EditorCommandTarget _target{};      ///< 연 순간의 대상(선택 스냅숏)
            };

            /** @brief 오른쪽 단추를 끌기 문턱 안에서 이번 프레임에 뗐으면 true 입니다(R1 — 끌었으면 비행 · 화면 이동이지 메뉴가 아니다). */
            static bool wasRightClickedWithoutDrag( float32 dragThreshold )
            {
                // 이번 누름 동안 가장 멀리 간 거리(ImGui 가 누를 때마다 새로 잰다)로 본다 — 뗀 자리만 보면 끌었다 되돌아온 누름이 클릭이 된다.
                const float32 maxDragSquared = ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right];
                return ImGui::IsMouseReleased( ImGuiMouseButton_Right ) && maxDragSquared <= dragThreshold * dragThreshold;
            }

            /** @brief @p subject 의 자리 위에 마우스가 있으면 true 입니다. 항목이면 직전 항목, 빈 곳이면 창 위 · 어느 항목 밖입니다. */
            static bool isOverSubject( const EditorContextMenuSubject& subject )
            {
                if ( subject._hoveredID != 0 )
                    return ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenBlockedByPopup );
                return ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByPopup ) && ImGui::IsAnyItemHovered() == false;
            }

            /** @brief @p target 에 대해 켜진 항목이 있으면 true 입니다(R6 — 켜진 항목이 없으면 열지 않는다). */
            static bool hasEnabledItem( const EditorCommandRegistry& registry, const vector<EditorMenuItem>& listItem, const EditorCommandTarget& target )
            {
                for ( const EditorMenuItem& item : listItem )
                {
                    if ( EditorCommandRegistry::isEnabled( registry.getCommands()[item._commandIndex], target ) )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorInputGUI::drawContextMenuFor( const EditorContextMenuSubject& subject )
    {
        EditorContext&                            editorContext = editor::getSubsystem<EditorContext>();
        EditorCommandRegistry&                    registry      = editorContext.getSubsystem<EditorCommandRegistry>();
        const EditorInputActions&                 inputActions  = editorContext.getSubsystem<EditorInputActions>();
        EditorInputGUIInternal::ContextMenuState& state         = getEditorState<EditorInputGUIInternal::ContextMenuState>();
        const ImGuiID                             popupKey      = ImGui::GetID( subject._pPopupID );
        const float32                             dragThreshold = inputActions.getInputMap().getDragThreshold();
        vector<EditorMenuItem>                    listItem;

        const bool bOpenRequested = EditorInputGUIInternal::isOverSubject( subject ) && EditorInputGUIInternal::wasRightClickedWithoutDrag( dragThreshold );
        if ( bOpenRequested )
        {
            bool                bSelectOnlyHovered = false;
            EditorCommandTarget target             = EditorContextMenuRules::makeMenuTarget( subject._kind, subject._hoveredID, subject._listSelectedID, bSelectOnlyHovered );
            target._worldPosition                  = subject._worldPosition;
            target._bHasWorldPosition              = subject._bHasWorldPosition;
            registry.collectContextMenuItems( subject._kind, listItem );
            if ( EditorInputGUIInternal::hasEnabledItem( registry, listItem, target ) )
            {
                if ( bSelectOnlyHovered && subject._selectOnly.isBound() )
                    subject._selectOnly( subject._hoveredID );
                state._popupKey = popupKey;
                state._target   = std::move( target );
                ImGui::OpenPopup( subject._pPopupID );
            }
        }

        if ( state._popupKey != popupKey || ImGui::BeginPopup( subject._pPopupID ) == false )
            return false;
        // 팝업이 열려 있는 동안 항목은 연 순간의 대상 종류로 다시 모은다 — 모듈 리로드로 등록부가 바뀌어도 낡은 인덱스를 읽지 않는다.
        registry.collectContextMenuItems( state._target._kind, listItem );
        string chosenCommandID;
        string shortcutLabel;
        for ( const EditorMenuItem& item : listItem )
        {
            const EditorCommandDesc& desc = registry.getCommands()[item._commandIndex];
            if ( item._bSeparatorBefore )
                ImGui::Separator();
            fixed_string<constant::kMaxBuffer128> menuLabel;
            EditorCommandRegistry::formatMenuLabel( desc, menuLabel );
            inputActions.formatShortcutLabel( desc._inputAction, shortcutLabel );
            const utf8* pShortcut = shortcutLabel.empty() ? nullptr : shortcutLabel.c_str();
            if ( ImGui::MenuItem( menuLabel.c_str(), pShortcut, false, EditorCommandRegistry::isEnabled( desc, state._target ) ) )
                chosenCommandID = desc._id;
        }
        ImGui::EndPopup();
        // 고른 명령은 팝업을 닫은 뒤 실행한다 — 명령이 등록부 · 선택을 바꿔도 그리는 중인 목록을 읽지 않는다.
        if ( chosenCommandID.empty() == false )
        {
            const EditorCommandTarget target = std::move( state._target );
            state._popupKey                  = 0;
            (void)registry.execute( chosenCommandID, target ); // 그 사이 꺼졌으면 아무 일도 없다
        }
        return true;
    }

    bool EditorInputGUI::beginDragSource( const EditorDragPayload& payload, const utf8* pPreviewLabel )
    {
        if ( ImGui::BeginDragDropSource() == false )
            return false;
        (void)ImGui::SetDragDropPayload( kPayloadTypeName, &payload, sizeof( payload ) ); // 처음 실은 프레임인지는 쓰지 않는다
        if ( pPreviewLabel != nullptr )
            ImGui::TextUnformatted( pPreviewLabel );
        ImGui::EndDragDropSource();
        return true;
    }

    bool EditorInputGUI::acceptDrop( EditorPayloadKindSet acceptedKinds, EditorDragPayload& outPayload )
    {
        // 놓기 전에 종류를 본다 — 집합 밖이면 놓을 자리로 강조하지도 않는다.
        const ImGuiPayload* pPeek       = ImGui::GetDragDropPayload();
        const bool          bOurPayload = pPeek != nullptr && pPeek->IsDataType( kPayloadTypeName ) && pPeek->DataSize == static_cast<int32>( sizeof( EditorDragPayload ) );
        if ( bOurPayload == false )
            return false;
        EditorDragPayload peeked{};
        std::memcpy( &peeked, pPeek->Data, sizeof( peeked ) );
        if ( acceptedKinds.contains( peeked._kind ) == false || ImGui::BeginDragDropTarget() == false )
            return false;
        const ImGuiPayload* pDelivered = ImGui::AcceptDragDropPayload( kPayloadTypeName );
        if ( pDelivered != nullptr )
            std::memcpy( &outPayload, pDelivered->Data, sizeof( outPayload ) );
        ImGui::EndDragDropTarget();
        return pDelivered != nullptr;
    }
} // namespace sw::editor
