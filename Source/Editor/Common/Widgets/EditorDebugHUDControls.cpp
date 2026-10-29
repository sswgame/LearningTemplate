#include "pch.h"

#include "Editor/Common/Widgets/EditorDebugHUDControls.h"

#include "Core/Log/Logger.h"

#include "DevTools/DebugHUD/DebugHUD.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "EditorDebugHUD" );

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
        if ( EditorWidgets::drawIconToggle( "##debugHUD", editoricon::kChart, bShown,
                                            "Debug HUD in the game view (Ctrl+F3) - right click for sections. It shows while playing" ) &&
             pHUD != nullptr )
            pHUD->setShown( bShown == false );
        ImGui::EndDisabled();
        EditorSelfTestMarks::note( pMark );
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
        for ( const DebugHUDSectionRegistration* pSection : DebugHUDRegistry::get().getSections() )
        {
            bool bShown = DebugHUD::isSectionShown( *pSection );
            ImGui::PushID( pSection->_pName );
            if ( ImGui::Checkbox( pSection->_pTitle, &bShown ) && pHUD->setSectionShown( pSection->_pName, bShown ) == false )
                SW_LOG_WARNING( "Debug HUD section '%#' is not registered any more", pSection->_pName );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "DebugHUDSection" ) );
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::note( ( string{ "debugHUD.section." } + pSection->_pName ).c_str() );
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
        if ( ImGui::BeginCombo( "Corner", DebugHUD::getCornerName( DebugHUD::getCorner() ) ) )
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
        if ( ImGui::SliderFloat( "Opacity", &opacity, DebugHUD::kMinOpacity, DebugHUD::kMaxOpacity, "%.2f" ) )
            pHUD->setOpacity( opacity );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EditorTip", "DebugHUDOpacity" ) );
        if ( ImGui::Button( "Open in-game settings window" ) && pHUD->openWindow() == false )
            SW_LOG_WARNING( "The Debug HUD settings window could not be opened - the reason is logged above" );
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
        if ( ImGui::Checkbox( "Show Debug HUD (Ctrl+F3)", &bShown ) )
            pHUD->setShown( bShown );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "DebugHUDShow" ) );
        EditorSelfTestMarks::note( "preferences.debugHUD.shown" );
        ImGui::SeparatorText( "Sections" );
        drawSectionChecklist();
        ImGui::SeparatorText( "Layout" );
        drawLayoutSettings();
    }

    void EditorDebugHUDControls::drawSectionPopupOnRightClick( const utf8* pPopupID )
    {
        if ( EditorInputGUI::beginItemContextMenu( pPopupID ) )
        {
            ImGui::SeparatorText( "Debug HUD sections" );
            drawSectionChecklist();
            ImGui::Separator();
            drawLayoutSettings();
            ImGui::EndPopup();
        }
    }
} // namespace sw::editor
