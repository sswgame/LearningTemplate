#include "pch.h"

#include "Editor/Panels/Settings/ShortcutsPanel.h"

#include "Editor/Common/Backend/ImGuiKeyForward.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Input/RawInputEvent.h"
#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ShortcutsPanelInternal
        {
            static constexpr Color kConflictRowBg{ 0.55f, 0.12f, 0.12f, 0.6f };

            /** @brief 그 커맨드 줄의 이름입니다(`shortcuts.<동작>.<커맨드 id>`). */
            static fixed_string<constant::buffer::kMax128> makeMark( const utf8* pAction, const string& commandID )
            {
                fixed_string<constant::buffer::kMax128> mark;
                formatstring( mark.data(), mark.capacity(), "shortcuts.%#.%#", pAction, commandID.c_str() );
                return mark;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ShortcutsPanel::ShortcutsPanel()
        : IEditorPanel( false )
        , _filter{}
        , _capture{}
        , _viewModel{}
    {
    }

    void ShortcutsPanel::drawContent()
    {
        const EditorInputActions& inputActions = ShortcutsViewModel::getInputActions();
        IShortcutBindings&        bindings     = _viewModel;

        pollShortcutCapture( bindings );

        // 단추를 먼저 둔다 — 검색 칸이 남은 폭을 다 쓰므로 뒤에 두면 창 밖으로 밀린다.
        if ( ImGui::Button( SW_LOCKEY( "EdText", "ResetAll" ) ) )
            _capture.resetAllShortcuts( bindings );
        EditorSelfTestMarks::record( "shortcuts.resetAll" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ResetAllShortcuts" ) );
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##ShortcutFilter", _filter, SW_LOCKEY( "EditorSettings", "SearchShortcutsHint" ), 0.0f, false );

        drawConflictPrompt( bindings );
        drawShortcutTable( inputActions, bindings );
    }

    void ShortcutsPanel::pollShortcutCapture( IShortcutBindings& bindings )
    {
        // 수정 키만 누른 동안은 조합이 아니다 — 기다린다.
        if ( _capture.isWaitingForChord() == false )
            return;
        Key   pressedKey      = Key::Unknown;
        uint8 pressedModifier = ModifierKey::None;
        if ( ImGuiKeyForward::findPressedChord( pressedKey, pressedModifier ) )
            _capture.submitChord( bindings, pressedKey, pressedModifier );
    }

    void ShortcutsPanel::drawConflictPrompt( IShortcutBindings& bindings )
    {
        if ( _capture.hasConflict() == false )
            return;
        EditorThemeUtil::textError( SW_LOCKEY( "EditorSettings", "ShortcutInUse" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "Replace" ) ) )
            _capture.confirmReplace( bindings );
        EditorSelfTestMarks::record( "shortcuts.replace" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ReplaceShortcut" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Cancel" ) ) );
        if ( ImGui::Button( SW_LOCKEY( "EdVerb", "Cancel" ) ) )
            _capture.cancel( bindings );
        EditorSelfTestMarks::record( "shortcuts.cancel" );
    }

    void ShortcutsPanel::drawShortcutTable( const EditorInputActions& inputActions, IShortcutBindings& bindings )
    {
        const EditorCommandRegistry& registry = ShortcutsViewModel::getCommandRegistry();
        const EditorListFilter       filter{ _filter.c_str() };
        if ( ImGui::BeginTable( "##Shortcuts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY ) == false )
            return;
        // 단추 칸은 Set + Reset 두 단추 폭만 잡는다 — 고정 160px 이 이름 칸을 눌러 "Compile Game (SWGa" 처럼 잘렸다(uiaudit).
        const float32 actionsWidth = EditorChrome::measureSmallButton( SW_LOCKEY( "EdText", "Set" ) ) + ImGui::GetStyle().ItemSpacing.x +
                                     EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reset" ) );
        ImGui::TableSetupColumn( "Command", ImGuiTableColumnFlags_WidthStretch, 2.0f );
        ImGui::TableSetupColumn( "Category", ImGuiTableColumnFlags_WidthStretch, 1.0f );
        ImGui::TableSetupColumn( "Shortcut", ImGuiTableColumnFlags_WidthStretch, 1.5f );
        ImGui::TableSetupColumn( "##Actions", ImGuiTableColumnFlags_WidthFixed, actionsWidth );
        ImGui::TableHeadersRow();
        string label;
        for ( const EditorCommandDesc& desc : registry.getCommands() )
        {
            if ( desc._inputAction.empty() )
                continue;
            inputActions.formatShortcutLabel( desc._inputAction, label );
            if ( filter.matchesAny( { desc._label, desc._id, desc._category, label } ) == false )
                continue;
            drawShortcutRow( inputActions, bindings, desc, label );
        }
        ImGui::EndTable();
    }

    void ShortcutsPanel::drawShortcutRow( const EditorInputActions& inputActions, IShortcutBindings& bindings, const EditorCommandDesc& desc, const string& shortcutLabel )
    {
        ImGui::PushID( desc._id.c_str() );
        ImGui::TableNextRow();
        if ( _capture.isConflictRow( desc._inputAction ) )
            ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32( EditorColorUtil::toImVec4( ShortcutsPanelInternal::kConflictRowBg ) ) );
        ImGui::TableSetColumnIndex( 0 );
        EditorWidgets::drawClampedLabel( desc._label, ImGui::GetContentRegionAvail().x, 1 ); // 좁으면 말줄임 + 호버에 전체
        ImGui::TableSetColumnIndex( 1 );
        ImGui::TextDisabled( "%s", desc._category.c_str() );
        ImGui::TableSetColumnIndex( 2 );
        if ( _capture.isCapturing( desc._inputAction ) )
            EditorThemeUtil::textAccent( SW_LOCKEY( "EditorSettings", "PressKeyCombo" ) );
        else
            EditorWidgets::drawClampedLabel( shortcutLabel, ImGui::GetContentRegionAvail().x, 1 );
        ImGui::TableSetColumnIndex( 3 );
        if ( ImGui::SmallButton( SW_LOCKEY( "EdText", "Set" ) ) )
            _capture.beginCapture( bindings, desc._inputAction );
        EditorSelfTestMarks::record( ShortcutsPanelInternal::makeMark( "set", desc._id ).c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "SetShortcut" ) );
        if ( inputActions.isShortcutModified( desc._inputAction ) )
        {
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( SW_LOCKEY( "EdVerb", "Reset" ) ) );
            if ( ImGui::SmallButton( SW_LOCKEY( "EdVerb", "Reset" ) ) )
                _capture.resetShortcut( bindings, desc._inputAction );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ResetShortcut" ) );
        }
        ImGui::PopID();
    }

    void ShortcutsPanel::shutdown()
    {
        _capture.cancel( _viewModel );
    }

    void ShortcutsPanel::onPanelCollapsed()
    {
        _capture.cancel( _viewModel );
    }

    void ShortcutsPanel::onPanelClosed()
    {
        _capture.cancel( _viewModel );
    }

    uint32 ShortcutsPanel::getPendingConflictCount()
    {
        const ShortcutsPanel* pPanel = EditorPanelManager::getInEditorContext().findPanel<ShortcutsPanel>(); // 필수 서비스: 없으면 버그(getInEditorContext 가 멈춘다)
        return ( pPanel != nullptr && pPanel->_capture.hasConflict() ) ? 1u : 0u;                            // 패널이 없으면 묻는 충돌도 없다
    }

    SW_EDITOR_PANEL( ShortcutsPanel, ShortcutsPanel::kPanelID, EditorPanelCategory::Tool, 2010 );
} // namespace sw::editor
