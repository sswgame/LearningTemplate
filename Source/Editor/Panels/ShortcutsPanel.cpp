#include "pch.h"

#include "Editor/Panels/ShortcutsPanel.h"

#include "Editor/Common/Commands/EditorShortcutOverrides.h"
#include "Editor/Common/GUI/EditorCommandGUI.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorListFilter.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct ShortcutsPanelInternal
        {
            static uint32& getPendingConflictCount()
            {
                static uint32 s_pendingConflictCount{ 0 };
                return s_pendingConflictCount;
            }

            /** @brief 그 커맨드 줄의 이름표입니다(`shortcuts.<동작>.<커맨드 id>`). */
            static fixed_string<constant::kMaxBuffer128> makeMark( const utf8* pAction, const string& commandID )
            {
                fixed_string<constant::kMaxBuffer128> mark;
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
        , _captureCommandID{}
        , _conflictCommandID{}
        , _pendingShortcut{}
    {
    }

    void ShortcutsPanel::drawContent()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        EditorCommandRegistry& registry = pContext->getCommandRegistry();

        // 조합 받기 — 수정자만 누른 동안은 기다린다. Esc 는 취소, Backspace 는 지운다(유니티 · 언리얼과 같다).
        if ( _captureCommandID.empty() == false && _conflictCommandID.empty() )
        {
            EditorCommandShortcut pressed{};
            if ( EditorCommandGUI::findPressedShortcut( pressed ) )
            {
                const EditorCommandDesc* pTarget = registry.find( _captureCommandID );
                if ( pressed._key == EditorCommandKey::Escape && pressed._modifier == commandmodifier::kNone )
                    finishCapture();
                else if ( pTarget != nullptr && pressed._key == EditorCommandKey::Backspace && pressed._modifier == commandmodifier::kNone )
                {
                    applyShortcut( *pTarget, EditorCommandShortcut{} );
                    finishCapture();
                }
                else if ( pTarget != nullptr )
                {
                    const EditorCommandDesc* pOther = nullptr;
                    for ( const EditorCommandDesc& desc : registry.getCommands() )
                    {
                        const bool bClash = desc._id != _captureCommandID && ( EditorCommandRegistry::isSameShortcut( desc._shortcut, pressed ) ||
                                                                               EditorCommandRegistry::isSameShortcut( desc._altShortcut, pressed ) );
                        if ( bClash && pOther == nullptr )
                            pOther = &desc;
                    }
                    if ( pOther != nullptr )
                    {
                        _conflictCommandID = pOther->_id;
                        _pendingShortcut   = pressed;
                    }
                    else
                    {
                        applyShortcut( *pTarget, pressed );
                        finishCapture();
                    }
                }
            }
        }
        ShortcutsPanelInternal::getPendingConflictCount() = _conflictCommandID.empty() ? 0u : 1u;

        // 단추를 먼저 둔다 — 검색 칸이 남은 폭을 다 쓰므로 뒤에 두면 창 밖으로 밀린다.
        if ( ImGui::Button( "Reset All" ) )
        {
            EditorCommandGUI::getShortcutOverrides().clear();
            saveAndRebuild();
        }
        EditorSelfTestMarks::note( "shortcuts.resetAll" );
        ImGui::SameLine();
        EditorWidgets::drawSearchField( "##ShortcutFilter", _filter, "Search commands or keys (Ctrl+S)...", 0.0f, false );

        if ( _conflictCommandID.empty() == false )
        {
            const EditorCommandDesc* pOther = registry.find( _conflictCommandID );
            EditorThemeUtil::textError( "This combo is already used. Replace clears it from the other command." );
            if ( ImGui::Button( "Replace" ) && pOther != nullptr )
            {
                const EditorCommandDesc*    pTarget = registry.find( _captureCommandID );
                const EditorCommandShortcut kNone{};
                const bool                  bOtherAlt = EditorCommandRegistry::isSameShortcut( pOther->_altShortcut, _pendingShortcut );
                EditorCommandGUI::getShortcutOverrides().setOverride( pOther->_id, bOtherAlt ? pOther->_shortcut : kNone, bOtherAlt ? kNone : pOther->_altShortcut,
                                                                      pOther->_defaultShortcut, pOther->_defaultAltShortcut );
                if ( pTarget != nullptr )
                    applyShortcut( *pTarget, _pendingShortcut );
                finishCapture();
            }
            EditorSelfTestMarks::note( "shortcuts.replace" );
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel" ) )
                finishCapture();
            EditorSelfTestMarks::note( "shortcuts.cancel" );
        }

        const EditorListFilter filter{ _filter.c_str() };
        if ( ImGui::BeginTable( "##Shortcuts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY ) == false )
            return;
        ImGui::TableSetupColumn( "Command" );
        ImGui::TableSetupColumn( "Category" );
        ImGui::TableSetupColumn( "Shortcut" );
        ImGui::TableSetupColumn( "##Actions", ImGuiTableColumnFlags_WidthFixed, 160.0f * EditorThemeUtil::getDpiScale() );
        ImGui::TableHeadersRow();
        for ( const EditorCommandDesc& desc : registry.getCommands() )
        {
            fixed_string<constant::kMaxBuffer64> label;
            EditorCommandRegistry::formatShortcutLabel( desc, label );
            if ( filter.matchesAny( { desc._label, desc._id, desc._category, label.c_str() } ) == false )
                continue;
            const bool bConflict = desc._id == _conflictCommandID || ( _conflictCommandID.empty() == false && desc._id == _captureCommandID );
            ImGui::PushID( desc._id.c_str() );
            ImGui::TableNextRow();
            if ( bConflict )
                ImGui::TableSetBgColor( ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32( ImVec4{ 0.55f, 0.12f, 0.12f, 0.6f } ) );
            ImGui::TableSetColumnIndex( 0 );
            ImGui::TextUnformatted( desc._label.c_str() );
            ImGui::TableSetColumnIndex( 1 );
            ImGui::TextDisabled( "%s", desc._category.c_str() );
            ImGui::TableSetColumnIndex( 2 );
            if ( desc._id == _captureCommandID )
                EditorThemeUtil::textAccent( "Press a key combo (Esc cancels, Backspace clears)" );
            else
                ImGui::TextUnformatted( label.c_str() );
            ImGui::TableSetColumnIndex( 3 );
            if ( ImGui::SmallButton( "Set" ) )
            {
                _captureCommandID  = desc._id;
                _conflictCommandID = {};
                EditorCommandGUI::setHotkeysSuspended( true );
            }
            EditorSelfTestMarks::note( ShortcutsPanelInternal::makeMark( "set", desc._id ).c_str() );
            const bool bModified = EditorCommandRegistry::isSameShortcut( desc._shortcut, desc._defaultShortcut ) == false ||
                                   EditorCommandRegistry::isSameShortcut( desc._altShortcut, desc._defaultAltShortcut ) == false;
            if ( bModified )
            {
                ImGui::SameLine();
                if ( ImGui::SmallButton( "Reset" ) )
                {
                    EditorCommandGUI::getShortcutOverrides().removeOverride( desc._id );
                    ImGui::PopID();
                    ImGui::EndTable();
                    saveAndRebuild();
                    return; // 레지스트리를 다시 만들었다 — 이 프레임의 표는 낡았다
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void ShortcutsPanel::shutdown( IRHIDevice* /*pRHIDevice*/ )
    {
        finishCapture();
    }

    uint32 ShortcutsPanel::getPendingConflictCount()
    {
        return ShortcutsPanelInternal::getPendingConflictCount();
    }

    void ShortcutsPanel::finishCapture()
    {
        _captureCommandID  = {};
        _conflictCommandID = {};
        _pendingShortcut   = {};
        EditorCommandGUI::setHotkeysSuspended( false );
    }

    void ShortcutsPanel::applyShortcut( const EditorCommandDesc& desc, const EditorCommandShortcut& shortcut )
    {
        // 받은 조합은 주 조합이 된다. 보조 조합은 그대로 둔다.
        EditorCommandGUI::getShortcutOverrides().setOverride( desc._id, shortcut, desc._altShortcut, desc._defaultShortcut, desc._defaultAltShortcut );
        saveAndRebuild();
    }

    void ShortcutsPanel::saveAndRebuild()
    {
        (void)EditorCommandGUI::getShortcutOverrides().saveToFile( EditorShortcutOverrides::getDefaultFilePath() ); // 실패는 saveToFile 이 경고로 알린다
        EditorCommandGUI::registerDefaults();
    }

    SW_EDITOR_PANEL( ShortcutsPanel, "shortcuts", EditorPanelCategory::Tool, 2010 );
} // namespace sw::editor
