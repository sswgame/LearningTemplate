#include "pch.h"

#include "Editor/Common/Widgets/EditorDebugHUDControls.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/UI/Debug/DebugHUD.h"

#include <imgui.h>

namespace sw::editor
{
    SW_LOG_CALLER( "EditorDebugHUD" );
    SW_EDITOR_SETTINGS_PAGE( DebugHUD, "debug_hud", "Editor/Debug HUD", 360, &EditorDebugHUDControls::drawPreferencesPage );

    void EditorDebugHUDControls::drawMasterToggle( const utf8* pMark )
    {
        DebugHUD*  pHUD   = DebugHUD::findActive();
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
        DebugHUD* pHUD = DebugHUD::findActive();
        if ( pHUD == nullptr )
        {
            ImGui::TextDisabled( "Debug HUD is not running" );
            return;
        }
        for ( const DebugHUDSectionRegistration* pSection : DebugHUDRegistry::get().getSections() )
        {
            bool bShown = DebugHUD::isSectionShown( *pSection );
            ImGui::PushID( pSection->_pName );
            if ( ImGui::Checkbox( pSection->_pTitle, &bShown ) && pHUD->setSectionShown( pSection->_pName, bShown ) == false )
                SW_LOG_WARNING( "Debug HUD section '%#' is not registered any more", pSection->_pName );
            if ( EditorSelfTestMarks::isEnabled() )
                EditorSelfTestMarks::note( ( string{ "debugHUD.section." } + pSection->_pName ).c_str() );
            ImGui::SameLine();
            ImGui::TextDisabled( "%s", pSection->_pName );
            ImGui::PopID();
        }
    }

    void EditorDebugHUDControls::drawLayoutSettings()
    {
        DebugHUD* pHUD = DebugHUD::findActive();
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
        float32 opacity = DebugHUD::getOpacity();
        if ( ImGui::SliderFloat( "Opacity", &opacity, DebugHUD::kMinOpacity, DebugHUD::kMaxOpacity, "%.2f" ) )
            pHUD->setOpacity( opacity );
        if ( ImGui::Button( "Open in-game settings window" ) && pHUD->openWindow() == false )
            SW_LOG_WARNING( "The Debug HUD settings window could not be opened (%#)", DebugHUD::kWindowDocumentPath );
        EditorWidgets::drawTooltip( "The same checklist as a runtime UI window (shows in the game view while playing)" );
    }

    void EditorDebugHUDControls::drawPreferencesPage()
    {
        DebugHUD* pHUD = DebugHUD::findActive();
        if ( pHUD == nullptr )
        {
            EditorWidgets::drawEmptyHint( "Debug HUD is not running (Shipping or a test host)." );
            return;
        }
        ImGui::TextDisabled( "Saved in the engine user settings (debug.hud, debug.hudSections, debug.hudCorner, debug.hudOpacity)" );
        bool bShown = pHUD->isShown();
        if ( ImGui::Checkbox( "Show Debug HUD (Ctrl+F3)", &bShown ) )
            pHUD->setShown( bShown );
        EditorSelfTestMarks::note( "preferences.debugHUD.shown" );
        ImGui::SeparatorText( "Sections" );
        drawSectionChecklist();
        ImGui::SeparatorText( "Layout" );
        drawLayoutSettings();
    }

    void EditorDebugHUDControls::drawSectionPopupOnRightClick( const utf8* pPopupID )
    {
        if ( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
            ImGui::OpenPopup( pPopupID );
        if ( ImGui::BeginPopup( pPopupID ) )
        {
            ImGui::SeparatorText( "Debug HUD sections" );
            drawSectionChecklist();
            ImGui::Separator();
            drawLayoutSettings();
            ImGui::EndPopup();
        }
    }
} // namespace sw::editor
