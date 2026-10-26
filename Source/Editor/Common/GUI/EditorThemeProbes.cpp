/**
 * @file EditorThemeProbes.cpp
 * @brief 에디터 테마의 자동화 탐침입니다 — `Editor.ThemeName` · `Editor.AccentColor` · `Editor.UIScale`.
 * @details 탐침 레지스트리(`Engine/Probe`)가 기능 층보다 아래라 기능이 자기 탐침을 등록합니다. 등록은 이 .cpp 의 정적 등록자가 하며 대상 id 를 정하는 시나리오용 전역 변수는
 *          `EditorProbeSupport.h` 에 있습니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorThemeUtil.h"

#include "Engine/Probe/AutomationProbe.h"

namespace sw::editor
{
    namespace
    {
        [[nodiscard]] bool readThemeName( const AutomationProbeQuery& /*query*/, AutomationProbeValue& outValue )
        {
            outValue.setText( EditorThemeUtil::getActiveTheme()._name );
            return true;
        }

        /** @brief 액센트 색을 0xRRGGBB 정수로 반환합니다. 채널마다 0..255 로 반올림하므로 `equals` 로 정확히 비교할 수 있습니다. */
        [[nodiscard]] bool readAccentColor( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            const Color& accent = EditorThemeUtil::getAccentColor();
            const uint32 red    = static_cast<uint32>( accent._r * 255.0f + 0.5f );
            const uint32 green  = static_cast<uint32>( accent._g * 255.0f + 0.5f );
            const uint32 blue   = static_cast<uint32>( accent._b * 255.0f + 0.5f );
            outValue            = static_cast<float64>( ( red << 16 ) | ( green << 8 ) | blue );
            return true;
        }

        [[nodiscard]] bool readUIScale( const GameObjectManager* /*pManager*/, float64& outValue )
        {
            outValue = static_cast<float64>( EditorThemeUtil::getDpiScale() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_QUERY_PROBE( editorThemeName, "Editor.ThemeName", "Active theme token set name (dark, light, highcontrast, ...)", &readThemeName,
                               false, false );
    SW_AUTOMATION_PROBE( editorAccentColor, "Editor.AccentColor", "Accent color as 0xRRGGBB", &readAccentColor );
    SW_AUTOMATION_PROBE( editorUIScale, "Editor.UIScale", "Editor UI scale (1 = 96 DPI)", &readUIScale );
} // namespace sw::editor
