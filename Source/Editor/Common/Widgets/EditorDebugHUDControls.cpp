#include "pch.h"

#include "Editor/Common/Widgets/EditorDebugHUDControls.h"

#include "Core/Log/Logger.h"

#include "DevTools/Client/DebugHUD/DebugHUD.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        /** @brief 게임 디버그 HUD 입니다(개발 도구의 기동 단계 서브시스템). 없으면(UI 없는 실행 · 테스트 호스트) nullptr 입니다. */
        DebugHUD* findEditorDebugHUDControlsTarget()
        {
            return editor::findSubsystem<DebugHUD>();
        }
    } // namespace

    SW_EDITOR_SETTINGS_PAGE( DebugHUD, "debug_hud", "Editor/Debug HUD", 360, &EditorDebugHUDControls::drawPreferencesPage );

    void EditorDebugHUDControls::drawMasterToggle( const utf8* pMark )
    {
        DebugHUD*  pHUD   = findEditorDebugHUDControlsTarget();
        const bool bShown = pHUD != nullptr && pHUD->isShown();
        ImGui::BeginDisabled( pHUD == nullptr );
        if ( EditorWidgets::drawIconToggle( "##debugHUD", SW_EDITOR_ICON( "Chart" ), bShown,
                                            "Debug HUD in the game view (Ctrl+F3) - right click for sections. It shows while playing" ) &&
             pHUD != nullptr )
            pHUD->setShown( bShown == false );
        ImGui::EndDisabled();
        EditorSelfTestMarks::record( pMark );
        drawSectionPopupOnRightClick( "##debugHUDSections" );
    }

    void EditorDebugHUDControls::drawSectionChecklist()
    {
        DebugHUD* pHUD = findEditorDebugHUDControlsTarget();
        if ( pHUD == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "DebugHUDNotRunning" ) );
            return;
        }
        for ( const DebugHUDSectionRegistration* pSection : editor::getSubsystem<DebugHUDRegistry>().getSections() )
        {
            bool bShown = DebugHUD::isSectionShown( *pSection );
            ImGui::PushID( pSection->_pName );
            if ( ImGui::Checkbox( pSection->_pTitle, &bShown ) && pHUD->setSectionShown( pSection->_pName, bShown ) == false )
                SW_LOG_WARNING_ID( "EditorDebugHUD.DebugHUDSectionNot", "Debug HUD section '%#' is not registered any more", pSection->_pName );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "DebugHUDSection" ) );
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::record( ( string{ "debugHUD.section." } + pSection->_pName ).c_str() );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", pSection->_pName );
            ImGui::PopID();
        }
    }

    void EditorDebugHUDControls::drawLayoutSettings()
    {
        DebugHUD* pHUD = findEditorDebugHUDControlsTarget();
        if ( pHUD == nullptr )
            return;
        int32 corner = static_cast<int32>( DebugHUD::getCorner() );
        if ( EditorWidgets::beginCombo( EditorThemeUtil::makeStableLabel( SW_LOCKEY( "EdWidget", "Corner" ), "Corner" ), DebugHUD::getCornerName( DebugHUD::getCorner() ) ) )
        {
            for ( int32 option = 0; option < static_cast<int32>( DebugHUDCorner::Count ); ++option )
            {
                if ( ImGui::Selectable( DebugHUD::getCornerName( static_cast<DebugHUDCorner>( option ) ), option == corner ) )
                    pHUD->setCorner( static_cast<DebugHUDCorner>( option ) );
            }
            ImGui::EndCombo();
        }
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "DebugHUDCorner" ) );
        float32 opacity = DebugHUD::getOpacity();
        EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "Opacity" ) );
        if ( EditorWidgets::sliderFloat( SW_WIDGET_ID( "##Opacity" ), &opacity, DebugHUD::kMinOpacity, DebugHUD::kMaxOpacity, "%.2f" ) )
            pHUD->setOpacity( opacity );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "DebugHUDOpacity" ) );
        if ( ImGui::Button( SW_LOCKEY( "EdText", "OpenInGameSettingsWindow" ) ) && pHUD->openWindow() == false )
            SW_LOG_WARNING_ID( "EditorDebugHUD.DebugHUDSettingsWindow", "The Debug HUD settings window could not be opened - the reason is logged above" );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "DebugHUDOpenWindow" ) );
    }

    void EditorDebugHUDControls::drawPreferencesPage()
    {
        DebugHUD* pHUD = findEditorDebugHUDControlsTarget();
        if ( pHUD == nullptr )
        {
            EditorWidgets::drawEmptyHint( SW_LOCKEY( "EdCommon", "DebugHUDNotRunningHost" ) );
            return;
        }
        ImGui::TextDisabled( "%s", SW_LOCKEY( "EdCommon", "DebugHUDSavedIn" ) );
        bool bShown = pHUD->isShown();
        if ( ImGui::Checkbox( SW_LOCKEY( "EdLabel", "ShowDebugHUDCtrlF3" ), &bShown ) )
            pHUD->setShown( bShown );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "DebugHUDShow" ) );
        EditorSelfTestMarks::record( "preferences.debugHUD.shown" );
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Sections" ) );
        drawSectionChecklist();
        ImGui::SeparatorText( SW_LOCKEY( "EdSection", "Layout" ) );
        drawLayoutSettings();
    }

    void EditorDebugHUDControls::drawSectionPopupOnRightClick( const utf8* pPopupID )
    {
        if ( EditorInputGUI::beginItemContextMenu( pPopupID ) )
        {
            ImGui::SeparatorText( SW_LOCKEY( "EdSection", "DebugHUDSections" ) );
            drawSectionChecklist();
            ImGui::Separator();
            drawLayoutSettings();
            ImGui::EndPopup();
        }
    }
} // namespace sw::editor
