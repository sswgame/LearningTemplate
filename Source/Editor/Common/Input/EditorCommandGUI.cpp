/**
 * @file EditorCommandGUI.cpp
 * @brief 커맨드 레지스트리를 ImGui 의 메뉴 항목 · 명령 단추로 드러냅니다(MVVM View — 레지스트리 · 입력 맵만 본다). 기본 커맨드 테이블은 `EditorDefaultCommands.cpp` 다.
 */
#include "pch.h"

#include "Editor/Common/Input/EditorCommandGUI.h"

#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorCommandGUIInternal
        {
            /** @brief 명령 단추 하나(도구줄 아이콘 또는 아이콘 + 이름표). 정의는 `EditorCommandGUI::drawToolbarButton` 아래. */
            static bool drawCommandButton( string_view commandID, bool bIconOnly );

            // ------------------------------------------------------------------------------
            // 4) ImGui 연결 — 메뉴 항목. 단축키 글자는 에디터 입력 맵에서 만든다
            // ------------------------------------------------------------------------------
            /** @brief 커맨드 @p commandIndex 를 메뉴 항목으로 그립니다. 켜짐은 프레임 앞에서 Model 이 낸 값 묶음에서 읽는다(View 는 씬 · 세션을 읽지 않는다). 눌렸으면 커맨드를 요청합니다. */
            static void drawCommandItem( const EditorCommandRegistry& registry, const EditorInputActions& inputActions, uint32 commandIndex )
            {
                const EditorCommandDesc&                desc = registry.getCommands()[commandIndex];
                fixed_string<constant::buffer::kMax128> menuLabel;
                EditorCommandRegistry::formatMenuLabel( desc, menuLabel );

                string shortcutLabel;
                inputActions.getShortcuts().formatShortcutLabel( desc._inputAction, shortcutLabel );

                const utf8* pShortcut = shortcutLabel.empty() ? nullptr : shortcutLabel.c_str();
                const bool  bEnabled  = registry.isEnabledInSnapshot( commandIndex );

                if ( ImGui::MenuItem( menuLabel.c_str(), pShortcut, false, bEnabled ) )
                    (void)EditorCommandRegistry::getInEditorContext().request( desc._id, EditorCommandTarget::none() ); // 꺼진 항목은 그려질 때 이미 회색이다 — 씬 편집은 게임 스레드가 실행한다

                if ( desc._tooltip.empty() == false )
                {
                    fixed_string<constant::buffer::kMax256> tooltip;
                    tooltip.append( desc._tooltip.c_str() );
                    if ( shortcutLabel.empty() == false )
                    {
                        tooltip.append( " (" );
                        tooltip.append( shortcutLabel.c_str() );
                        tooltip.append( ")" );
                    }
                    EditorWidgets::drawTooltip( tooltip.c_str() );
                }
            }

            /** @brief 메뉴 하나의 항목을 구분선과 함께 그립니다. */
            static void drawMenu( const EditorCommandRegistry& registry, const EditorMenu& menu )
            {
                const EditorInputActions& inputActions = EditorInputActions::getInEditorContext();
                for ( const EditorMenuItem& item : menu._listItem )
                {
                    if ( item._bSeparatorBefore )
                        ImGui::Separator();
                    drawCommandItem( registry, inputActions, item._commandIndex );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorCommandGUI::drawMainMenus()
    {
        const EditorCommandRegistry& registry = EditorCommandRegistry::getInEditorContext();
        for ( const EditorMenu& menu : registry.getMenus() )
        {
            if ( menu._parentPath != commandmenu::kMainMenuBar )
                continue;
            fixed_string<constant::buffer::kMax128> menuLabel;
            menuLabel.append( menu._label.c_str() );
            menuLabel.append( "###" ); // 보이는 이름은 언어마다 다르다 — ImGui ID 는 경로 마디로 고정한다
            menuLabel.append( menu._name.c_str() );
            if ( ImGui::BeginMenu( menuLabel.c_str() ) )
            {
                EditorCommandGUIInternal::drawMenu( registry, menu );
                ImGui::EndMenu();
            }
        }
    }

    void EditorCommandGUI::drawMenuItems( string_view menuPath )
    {
        const EditorCommandRegistry& registry = EditorCommandRegistry::getInEditorContext();
        const EditorMenu*            pMenu    = registry.findMenu( menuPath );
        if ( pMenu != nullptr )
            EditorCommandGUIInternal::drawMenu( registry, *pMenu );
    }

    bool EditorCommandGUI::drawToolbarButton( string_view commandID )
    {
        return EditorCommandGUIInternal::drawCommandButton( commandID, true );
    }

    bool EditorCommandGUI::drawCommandButton( string_view commandID )
    {
        return EditorCommandGUIInternal::drawCommandButton( commandID, false );
    }
} // namespace sw::editor

namespace sw::editor
{
    bool EditorCommandGUIInternal::drawCommandButton( string_view commandID, bool bIconOnly )
    {
        const EditorCommandRegistry& registry = EditorCommandRegistry::getInEditorContext();
        const EditorCommandDesc*     pDesc    = registry.find( commandID );
        if ( pDesc == nullptr )
            reportFatalError( "Toolbar button names command '%#' which is not registered", string( commandID ).c_str() );
        EditorInputActions& inputActions = EditorInputActions::getInEditorContext();

        const bool bEnabled = registry.isEnabledInSnapshot( static_cast<uint32>( pDesc - registry.getCommands().data() ) );
        ImGui::PushID( pDesc->_id.c_str() );
        ImGui::BeginDisabled( bEnabled == false );
        bool bPressed = false;
        if ( bIconOnly && pDesc->_iconName.empty() == false )
        {
            // 도구줄 아이콘은 상자 없이 둔다(drawIconToggle 과 같은 모양) — 정사각 한 변은 줄 높이라 글꼴 · DPI 를 따른다.
            const float32 side = ImGui::GetFrameHeight();
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f } );
            ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
            bPressed = ImGui::Button( EditorIconGlyphTable::findGlyphByName( pDesc->_iconName.c_str() ), ImVec2{ side, side } );
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        else
        {
            fixed_string<constant::buffer::kMax128> label;
            EditorCommandRegistry::formatMenuLabel( *pDesc, label ); // 아이콘 + 이름표(메뉴와 같은 글)
            bPressed = ImGui::Button( label.c_str() );
        }
        ImGui::EndDisabled();
        ImGui::PopID();

        EditorSelfTestMarks::recordFormat( "toolbar.%#", pDesc->_id.c_str() );

        if ( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled ) )
        {
            string shortcutLabel;
            inputActions.getShortcuts().formatShortcutLabel( pDesc->_inputAction, shortcutLabel );
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "TooltipWrapEm" ) );
            ImGui::TextUnformatted( pDesc->_label.c_str() );
            if ( shortcutLabel.empty() == false )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "%s", shortcutLabel.c_str() );
            }
            if ( pDesc->_tooltip.empty() == false )
                ImGui::TextDisabled( "%s", pDesc->_tooltip.c_str() );
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        if ( bPressed )
            inputActions.queueCommand( pDesc->_id, EditorCommandTarget::none() );
        return bPressed;
    }
} // namespace sw::editor
