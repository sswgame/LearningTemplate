#include "pch.h"

#include "Editor/Common/Widgets/EditorNetModeCombo.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySettings.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Reflection/EnumName.h"

#include <imgui.h>

namespace sw::editor
{
    bool EditorNetModeCombo::draw( EditorNetPlaySettings& inoutSettings, bool bLocked, const utf8* pMark )
    {
        EditorNetPlaySettings& settings = inoutSettings;
        bool                   bChanged = false;
        ImGui::BeginDisabled( bLocked );
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "FieldModeEm" ) );
        if ( EditorWidgets::beginCombo( "##NetMode", LocalizedText::findEnum( settings._mode ) ) )
        {
            constexpr EditorNetMode kArrMode[] = { EditorNetMode::Standalone, EditorNetMode::DedicatedServer, EditorNetMode::ListenHost };
            for ( const EditorNetMode mode : kArrMode )
            {
                if ( ImGui::Selectable( EditorThemeUtil::makeStableLabel( LocalizedText::findEnum( mode ), getEnumName( mode ) ), settings._mode == mode ) )
                {
                    settings._mode = mode;
                    bChanged       = true;
                }
            }
            ImGui::EndCombo();
        }
        EditorSelfTestMarks::record( pMark );
        EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "NetMode" ) );
        if ( settings._mode != EditorNetMode::Standalone )
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth( EditorChrome::measureStepperInput( 2.0f ) ); // -/+ 단추 몫까지(숫자 칸 폭만 주면 숫자가 눌린다)
            if ( EditorWidgets::inputInt( SW_WIDGET_ID( "##PlayerCount" ), &settings._clientCount, 1, 1 ) )
                bChanged = true;
            settings._clientCount = MathUtil::clamp( settings._clientCount, 1, kMaxPlayerCount );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "NetPlayerCount" ) );
        }
        ImGui::EndDisabled();
        return bChanged;
    }
} // namespace sw::editor
