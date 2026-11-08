#include "pch.h"

#include "Editor/Common/GUI/EditorCompactControls.h"

#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include <imgui.h>

namespace sw::editor
{
    bool EditorCompactControls::drawIconButton( const utf8* pID, const utf8* pIcon, const utf8* pTooltip, const utf8* pDisabledReason )
    {
        const bool bDisabled = pDisabledReason != nullptr;
        ImGui::BeginDisabled( bDisabled );
        const bool bPressed = EditorWidgets::drawIconToggle( pID, pIcon, false, bDisabled ? nullptr : pTooltip );
        ImGui::EndDisabled();
        // 막힌 단추는 보통 툴팁이 뜨지 않는다 — 막힌 이유는 막혔을 때 가장 필요한 글이라 따로 띄운다.
        if ( bDisabled && StringUtil::isNullOrEmpty( pDisabledReason ) == false && ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort ) )
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "TooltipWrapEm" ) );
            ImGui::TextUnformatted( pDisabledReason );
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        return bPressed && bDisabled == false;
    }

    bool EditorCompactControls::beginViewOptions( const utf8* pID, bool bFiltered, const utf8* pTooltip, const utf8* pMark )
    {
        if ( EditorWidgets::drawIconToggle( pID, SW_EDITOR_ICON( "Filter" ), bFiltered, pTooltip ) )
            EditorInputGUI::openPopup( pID );
        if ( pMark != nullptr )
            EditorSelfTestMarks::record( pMark );
        return ImGui::BeginPopup( pID );
    }

    void EditorCompactControls::endViewOptions()
    {
        ImGui::EndPopup();
    }

    bool EditorCompactControls::drawResetButton( const utf8* pID, const utf8* pTooltip )
    {
        const utf8* const pIcon = SW_EDITOR_ICON( "Refresh" );
        EditorChrome::sameLineIfFits( EditorChrome::measureSmallButton( pIcon ) );
        ImGui::PushID( pID );
        const bool bPressed = ImGui::SmallButton( pIcon );
        ImGui::PopID();
        EditorWidgets::drawTooltip( pTooltip );
        return bPressed;
    }
} // namespace sw::editor
