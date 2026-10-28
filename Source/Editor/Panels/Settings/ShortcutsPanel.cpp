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
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
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
            static fixed_string<constant::kMaxBuffer128> makeMark( const utf8* pAction, const string& commandID )
            {
                fixed_string<constant::kMaxBuffer128> mark;
                formatstring( mark.data(), mark.capacity(), "shortcuts.%#.%#", pAction, commandID.c_str() );
                return mark;
            }

            static EditorInputActions& getInputActions() { return editor::getSubsystem<EditorContext>().getSubsystem<EditorInputActions>(); }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ShortcutsPanel::ShortcutsPanel()
        : IEditorPanel( false )
        , _filter{}
        , _captureAction{}
        , _conflictAction{}
        , _pendingKey{ Key::Unknown }
        , _pendingModifier{ ModifierKey::None }
    {
    }

    void ShortcutsPanel::drawContent()
    {
        EditorInputActions& inputActions = ShortcutsPanelInternal::getInputActions();

        pollShortcutCapture( inputActions );

        // 단추를 먼저 둔다 — 검색 칸이 남은 폭을 다 쓰므로 뒤에 두면 창 밖으로 밀린다.
        if ( ImGui::Button( "Reset All" ) )
            inputActions.resetAllShortcuts();
        EditorSelfTestMarks::note( "shortcuts.resetAll" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ResetAllShortcuts" ) );
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##ShortcutFilter", _filter, SW_LOCKEY( "EditorSettings", "SearchShortcutsHint" ), 0.0f, false );

        drawConflictPrompt( inputActions );
        drawShortcutTable( inputActions );
    }

    void ShortcutsPanel::pollShortcutCapture( EditorInputActions& inputActions )
    {
        // 조합 받기 — 수정 키만 누른 동안은 기다린다. Esc 는 취소다(유니티 · 언리얼과 같다).
        if ( _captureAction.empty() || _conflictAction.empty() == false )
            return;
        Key   pressedKey      = Key::Unknown;
        uint8 pressedModifier = ModifierKey::None;
        if ( ImGuiKeyForward::findPressedChord( pressedKey, pressedModifier ) == false )
            return;
        if ( pressedKey == Key::Escape && pressedModifier == ModifierKey::None )
        {
            finishCapture();
            return;
        }
        hashed_string conflictAction{};
        if ( inputActions.rebindShortcut( _captureAction, pressedKey, pressedModifier, conflictAction ) )
        {
            finishCapture();
            return;
        }
        if ( conflictAction.empty() )
        {
            finishCapture(); // 키보드 바인딩이 없는 액션 — 바꿀 것이 없다
            return;
        }
        _conflictAction  = conflictAction.c_str();
        _pendingKey      = pressedKey;
        _pendingModifier = pressedModifier;
    }

    void ShortcutsPanel::drawConflictPrompt( EditorInputActions& inputActions )
    {
        if ( _conflictAction.empty() )
            return;
        EditorThemeUtil::textError( SW_LOCKEY( "EditorSettings", "ShortcutInUse" ) );
        if ( ImGui::Button( "Replace" ) )
        {
            // 바꾸기: 받은 조합은 이 액션이, 이 액션의 옛 조합은 겹친 액션이 갖는다(언리얼 · 유니티의 바꾸기와 같다).
            if ( inputActions.swapShortcut( _captureAction, _conflictAction, _pendingKey, _pendingModifier ) == false )
                EditorThemeUtil::textError( SW_LOCKEY( "EditorSettings", "ShortcutInUse" ) );
            finishCapture();
        }
        EditorSelfTestMarks::note( "shortcuts.replace" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ReplaceShortcut" ) );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Cancel" ) );
        if ( ImGui::Button( "Cancel" ) )
            finishCapture();
        EditorSelfTestMarks::note( "shortcuts.cancel" );
    }

    void ShortcutsPanel::drawShortcutTable( EditorInputActions& inputActions )
    {
        const EditorCommandRegistry& registry = editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>();
        const EditorListFilter       filter{ _filter.c_str() };
        if ( ImGui::BeginTable( "##Shortcuts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY ) == false )
            return;
        ImGui::TableSetupColumn( "Command" );
        ImGui::TableSetupColumn( "Category" );
        ImGui::TableSetupColumn( "Shortcut" );
        ImGui::TableSetupColumn( "##Actions", ImGuiTableColumnFlags_WidthFixed, 160.0f * EditorThemeUtil::getDPIScale() );
        ImGui::TableHeadersRow();
        string label;
        for ( const EditorCommandDesc& desc : registry.getCommands() )
        {
            if ( desc._inputAction.empty() )
                continue;
            inputActions.formatShortcutLabel( desc._inputAction, label );
            if ( filter.matchesAny( { desc._label, desc._id, desc._category, label } ) == false )
                continue;
            drawShortcutRow( inputActions, desc, label );
        }
        ImGui::EndTable();
    }

    void ShortcutsPanel::drawShortcutRow( EditorInputActions& inputActions, const EditorCommandDesc& desc, const string& shortcutLabel )
    {
        const bool bCapturing = desc._inputAction == _captureAction;
        const bool bConflict  = desc._inputAction == _conflictAction || ( _conflictAction.empty() == false && bCapturing );
        ImGui::PushID( desc._id.c_str() );
        ImGui::TableNextRow();
        if ( bConflict )
            ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32( EditorColorUtil::toImVec4( ShortcutsPanelInternal::kConflictRowBg ) ) );
        ImGui::TableSetColumnIndex( 0 );
        ImGui::TextUnformatted( desc._label.c_str() );
        ImGui::TableSetColumnIndex( 1 );
        ImGui::TextDisabled( "%s", desc._category.c_str() );
        ImGui::TableSetColumnIndex( 2 );
        if ( bCapturing )
            EditorThemeUtil::textAccent( SW_LOCKEY( "EditorSettings", "PressKeyCombo" ) );
        else
            ImGui::TextUnformatted( shortcutLabel.c_str() );
        ImGui::TableSetColumnIndex( 3 );
        if ( ImGui::SmallButton( "Set" ) )
        {
            _captureAction  = desc._inputAction;
            _conflictAction = {};
            inputActions.setCommandsSuspended( true );
        }
        EditorSelfTestMarks::note( ShortcutsPanelInternal::makeMark( "set", desc._id ).c_str() );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "SetShortcut" ) );
        if ( inputActions.isShortcutModified( desc._inputAction ) )
        {
            EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( "Reset" ) );
            if ( ImGui::SmallButton( "Reset" ) )
                inputActions.resetShortcut( desc._inputAction );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorSettings", "ResetShortcut" ) );
        }
        ImGui::PopID();
    }

    void ShortcutsPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        finishCapture();
    }

    uint32 ShortcutsPanel::getPendingConflictCount()
    {
        const ShortcutsPanel* pPanel = editor::getSubsystem<EditorPanelManager>().findPanel<ShortcutsPanel>(); // 필수 서비스: 없으면 버그(getSubsystem 이 멈춘다)
        return ( pPanel != nullptr && pPanel->_conflictAction.empty() == false ) ? 1u : 0u;                    // 패널이 없으면 묻는 충돌도 없다
    }

    void ShortcutsPanel::finishCapture()
    {
        _captureAction   = {};
        _conflictAction  = {};
        _pendingKey      = Key::Unknown;
        _pendingModifier = ModifierKey::None;
        // 에디터가 내려가는 중(패널 종료)이면 입력 서브시스템이 이미 없을 수 있다.
        EditorContext* pContext = editor::findSubsystem<EditorContext>(); // 선택: 종료 경로에서 컨텍스트가 먼저 사라진다
        if ( pContext != nullptr )
            pContext->getSubsystem<EditorInputActions>().setCommandsSuspended( false );
    }

    SW_EDITOR_PANEL( ShortcutsPanel, ShortcutsPanel::kPanelID, EditorPanelCategory::Tool, 2010 );
} // namespace sw::editor
