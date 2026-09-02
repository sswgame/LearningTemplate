#include "pch.h"

#include "Editor/Common/Gui/EditorThemeUtil.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorThemeInternal
        {
            inline static EditorThemeConfig s_activeTheme{};

            static ImVec4 toImVec4( const Color4& c, float32 alphaMultiplier = 1.0f )
            {
                return ImVec4( c._r, c._g, c._b, c._a * alphaMultiplier );
            }

            static ImVec4 adjustBrightness( const Color4& c, float32 factor, float32 alpha = 1.0f )
            {
                return ImVec4(
                    MathUtil::clamp( c._r * factor, 0.0f, 1.0f ),
                    MathUtil::clamp( c._g * factor, 0.0f, 1.0f ),
                    MathUtil::clamp( c._b * factor, 0.0f, 1.0f ),
                    alpha );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    const EditorThemeConfig& EditorThemeUtil::getActiveTheme()
    {
        return EditorThemeInternal::s_activeTheme;
    }

    void EditorThemeUtil::applyPreset( EditorThemePreset preset )
    {
        EditorThemeConfig config{};
        config._preset = preset;

        switch ( preset )
        {
            case EditorThemePreset::ModernDark:
                config._accentColor       = Color4{ 0.27f, 0.57f, 1.0f, 1.0f }; // Electric Blue
                config._windowBg          = Color4{ 0.10f, 0.11f, 0.14f, 1.0f };
                config._panelBg           = Color4{ 0.13f, 0.15f, 0.19f, 1.0f };
                config._headerBg          = Color4{ 0.18f, 0.21f, 0.28f, 1.0f };
                config._frameBg           = Color4{ 0.15f, 0.17f, 0.22f, 1.0f };
                config._border            = Color4{ 0.22f, 0.26f, 0.33f, 1.0f };
                config._successColor      = Color4{ 0.20f, 0.75f, 0.35f, 1.0f };
                config._warningColor      = Color4{ 0.95f, 0.70f, 0.15f, 1.0f };
                config._errorColor        = Color4{ 0.95f, 0.30f, 0.25f, 1.0f };
                config._infoColor         = Color4{ 0.30f, 0.70f, 0.95f, 1.0f };
                config._textMuted         = Color4{ 0.55f, 0.60f, 0.68f, 1.0f };
                config._windowRounding    = 4.0f;
                config._frameRounding     = 3.0f;
                config._popupRounding     = 4.0f;
                config._tabRounding       = 4.0f;
                config._scrollbarRounding = 6.0f;
                config._grabRounding      = 3.0f;
                break;

            case EditorThemePreset::DeepCharcoal:
                config._accentColor       = Color4{ 0.35f, 0.70f, 0.95f, 1.0f }; // Ice Blue
                config._windowBg          = Color4{ 0.08f, 0.08f, 0.09f, 1.0f };
                config._panelBg           = Color4{ 0.11f, 0.11f, 0.13f, 1.0f };
                config._headerBg          = Color4{ 0.16f, 0.16f, 0.19f, 1.0f };
                config._frameBg           = Color4{ 0.13f, 0.13f, 0.16f, 1.0f };
                config._border            = Color4{ 0.20f, 0.20f, 0.24f, 1.0f };
                config._successColor      = Color4{ 0.25f, 0.80f, 0.40f, 1.0f };
                config._warningColor      = Color4{ 0.90f, 0.75f, 0.20f, 1.0f };
                config._errorColor        = Color4{ 0.90f, 0.30f, 0.30f, 1.0f };
                config._infoColor         = Color4{ 0.35f, 0.75f, 0.90f, 1.0f };
                config._textMuted         = Color4{ 0.50f, 0.55f, 0.60f, 1.0f };
                config._windowRounding    = 2.0f;
                config._frameRounding     = 2.0f;
                config._popupRounding     = 2.0f;
                config._tabRounding       = 2.0f;
                config._scrollbarRounding = 4.0f;
                config._grabRounding      = 2.0f;
                break;

            case EditorThemePreset::MidnightBlue:
                config._accentColor       = Color4{ 0.40f, 0.55f, 1.0f, 1.0f }; // Neon Indigo
                config._windowBg          = Color4{ 0.07f, 0.09f, 0.14f, 1.0f };
                config._panelBg           = Color4{ 0.09f, 0.12f, 0.18f, 1.0f };
                config._headerBg          = Color4{ 0.14f, 0.19f, 0.28f, 1.0f };
                config._frameBg           = Color4{ 0.11f, 0.15f, 0.22f, 1.0f };
                config._border            = Color4{ 0.18f, 0.25f, 0.36f, 1.0f };
                config._successColor      = Color4{ 0.20f, 0.85f, 0.50f, 1.0f };
                config._warningColor      = Color4{ 1.0f, 0.75f, 0.20f, 1.0f };
                config._errorColor        = Color4{ 1.0f, 0.35f, 0.35f, 1.0f };
                config._infoColor         = Color4{ 0.40f, 0.75f, 1.0f, 1.0f };
                config._textMuted         = Color4{ 0.50f, 0.60f, 0.75f, 1.0f };
                config._windowRounding    = 4.0f;
                config._frameRounding     = 3.0f;
                config._popupRounding     = 4.0f;
                config._tabRounding       = 4.0f;
                config._scrollbarRounding = 6.0f;
                config._grabRounding      = 3.0f;
                break;

            case EditorThemePreset::ClassicDark:
                ImGui::StyleColorsDark();
                return;

            default:
                break;
        }

        applyTheme( config );
    }

    void EditorThemeUtil::applyTheme( const EditorThemeConfig& config )
    {
        EditorThemeInternal::s_activeTheme = config;

        ImGuiStyle& style = ImGui::GetStyle();

        // 1. 지오메트리 & 레이아웃 메트릭스
        style.WindowRounding    = config._windowRounding;
        style.ChildRounding     = config._frameRounding;
        style.FrameRounding     = config._frameRounding;
        style.PopupRounding     = config._popupRounding;
        style.ScrollbarRounding = config._scrollbarRounding;
        style.GrabRounding      = config._grabRounding;
        style.TabRounding       = config._tabRounding;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize  = 1.0f;
        style.PopupBorderSize  = 1.0f;
        style.FrameBorderSize  = 1.0f;
        style.TabBorderSize    = 0.0f;

        style.WindowPadding     = ImVec2( 8.0f, 8.0f );
        style.FramePadding      = ImVec2( 6.0f, 4.0f );
        style.ItemSpacing       = ImVec2( 6.0f, 4.0f );
        style.ItemInnerSpacing  = ImVec2( 4.0f, 4.0f );
        style.TouchExtraPadding = ImVec2( 0.0f, 0.0f );
        style.IndentSpacing     = 20.0f;
        style.ScrollbarSize     = 12.0f;
        style.GrabMinSize       = 8.0f;

        // 2. 통합 컬러 팔레트 구성
        ImVec4* pColors = style.Colors;

        const Color4& accent = config._accentColor;
        const Color4& winBg  = config._windowBg;
        const Color4& panBg  = config._panelBg;
        const Color4& hdrBg  = config._headerBg;
        const Color4& frmBg  = config._frameBg;
        const Color4& border = config._border;

        // 텍스트
        pColors[ImGuiCol_Text]         = ImVec4( 0.92f, 0.94f, 0.97f, 1.0f );
        pColors[ImGuiCol_TextDisabled] = EditorThemeInternal::toImVec4( config._textMuted, 1.0f );

        // 창 및 배경
        pColors[ImGuiCol_WindowBg]     = EditorThemeInternal::toImVec4( winBg, 1.0f );
        pColors[ImGuiCol_ChildBg]      = EditorThemeInternal::toImVec4( panBg, 1.0f );
        pColors[ImGuiCol_PopupBg]      = EditorThemeInternal::adjustBrightness( winBg, 1.15f, 0.98f );
        pColors[ImGuiCol_Border]       = EditorThemeInternal::toImVec4( border, 1.0f );
        pColors[ImGuiCol_BorderShadow] = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f );
        pColors[ImGuiCol_MenuBarBg]    = EditorThemeInternal::adjustBrightness( winBg, 0.85f, 1.0f );

        // 입력 프레임
        pColors[ImGuiCol_FrameBg]        = EditorThemeInternal::toImVec4( frmBg, 1.0f );
        pColors[ImGuiCol_FrameBgHovered] = EditorThemeInternal::adjustBrightness( frmBg, 1.25f, 1.0f );
        pColors[ImGuiCol_FrameBgActive]  = EditorThemeInternal::adjustBrightness( frmBg, 1.45f, 1.0f );

        // 타이틀바
        pColors[ImGuiCol_TitleBg]          = EditorThemeInternal::adjustBrightness( winBg, 0.9f, 1.0f );
        pColors[ImGuiCol_TitleBgActive]    = EditorThemeInternal::toImVec4( hdrBg, 1.0f );
        pColors[ImGuiCol_TitleBgCollapsed] = EditorThemeInternal::toImVec4( winBg, 0.75f );

        // 스크롤바
        pColors[ImGuiCol_ScrollbarBg]          = ImVec4( 0.02f, 0.02f, 0.03f, 0.35f );
        pColors[ImGuiCol_ScrollbarGrab]        = EditorThemeInternal::adjustBrightness( border, 1.1f, 0.8f );
        pColors[ImGuiCol_ScrollbarGrabHovered] = EditorThemeInternal::adjustBrightness( border, 1.4f, 1.0f );
        pColors[ImGuiCol_ScrollbarGrabActive]  = EditorThemeInternal::toImVec4( accent, 0.9f );

        // 버튼
        pColors[ImGuiCol_Button]        = EditorThemeInternal::adjustBrightness( frmBg, 1.1f, 1.0f );
        pColors[ImGuiCol_ButtonHovered] = EditorThemeInternal::toImVec4( accent, 0.85f );
        pColors[ImGuiCol_ButtonActive]  = EditorThemeInternal::adjustBrightness( accent, 1.15f, 1.0f );

        // 헤더
        pColors[ImGuiCol_Header]        = EditorThemeInternal::toImVec4( hdrBg, 1.0f );
        pColors[ImGuiCol_HeaderHovered] = EditorThemeInternal::toImVec4( accent, 0.75f );
        pColors[ImGuiCol_HeaderActive]  = EditorThemeInternal::toImVec4( accent, 0.95f );

        // 탭 & 도킹
        pColors[ImGuiCol_Tab]                = EditorThemeInternal::adjustBrightness( winBg, 0.95f, 1.0f );
        pColors[ImGuiCol_TabHovered]         = EditorThemeInternal::toImVec4( accent, 0.80f );
        pColors[ImGuiCol_TabActive]          = EditorThemeInternal::toImVec4( panBg, 1.0f );
        pColors[ImGuiCol_TabUnfocused]       = EditorThemeInternal::adjustBrightness( winBg, 0.80f, 1.0f );
        pColors[ImGuiCol_TabUnfocusedActive] = EditorThemeInternal::adjustBrightness( panBg, 0.90f, 1.0f );
        pColors[ImGuiCol_DockingPreview]     = EditorThemeInternal::toImVec4( accent, 0.45f );
        pColors[ImGuiCol_DockingEmptyBg]     = ImVec4( 0.05f, 0.05f, 0.07f, 1.0f );

        // 구분선 & 기타
        pColors[ImGuiCol_Separator]         = EditorThemeInternal::toImVec4( border, 0.85f );
        pColors[ImGuiCol_SeparatorHovered]  = EditorThemeInternal::toImVec4( accent, 0.85f );
        pColors[ImGuiCol_SeparatorActive]   = EditorThemeInternal::toImVec4( accent, 1.0f );
        pColors[ImGuiCol_ResizeGrip]        = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f );
        pColors[ImGuiCol_ResizeGripHovered] = EditorThemeInternal::toImVec4( accent, 0.60f );
        pColors[ImGuiCol_ResizeGripActive]  = EditorThemeInternal::toImVec4( accent, 0.90f );
        pColors[ImGuiCol_CheckMark]         = EditorThemeInternal::toImVec4( accent, 1.0f );
        pColors[ImGuiCol_SliderGrab]        = EditorThemeInternal::toImVec4( accent, 0.85f );
        pColors[ImGuiCol_SliderGrabActive]  = EditorThemeInternal::adjustBrightness( accent, 1.2f, 1.0f );
        pColors[ImGuiCol_TextSelectedBg]    = EditorThemeInternal::toImVec4( accent, 0.35f );
        pColors[ImGuiCol_NavHighlight]      = EditorThemeInternal::toImVec4( accent, 0.85f );

        // 테이블
        pColors[ImGuiCol_TableHeaderBg]     = EditorThemeInternal::toImVec4( hdrBg, 1.0f );
        pColors[ImGuiCol_TableBorderStrong] = EditorThemeInternal::toImVec4( border, 1.0f );
        pColors[ImGuiCol_TableBorderLight]  = EditorThemeInternal::toImVec4( border, 0.5f );
        pColors[ImGuiCol_TableRowBg]        = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f );
        pColors[ImGuiCol_TableRowBgAlt]     = ImVec4( 1.0f, 1.0f, 1.0f, 0.02f );
    }

    void EditorThemeUtil::loadFromConfig()
    {
        const EditorConfig& cfg = EditorConfig::getActive();

        EditorThemePreset preset = EditorThemePreset::ModernDark;
        if ( cfg._themePreset == "DeepCharcoal" )
            preset = EditorThemePreset::DeepCharcoal;
        else if ( cfg._themePreset == "MidnightBlue" )
            preset = EditorThemePreset::MidnightBlue;
        else if ( cfg._themePreset == "ClassicDark" )
            preset = EditorThemePreset::ClassicDark;

        applyPreset( preset );

        if ( preset != EditorThemePreset::ClassicDark )
        {
            EditorThemeConfig themeCfg = EditorThemeInternal::s_activeTheme;
            if ( cfg._themeAccentR > 0.0f || cfg._themeAccentG > 0.0f || cfg._themeAccentB > 0.0f )
                themeCfg._accentColor = Color4{ cfg._themeAccentR, cfg._themeAccentG, cfg._themeAccentB, 1.0f };

            if ( cfg._themeWindowRounding >= 0.0f )
            {
                themeCfg._windowRounding    = cfg._themeWindowRounding;
                themeCfg._frameRounding     = cfg._themeFrameRounding;
                themeCfg._tabRounding       = cfg._themeTabRounding;
                themeCfg._popupRounding     = cfg._themeWindowRounding;
                themeCfg._scrollbarRounding = cfg._themeFrameRounding * 2.0f;
                themeCfg._grabRounding      = cfg._themeFrameRounding;
            }

            applyTheme( themeCfg );
        }
    }

    void EditorThemeUtil::saveToConfig()
    {
        EditorConfig cfg = EditorConfig::getActive();

        const EditorThemeConfig& themeCfg = EditorThemeInternal::s_activeTheme;
        switch ( themeCfg._preset )
        {
            case EditorThemePreset::ModernDark:
                cfg._themePreset = "ModernDark";
                break;
            case EditorThemePreset::DeepCharcoal:
                cfg._themePreset = "DeepCharcoal";
                break;
            case EditorThemePreset::MidnightBlue:
                cfg._themePreset = "MidnightBlue";
                break;
            case EditorThemePreset::ClassicDark:
                cfg._themePreset = "ClassicDark";
                break;
            default:
                cfg._themePreset = "ModernDark";
                break;
        }

        cfg._themeAccentR        = themeCfg._accentColor._r;
        cfg._themeAccentG        = themeCfg._accentColor._g;
        cfg._themeAccentB        = themeCfg._accentColor._b;
        cfg._themeWindowRounding = themeCfg._windowRounding;
        cfg._themeFrameRounding  = themeCfg._frameRounding;
        cfg._themeTabRounding    = themeCfg._tabRounding;

        EditorConfig::setActive( cfg );
        EditorConfig::saveToHost();
    }

    void EditorThemeUtil::setAccentColor( const Color4& accentColor )
    {
        EditorThemeConfig cfg = EditorThemeInternal::s_activeTheme;
        cfg._accentColor      = accentColor;
        applyTheme( cfg );
        saveToConfig();
    }

    const Color4& EditorThemeUtil::getAccentColor()
    {
        return EditorThemeInternal::s_activeTheme._accentColor;
    }

    const Color4& EditorThemeUtil::getWindowBgColor()
    {
        return EditorThemeInternal::s_activeTheme._windowBg;
    }

    const Color4& EditorThemeUtil::getPanelBgColor()
    {
        return EditorThemeInternal::s_activeTheme._panelBg;
    }

    const Color4& EditorThemeUtil::getHeaderBgColor()
    {
        return EditorThemeInternal::s_activeTheme._headerBg;
    }

    const Color4& EditorThemeUtil::getFrameBgColor()
    {
        return EditorThemeInternal::s_activeTheme._frameBg;
    }

    const Color4& EditorThemeUtil::getBorderColor()
    {
        return EditorThemeInternal::s_activeTheme._border;
    }

    const Color4& EditorThemeUtil::getSuccessColor()
    {
        return EditorThemeInternal::s_activeTheme._successColor;
    }

    const Color4& EditorThemeUtil::getWarningColor()
    {
        return EditorThemeInternal::s_activeTheme._warningColor;
    }

    const Color4& EditorThemeUtil::getErrorColor()
    {
        return EditorThemeInternal::s_activeTheme._errorColor;
    }

    const Color4& EditorThemeUtil::getInfoColor()
    {
        return EditorThemeInternal::s_activeTheme._infoColor;
    }

    const Color4& EditorThemeUtil::getTextMutedColor()
    {
        return EditorThemeInternal::s_activeTheme._textMuted;
    }

    void EditorThemeUtil::textAccent( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( getAccentColor() ), "%s", pText );
    }

    void EditorThemeUtil::textSuccess( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( getSuccessColor() ), "%s", pText );
    }

    void EditorThemeUtil::textWarning( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( getWarningColor() ), "%s", pText );
    }

    void EditorThemeUtil::textError( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( getErrorColor() ), "%s", pText );
    }

    void EditorThemeUtil::textMuted( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( getTextMutedColor() ), "%s", pText );
    }

    void EditorThemeUtil::pushAccentButton( float32 alpha )
    {
        const Color4& accent = getAccentColor();
        ImGui::PushStyleColor( ImGuiCol_Button, EditorThemeInternal::toImVec4( accent, 0.70f * alpha ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, EditorThemeInternal::toImVec4( accent, 0.90f * alpha ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, EditorThemeInternal::toImVec4( accent, 1.0f * alpha ) );
    }

    void EditorThemeUtil::popAccentButton()
    {
        ImGui::PopStyleColor( 3 );
    }

    void EditorThemeUtil::pushAccentHeader( float32 alpha )
    {
        const Color4& accent = getAccentColor();
        ImGui::PushStyleColor( ImGuiCol_Header, EditorThemeInternal::toImVec4( accent, 0.70f * alpha ) );
        ImGui::PushStyleColor( ImGuiCol_HeaderHovered, EditorThemeInternal::toImVec4( accent, 0.85f * alpha ) );
        ImGui::PushStyleColor( ImGuiCol_HeaderActive, EditorThemeInternal::toImVec4( accent, 1.0f * alpha ) );
    }

    void EditorThemeUtil::popAccentHeader()
    {
        ImGui::PopStyleColor( 3 );
    }

    void EditorThemeUtil::drawThemeSettingsDialog( bool* pOpen )
    {
        if ( pOpen != nullptr && *pOpen == false )
            return;

        ImGui::SetNextWindowSize( ImVec2( 460.0f, 380.0f ), ImGuiCond_FirstUseEver );
        if ( ImGui::Begin( ICON_FA_PALETTE "  Theme & Look and Feel", pOpen, ImGuiWindowFlags_NoCollapse ) )
        {
            EditorThemeConfig cfg = EditorThemeInternal::s_activeTheme;

            // 1) 프리셋 선택
            const utf8* arrPresetNames[] = {
                "Modern Dark (UE5 / JetBrains)",
                "Deep Charcoal (Minimalist)",
                "Midnight Blue (High-Tech)",
                "Classic Dark (Default ImGui)" };

            int32 currentPreset = static_cast<int32>( cfg._preset );
            if ( ImGui::Combo( "Theme Preset", &currentPreset, arrPresetNames, IM_ARRAYSIZE( arrPresetNames ) ) )
            {
                applyPreset( static_cast<EditorThemePreset>( currentPreset ) );
                saveToConfig();
                cfg = EditorThemeInternal::s_activeTheme;
            }
            EditorWidgets::drawTooltip( "에디터 전체의 테마 프리셋(Modern Dark, Deep Charcoal, Midnight Blue 등)을 선택합니다" );

            ImGui::Separator();
            ImGui::TextDisabled( "Color Palette Customization" );

            // 2) 액센트 컬러 피커
            float32 arrAccentRaw[4] = { cfg._accentColor._r, cfg._accentColor._g, cfg._accentColor._b, cfg._accentColor._a };
            if ( ImGui::ColorEdit4( "Accent Color", arrAccentRaw, ImGuiColorEditFlags_NoAlpha ) )
            {
                cfg._accentColor = Color4{ arrAccentRaw[0], arrAccentRaw[1], arrAccentRaw[2], arrAccentRaw[3] };
                applyTheme( cfg );
                saveToConfig();
            }
            EditorWidgets::drawTooltip( "버튼, 선택 하이라이트, 폴더 및 활성 항목에 적용할 대표 액센트 색상" );

            // 3) 빠른 액센트 팔레트 스와치
            ImGui::Text( "Accent Swatches:" );
            ImGui::SameLine();
            const Color4 arrSwatches[] = {
                {0.27f, 0.57f,  1.0f, 1.0f}, // Electric Blue
                {0.15f, 0.75f, 0.55f, 1.0f}, // Emerald Cyan
                {0.65f, 0.35f, 0.95f, 1.0f}, // Neon Violet
                {0.95f, 0.55f, 0.15f, 1.0f}, // Amber Gold
                {0.90f, 0.25f, 0.35f, 1.0f}  // Crimson
            };

            for ( int32 index = 0; index < 5; ++index )
            {
                if ( index > 0 )
                    ImGui::SameLine();
                ImGui::PushID( index );
                if ( ImGui::ColorButton( "##swatch", EditorThemeInternal::toImVec4( arrSwatches[index] ), ImGuiColorEditFlags_NoAlpha, ImVec2( 22.0f, 22.0f ) ) )
                {
                    setAccentColor( arrSwatches[index] );
                    cfg = getActiveTheme();
                }
                EditorWidgets::drawTooltip( "추천 액센트 색상 팔레트" );
                ImGui::PopID();
            }

            ImGui::Separator();
            ImGui::TextDisabled( "Geometry & Rounding" );

            // 4) 라운딩 슬라이더
            bool bMetricsChanged = false;
            bMetricsChanged |= ImGui::SliderFloat( "Window Rounding", &cfg._windowRounding, 0.0f, 12.0f, "%.0f px" );
            EditorWidgets::drawTooltip( "에디터 창 및 팝업 대화상자 모서리의 둥글기(px)" );
            bMetricsChanged |= ImGui::SliderFloat( "Frame Rounding", &cfg._frameRounding, 0.0f, 8.0f, "%.0f px" );
            EditorWidgets::drawTooltip( "버튼, 입력 필드 및 컨트롤 프레임의 모서리 둥글기(px)" );
            bMetricsChanged |= ImGui::SliderFloat( "Tab Rounding", &cfg._tabRounding, 0.0f, 8.0f, "%.0f px" );
            EditorWidgets::drawTooltip( "도킹 탭 및 상단 패널 탭 모서리의 둥글기(px)" );

            if ( bMetricsChanged )
            {
                cfg._popupRounding     = cfg._windowRounding;
                cfg._scrollbarRounding = cfg._frameRounding * 2.0f;
                cfg._grabRounding      = cfg._frameRounding;
                applyTheme( cfg );
                saveToConfig();
            }

            ImGui::Separator();

            // 5) 기본값 복원 버튼
            if ( ImGui::Button( ICON_FA_ROTATE_LEFT "  Reset to Modern Dark" ) )
            {
                applyPreset( EditorThemePreset::ModernDark );
                saveToConfig();
            }
            EditorWidgets::drawTooltip( "모든 테마 설정을 Modern Dark 기본값으로 복원합니다" );
        }
        ImGui::End();
    }

    Color4 EditorThemeUtil::getFolderColor()
    {
        // 언리얼 엔진 5 / Rider 표준: 테마 액센트(Electric Blue)와 일치하는 세련된 블루/스카이 톤
        return getAccentColor();
    }

    const utf8* EditorThemeUtil::getFolderIcon( bool bOpened )
    {
        return bOpened ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;
    }

    const utf8* EditorThemeUtil::getAssetIconForPath( string_view path, bool bIsDirectory )
    {
        if ( bIsDirectory )
            return ICON_FA_FOLDER;

        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Scene, path ) )
            return ICON_FA_CLAPPERBOARD;
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Prefab, path ) )
            return ICON_FA_CUBES;
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Texture, path ) )
            return ICON_FA_IMAGE;
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Material, path ) )
            return ICON_FA_DROPLET;
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Shader, path ) )
            return ICON_FA_CODE;
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Audio, path ) )
            return ICON_FA_MUSIC;
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::AnimationGraph, path ) || EditorAssetTypeRegistry::matches( EditorAssetKind::SpriteClip, path ) )
            return ICON_FA_PERSON_RUNNING;
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::DialogueGraph, path ) )
            return ICON_FA_COMMENTS;
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::TileMap, path ) )
            return ICON_FA_BORDER_ALL;
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Data, path ) )
            return ICON_FA_TABLE;

        return ICON_FA_FILE;
    }

    Color4 EditorThemeUtil::getAssetColorForPath( string_view path, bool bIsDirectory )
    {
        if ( bIsDirectory )
            return getFolderColor();

        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Scene, path ) )
            return getAccentColor();
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Prefab, path ) )
            return Color4{ 0.35f, 0.70f, 1.0f, 1.0f };
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Texture, path ) )
            return Color4{ 0.35f, 0.85f, 0.45f, 1.0f };
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Material, path ) )
            return Color4{ 0.80f, 0.45f, 0.95f, 1.0f };
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Shader, path ) )
            return Color4{ 0.95f, 0.45f, 0.35f, 1.0f };
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Audio, path ) )
            return Color4{ 0.95f, 0.85f, 0.25f, 1.0f };
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::AnimationGraph, path ) || EditorAssetTypeRegistry::matches( EditorAssetKind::SpriteClip, path ) )
            return Color4{ 1.0f, 0.60f, 0.20f, 1.0f };
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::DialogueGraph, path ) )
            return Color4{ 0.40f, 0.75f, 1.0f, 1.0f };
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::TileMap, path ) )
            return Color4{ 0.45f, 0.85f, 0.50f, 1.0f };
        if ( EditorAssetTypeRegistry::matches( EditorAssetKind::Data, path ) )
            return Color4{ 0.60f, 0.75f, 0.95f, 1.0f };

        return getTextMutedColor();
    }
} // namespace sw::editor
