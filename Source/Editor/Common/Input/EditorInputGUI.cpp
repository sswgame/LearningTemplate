/**
 * @file EditorInputGUI.cpp
 * @brief 맥락 메뉴(R1 · R2 · R3 · R6), 끌어 놓기(`{종류, id}` 한 형식), 항목 클릭 · 포인터 상태, 비맥락 팝업의 ImGui 어댑터입니다.
 */
#include "pch.h"

#include "Editor/Common/Input/EditorInputGUI.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Input/RawInputEvent.h"

#include <imgui.h>
#include <imgui_internal.h>

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

            /** @brief 페이로드 이름 표입니다(종류마다 id → 이름). 끌기는 문자열이 아니라 id 를 싣고, 받는 쪽이 이 표로 이름을 되찾는다. */
            struct PayloadNameState
            {
                unordered_map<uint64, string> _arrMapName[static_cast<size_t>( EditorPayloadKind::Count )];
            };

            /** @brief @p button 을 끌기 문턱 안에서 이번 프레임에 뗐으면 true 입니다(R1 — 끌었으면 비행 · 화면 이동이지 메뉴가 아니다). */
            static bool wasClickedWithoutDrag( ImGuiMouseButton button )
            {
                // 이번 누름 동안 가장 멀리 간 거리(ImGui 가 누를 때마다 새로 잰다)로 본다 — 뗀 자리만 보면 끌었다 되돌아온 누름이 클릭이 된다.
                // 문턱은 입력 맵의 값이다(`EditorInputActions::processFrame` 이 `MouseDragThreshold` 에 넣는다).
                const ImGuiIO& io = ImGui::GetIO();
                return ImGui::IsMouseReleased( button ) && io.MouseDragMaxDistanceSqr[button] <= io.MouseDragThreshold * io.MouseDragThreshold;
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

            /** @brief 끌려 오는 페이로드가 우리 형식이고 받을 종류면 @p outPayload 에 담고 true 입니다(놓기 전에 보는 것 — 집합 밖이면 강조하지 않는다). */
            static bool peekAcceptedPayload( EditorPayloadKindSet acceptedKinds, EditorDragPayload& outPayload )
            {
                const ImGuiPayload* pPeek       = ImGui::GetDragDropPayload();
                const bool          bOurPayload = pPeek != nullptr && pPeek->IsDataType( EditorInputGUI::kPayloadTypeName ) &&
                                         pPeek->DataSize == static_cast<int32>( sizeof( EditorDragPayload ) );
                if ( bOurPayload == false )
                    return false;
                std::memcpy( &outPayload, pPeek->Data, sizeof( outPayload ) );
                return acceptedKinds.contains( outPayload._kind );
            }

            /** @brief `BeginDragDropTarget` 이 열린 뒤 우리 형식을 받습니다. 받았으면 @p outPayload 에 담고 true 이며, 놓인 것이면 @p outbDelivered 가 true 입니다. */
            static bool acceptOpenTarget( ImGuiDragDropFlags flags, EditorDragPayload& outPayload, bool& outbDelivered )
            {
                const ImGuiPayload* pPayload = ImGui::AcceptDragDropPayload( EditorInputGUI::kPayloadTypeName, flags );
                if ( pPayload == nullptr )
                    return false;
                std::memcpy( &outPayload, pPayload->Data, sizeof( outPayload ) );
                outbDelivered = pPayload->IsDelivery();
                return true;
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
        vector<EditorMenuItem>                    listItem;

        const bool bOpenRequested = subject._bCallerDecidesOpen
                                      ? subject._bOpenRequested
                                      : EditorInputGUIInternal::isOverSubject( subject ) && EditorInputGUIInternal::wasClickedWithoutDrag( ImGuiMouseButton_Right );
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
            const bool bEnabled = EditorCommandRegistry::isEnabled( desc, state._target );
            // 시나리오가 누를 표식은 `menu.<명령 id>` 다 — 어느 창의 메뉴든 같은 명령은 같은 표식이다.
            fixed_string<constant::kMaxBuffer128> markKey{ "menu." };
            markKey.append( desc._id.c_str() );
            if ( desc._submenu.isBound() )
            {
                // 하위 메뉴는 열린 동안 그 명령의 함수가 안을 그린다(대상에 따라 항목이 바뀌는 메뉴 — Add Component · Select All With).
                const bool bSubmenuOpen = ImGui::BeginMenu( menuLabel.c_str(), bEnabled );
                EditorSelfTestMarks::note( markKey.c_str() );
                if ( bSubmenuOpen )
                {
                    desc._submenu( state._target );
                    ImGui::EndMenu();
                }
                continue;
            }
            inputActions.formatShortcutLabel( desc._inputAction, shortcutLabel );
            const utf8* pShortcut = shortcutLabel.empty() ? nullptr : shortcutLabel.c_str();
            if ( ImGui::MenuItem( menuLabel.c_str(), pShortcut, false, bEnabled ) )
                chosenCommandID = desc._id;
            EditorSelfTestMarks::note( markKey.c_str() );
        }
        ImGui::EndPopup();
        // 고른 명령은 다음 프레임 첫머리에 실행한다 — 패널이 그리는 중인 목록(컴포넌트 카드 · 행)이 명령 뒤에 남지 않는다.
        if ( chosenCommandID.empty() == false )
        {
            editorContext.getSubsystem<EditorInputActions>().queueCommand( chosenCommandID, state._target );
            state._popupKey = 0;
        }
        return true;
    }

    bool EditorInputGUI::beginDragSource( const EditorDragPayload& payload, const utf8* pPreviewLabel, bool bAllowNullItem )
    {
        const ImGuiDragDropFlags flags = bAllowNullItem ? ImGuiDragDropFlags_SourceAllowNullID : ImGuiDragDropFlags_None;
        if ( ImGui::BeginDragDropSource( flags ) == false )
            return false;
        (void)ImGui::SetDragDropPayload( kPayloadTypeName, &payload, sizeof( payload ) ); // 처음 실은 프레임인지는 쓰지 않는다
        if ( pPreviewLabel != nullptr )
            ImGui::TextUnformatted( pPreviewLabel );
        ImGui::EndDragDropSource();
        return true;
    }

    bool EditorInputGUI::acceptDrop( EditorPayloadKindSet acceptedKinds, EditorDragPayload& outPayload )
    {
        EditorDragPayload peeked{};
        if ( EditorInputGUIInternal::peekAcceptedPayload( acceptedKinds, peeked ) == false || ImGui::BeginDragDropTarget() == false )
            return false;
        bool       bDelivered = false;
        const bool bAccepted  = EditorInputGUIInternal::acceptOpenTarget( ImGuiDragDropFlags_None, outPayload, bDelivered );
        ImGui::EndDragDropTarget();
        return bAccepted && bDelivered;
    }

    bool EditorInputGUI::previewDrop( EditorPayloadKindSet acceptedKinds, EditorDragPayload& outPayload, bool& outbDelivered )
    {
        outbDelivered = false;
        EditorDragPayload peeked{};
        if ( EditorInputGUIInternal::peekAcceptedPayload( acceptedKinds, peeked ) == false || ImGui::BeginDragDropTarget() == false )
            return false;
        const ImGuiDragDropFlags flags     = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
        const bool               bAccepted = EditorInputGUIInternal::acceptOpenTarget( flags, outPayload, outbDelivered );
        ImGui::EndDragDropTarget();
        return bAccepted;
    }

    bool EditorInputGUI::acceptDropInRect( const float2& minPos, const float2& maxPos, const utf8* pTargetID, EditorPayloadKindSet acceptedKinds, EditorDragPayload& outPayload )
    {
        EditorDragPayload peeked{};
        if ( EditorInputGUIInternal::peekAcceptedPayload( acceptedKinds, peeked ) == false )
            return false;
        const ImRect rect( ImVec2( minPos._x, minPos._y ), ImVec2( maxPos._x, maxPos._y ) );
        if ( ImGui::BeginDragDropTargetCustom( rect, ImGui::GetID( pTargetID ) ) == false )
            return false;
        bool       bDelivered = false;
        const bool bAccepted  = EditorInputGUIInternal::acceptOpenTarget( ImGuiDragDropFlags_None, outPayload, bDelivered );
        ImGui::EndDragDropTarget();
        return bAccepted && bDelivered;
    }

    uint64 EditorInputGUI::registerPayloadName( EditorPayloadKind kind, string_view name )
    {
        // id 는 이름의 해시다 — 같은 이름은 어느 패널에서 끌어도 같은 id 이고, 표는 받는 쪽이 이름을 되찾는 데만 쓴다.
        const uint64                              id    = HashUtil::computeStringHash64( name, false );
        EditorInputGUIInternal::PayloadNameState& state = getEditorState<EditorInputGUIInternal::PayloadNameState>();
        state._arrMapName[static_cast<size_t>( kind )].insert_or_assign( id, string( name ) );
        return id;
    }

    bool EditorInputGUI::findPayloadName( EditorPayloadKind kind, uint64 id, string& outName )
    {
        const EditorInputGUIInternal::PayloadNameState& state = getEditorState<EditorInputGUIInternal::PayloadNameState>();
        const unordered_map<uint64, string>&            map   = state._arrMapName[static_cast<size_t>( kind )];
        const auto                                      found = map.find( id );
        if ( found == map.end() )
            return false;
        outName = found->second;
        return true;
    }

    bool EditorInputGUI::beginItemContextMenu( const utf8* pPopupID )
    {
        if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenBlockedByPopup ) && EditorInputGUIInternal::wasClickedWithoutDrag( ImGuiMouseButton_Right ) )
            ImGui::OpenPopup( pPopupID );
        return ImGui::BeginPopup( pPopupID );
    }

    bool EditorInputGUI::beginWindowContextMenu( const utf8* pPopupID )
    {
        const bool bOverEmpty = ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByPopup ) && ImGui::IsAnyItemHovered() == false;
        if ( bOverEmpty && EditorInputGUIInternal::wasClickedWithoutDrag( ImGuiMouseButton_Right ) )
            ImGui::OpenPopup( pPopupID );
        return ImGui::BeginPopup( pPopupID );
    }

    bool EditorInputGUI::wasItemClicked( EditorMouseButton button )
    {
        return ImGui::IsItemClicked( static_cast<ImGuiMouseButton>( button ) );
    }

    bool EditorInputGUI::wasItemDoubleClicked( EditorMouseButton button )
    {
        return ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( static_cast<ImGuiMouseButton>( button ) );
    }

    EditorPointerState EditorInputGUI::readPointer()
    {
        const ImGuiIO&     io = ImGui::GetIO();
        EditorPointerState state{};
        state._position        = float2{ io.MousePos.x, io.MousePos.y };
        state._delta           = float2{ io.MouseDelta.x, io.MouseDelta.y };
        state._wheel           = io.MouseWheel;
        state._wheelHorizontal = io.MouseWheelH;
        state._modifierMask    = getModifierMask();
        for ( size_t index = 0; index < EditorPointerState::kButtonCount; ++index )
        {
            const ImGuiMouseButton button       = static_cast<ImGuiMouseButton>( index );
            state._arrDown[index]               = ImGui::IsMouseDown( button );
            state._arrPressed[index]            = ImGui::IsMouseClicked( button );
            state._arrReleased[index]           = ImGui::IsMouseReleased( button );
            state._arrDoubleClicked[index]      = ImGui::IsMouseDoubleClicked( button );
            state._arrDragging[index]           = ImGui::IsMouseDragging( button );
            state._arrClickedWithoutDrag[index] = EditorInputGUIInternal::wasClickedWithoutDrag( button );
            const ImVec2 dragDelta              = ImGui::GetMouseDragDelta( button, 0.0f );
            state._arrDragDelta[index]          = float2{ dragDelta.x, dragDelta.y };
        }
        return state;
    }

    uint8 EditorInputGUI::getModifierMask()
    {
        const ImGuiIO& io   = ImGui::GetIO();
        uint8          mask = ModifierKey::None;
        if ( io.KeyCtrl )
            mask = static_cast<uint8>( mask | ModifierKey::Ctrl );
        if ( io.KeyShift )
            mask = static_cast<uint8>( mask | ModifierKey::Shift );
        if ( io.KeyAlt )
            mask = static_cast<uint8>( mask | ModifierKey::Alt );
        if ( io.KeySuper )
            mask = static_cast<uint8>( mask | ModifierKey::Super );
        return mask;
    }

    bool EditorInputGUI::wasCancelPressed()
    {
        return ImGui::IsKeyPressed( ImGuiKey_Escape, false );
    }

    void EditorInputGUI::openPopup( const utf8* pPopupID )
    {
        (void)ImGui::OpenPopup( pPopupID ); // 이미 열려 있으면 그대로다
    }

    bool EditorInputGUI::wasActionTriggered( string_view action )
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorInputActions>().wasActionTriggered( action );
    }

    bool EditorInputGUI::isActionDown( string_view action )
    {
        return editor::getSubsystem<EditorContext>().getSubsystem<EditorInputActions>().isActionDown( action );
    }

    void EditorInputGUI::setModeLayerEnabled( string_view layer, bool bEnabled )
    {
        editor::getSubsystem<EditorContext>().getSubsystem<EditorInputActions>().setModeLayerEnabled( layer, bEnabled );
    }
} // namespace sw::editor
