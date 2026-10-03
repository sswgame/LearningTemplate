#include "pch.h"

#include "Editor/Common/Gui/EditorThemeUtil.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorThemeInternal
        {
            /**
             * @brief 컨텍스트가 없을 때 반환할 기본 테마입니다. **상수이므로 상태가 아닙니다.**
             * @details 색 게터들이 `const Color4&` 를 반환하므로 임시 객체를 참조로 넘길 수 없습니다.
             */
            static const EditorThemeConfig& fallbackTheme()
            {
                static const EditorThemeConfig kFallback{};
                return kFallback;
            }

            /** @brief 지금 적용된 테마입니다. `EditorContext` 가 소유하고, 컨텍스트가 없으면 기본값을 봅니다. */
            static const EditorThemeConfig& activeTheme()
            {
                EditorContext* pContext = EditorContext::get();
                return pContext != nullptr ? pContext->getThemeConfig() : fallbackTheme();
            }

            /** @brief 테마를 바꿔 씁니다. 컨텍스트가 없으면 조용히 무시합니다(적용할 UI 가 없습니다). */
            static void setActiveTheme( const EditorThemeConfig& config )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    pContext->getThemeConfig() = config;
            }

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

            /**
             * @brief 프리셋 하나의 정의입니다. 이름과 팔레트가 **여기 한 줄에** 모여 있습니다.
             * @details 팔레트 적용 · 설정 읽기/쓰기의 이름 변환 · 대화 상자의 콤보가 모두 이 표를 봅니다. 프리셋을 하나 더하려면
             *          한 줄만 더합니다. 이름 배열을 따로 두고 열거형 순서로 인덱스를 삼으면 순서를 바꿀 때 콤보가 조용히 틀린 이름을 보여 줍니다.
             */
            struct ThemePresetRow
            {
                EditorThemePreset _preset;
                const utf8*       _pConfigId;    ///< EditorConfig 에 저장되는 이름
                const utf8*       _pDisplayName; ///< 콤보에 보이는 이름
                /** @brief true면 창 색을 우리 팔레트로 덮지 않고 ImGui 기본 다크를 씁니다. */
                bool              _bUseImGuiDarkColors;
                EditorThemeConfig _config;
            };

            static const ThemePresetRow* getPresetRows( uint32& outCount );

            /** @brief 해당 프리셋의 행입니다. 모르는 값이면 첫 행(기본)입니다. */
            static const ThemePresetRow& findRow( EditorThemePreset preset )
            {
                uint32                      rowCount{ 0 };
                const ThemePresetRow* const pRow = getPresetRows( rowCount );
                for ( uint32 index = 0; index < rowCount; ++index )
                {
                    if ( pRow[index]._preset == preset )
                        return pRow[index];
                }
                return pRow[0];
            }

            /** @brief 저장된 이름의 행입니다. 못 찾으면 첫 행(기본)입니다. */
            static const ThemePresetRow& findRowByConfigId( string_view configId )
            {
                uint32                      rowCount{ 0 };
                const ThemePresetRow* const pRow = getPresetRows( rowCount );
                for ( uint32 index = 0; index < rowCount; ++index )
                {
                    if ( configId == pRow[index]._pConfigId )
                        return pRow[index];
                }
                return pRow[0];
            }

            /** @brief DPI 배율입니다(`EditorThemeUtil::setDpiScale`). 테마는 96 DPI 기준 크기로 적고 적용할 때 이것을 곱한다. */
            static float32& dpiScale()
            {
                static float32 s_dpiScale{ 1.0f };
                return s_dpiScale;
            }

            /** @brief 현재 ImGui 스타일의 지오메트리를 config 에 되읽습니다. 스타일에는 DPI 배율이 곱해져 있으므로 나눠 96 DPI 기준으로 적는다. */
            static void readGeometryFromStyle( EditorThemeConfig& outConfig )
            {
                const ImGuiStyle& style      = ImGui::GetStyle();
                const float32     invScale   = 1.0f / dpiScale();
                outConfig._windowRounding    = style.WindowRounding * invScale;
                outConfig._frameRounding     = style.FrameRounding * invScale;
                outConfig._popupRounding     = style.PopupRounding * invScale;
                outConfig._tabRounding       = style.TabRounding * invScale;
                outConfig._scrollbarRounding = style.ScrollbarRounding * invScale;
                outConfig._grabRounding      = style.GrabRounding * invScale;
            }
        };

        const EditorThemeInternal::ThemePresetRow* EditorThemeInternal::getPresetRows( uint32& outCount )
        {
            static const ThemePresetRow arrRow[] = {
                {  EditorThemePreset::ModernDark,   "ModernDark", "Modern Dark (UE5 / JetBrains)", false,
                 EditorThemeConfig{ EditorThemePreset::ModernDark,
                 Color4{ 0.27f, 0.57f, 1.0f, 1.0f }, // Electric Blue
                 Color4{ 0.10f, 0.11f, 0.14f, 1.0f },
                 Color4{ 0.13f, 0.15f, 0.19f, 1.0f },
                 Color4{ 0.18f, 0.21f, 0.28f, 1.0f },
                 Color4{ 0.15f, 0.17f, 0.22f, 1.0f },
                 Color4{ 0.22f, 0.26f, 0.33f, 1.0f },
                 Color4{ 0.20f, 0.75f, 0.35f, 1.0f },
                 Color4{ 0.95f, 0.70f, 0.15f, 1.0f },
                 Color4{ 0.95f, 0.30f, 0.25f, 1.0f },
                 Color4{ 0.30f, 0.70f, 0.95f, 1.0f },
                 Color4{ 0.55f, 0.60f, 0.68f, 1.0f },
                 4.0f, 3.0f, 4.0f, 4.0f, 6.0f, 3.0f }},

                {EditorThemePreset::DeepCharcoal, "DeepCharcoal",    "Deep Charcoal (Minimalist)", false,
                 EditorThemeConfig{ EditorThemePreset::DeepCharcoal,
                 Color4{ 0.35f, 0.70f, 0.95f, 1.0f }, // Ice Blue
                 Color4{ 0.08f, 0.08f, 0.09f, 1.0f },
                 Color4{ 0.11f, 0.11f, 0.13f, 1.0f },
                 Color4{ 0.16f, 0.16f, 0.19f, 1.0f },
                 Color4{ 0.13f, 0.13f, 0.16f, 1.0f },
                 Color4{ 0.20f, 0.20f, 0.24f, 1.0f },
                 Color4{ 0.25f, 0.80f, 0.40f, 1.0f },
                 Color4{ 0.90f, 0.75f, 0.20f, 1.0f },
                 Color4{ 0.90f, 0.30f, 0.30f, 1.0f },
                 Color4{ 0.35f, 0.75f, 0.90f, 1.0f },
                 Color4{ 0.50f, 0.55f, 0.60f, 1.0f },
                 2.0f, 2.0f, 2.0f, 2.0f, 4.0f, 2.0f }},

                {EditorThemePreset::MidnightBlue, "MidnightBlue",     "Midnight Blue (High-Tech)", false,
                 EditorThemeConfig{ EditorThemePreset::MidnightBlue,
                 Color4{ 0.40f, 0.55f, 1.0f, 1.0f }, // Neon Indigo
                 Color4{ 0.07f, 0.09f, 0.14f, 1.0f },
                 Color4{ 0.09f, 0.12f, 0.18f, 1.0f },
                 Color4{ 0.14f, 0.19f, 0.28f, 1.0f },
                 Color4{ 0.11f, 0.15f, 0.22f, 1.0f },
                 Color4{ 0.18f, 0.25f, 0.36f, 1.0f },
                 Color4{ 0.20f, 0.85f, 0.50f, 1.0f },
                 Color4{ 1.0f, 0.75f, 0.20f, 1.0f },
                 Color4{ 1.0f, 0.35f, 0.35f, 1.0f },
                 Color4{ 0.40f, 0.75f, 1.0f, 1.0f },
                 Color4{ 0.50f, 0.60f, 0.75f, 1.0f },
                 4.0f, 3.0f, 4.0f, 4.0f, 6.0f, 3.0f }},

                // 창 색은 ImGui 기본 다크를 그대로 쓰므로 아래 배경색은 적용되지 않는다. 다만 상태색
                // (textSuccess/textWarning/...)과 액센트는 이 테마에서도 필요하므로 값을 채워 둔다.
                // 이 프리셋도 활성 테마를 갱신해야 한다 — 안 그러면 **이전 테마의 색**이 나온다.
                { EditorThemePreset::ClassicDark,  "ClassicDark",  "Classic Dark (Default ImGui)",  true,
                 EditorThemeConfig{ EditorThemePreset::ClassicDark,
                 Color4{ 0.26f, 0.59f, 0.98f, 1.0f }, // ImGui 기본 파랑
                 Color4{ 0.06f, 0.06f, 0.06f, 1.0f },
                 Color4{ 0.10f, 0.10f, 0.10f, 1.0f },
                 Color4{ 0.16f, 0.29f, 0.48f, 1.0f },
                 Color4{ 0.16f, 0.29f, 0.48f, 1.0f },
                 Color4{ 0.43f, 0.43f, 0.50f, 1.0f },
                 Color4{ 0.20f, 0.75f, 0.35f, 1.0f },
                 Color4{ 0.95f, 0.70f, 0.15f, 1.0f },
                 Color4{ 0.95f, 0.30f, 0.25f, 1.0f },
                 Color4{ 0.30f, 0.70f, 0.95f, 1.0f },
                 Color4{ 0.50f, 0.50f, 0.50f, 1.0f },
                 0.0f, 0.0f, 0.0f, 4.0f, 9.0f, 0.0f }}
            };

            static_assert( sizeof( arrRow ) / sizeof( arrRow[0] ) == static_cast<size_t>( EditorThemePreset::Count ),
                           "EditorThemePreset 과 프리셋 표의 개수가 다릅니다" );

            outCount = static_cast<uint32>( sizeof( arrRow ) / sizeof( arrRow[0] ) );
            return arrRow;
        }
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    const EditorThemeConfig& EditorThemeUtil::getActiveTheme()
    {
        return EditorThemeInternal::activeTheme();
    }

    void EditorThemeUtil::applyPreset( EditorThemePreset preset )
    {
        const EditorThemeInternal::ThemePresetRow& row = EditorThemeInternal::findRow( preset );

        EditorThemeConfig config = row._config;
        config._preset           = row._preset;

        if ( row._bUseImGuiDarkColors )
        {
            // 창 색은 ImGui 기본 다크를 그대로 쓴다 (지오메트리는 건드리지 않는다).
            ImGui::StyleColorsDark();

            // 주의: 여기서 그대로 return 하면 활성 테마가 **이전 프리셋에 머물러** (1) 이 선택이 저장되지 않고
            // (2) 콤보가 옛 프리셋을 선택된 것으로 보여 주고 (3) textWarning 등 상태색 API 가 옛 테마 색을 낸다.
            // 지오메트리는 현재 스타일에서 되읽어 기록이 화면과 어긋나지 않게 한다.
            EditorThemeInternal::readGeometryFromStyle( config );
            EditorThemeInternal::setActiveTheme( config );
            return;
        }

        applyTheme( config );
    }

    void EditorThemeUtil::applyTheme( const EditorThemeConfig& config )
    {
        EditorThemeInternal::setActiveTheme( config );

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
        // 위 크기는 96 DPI 기준이다. 모니터 배율을 곱한다(`setDpiScale`).
        if ( EditorThemeInternal::dpiScale() != 1.0f )
            style.ScaleAllSizes( EditorThemeInternal::dpiScale() );

        // 2. 통합 컬러 팔레트 구성
        ImVec4* pColors = style.Colors;

        const Color4& accent   = config._accentColor;
        const Color4& winBg    = config._windowBg;
        const Color4& panBg    = config._panelBg;
        const Color4& headerBg = config._headerBg;
        const Color4& frmBg    = config._frameBg;
        const Color4& border   = config._border;

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
        pColors[ImGuiCol_TitleBgActive]    = EditorThemeInternal::toImVec4( headerBg, 1.0f );
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
        pColors[ImGuiCol_Header]        = EditorThemeInternal::toImVec4( headerBg, 1.0f );
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
        pColors[ImGuiCol_TableHeaderBg]     = EditorThemeInternal::toImVec4( headerBg, 1.0f );
        pColors[ImGuiCol_TableBorderStrong] = EditorThemeInternal::toImVec4( border, 1.0f );
        pColors[ImGuiCol_TableBorderLight]  = EditorThemeInternal::toImVec4( border, 0.5f );
        pColors[ImGuiCol_TableRowBg]        = ImVec4( 0.0f, 0.0f, 0.0f, 0.0f );
        pColors[ImGuiCol_TableRowBgAlt]     = ImVec4( 1.0f, 1.0f, 1.0f, 0.02f );
    }

    void EditorThemeUtil::setDpiScale( float32 dpiScale )
    {
        const float32 scale = ( dpiScale > 0.0f ) ? dpiScale : 1.0f;
        if ( EditorThemeInternal::dpiScale() == scale )
            return;
        // 지금 테마를 96 DPI 기준으로 되읽고 새 배율로 다시 적용한다. ImGui 기본 다크 프리셋은 지오메트리를 건드리지 않으므로, 바뀐 비율만큼
        // 지금 스타일을 늘린다.
        const float32     previousScale = EditorThemeInternal::dpiScale();
        EditorThemeConfig config        = getActiveTheme();
        EditorThemeInternal::dpiScale() = scale;
        if ( EditorThemeInternal::findRow( config._preset )._bUseImGuiDarkColors )
            ImGui::GetStyle().ScaleAllSizes( scale / previousScale );
        else
            applyTheme( config );
        ImGui::GetStyle().FontScaleDpi = scale;
    }

    float32 EditorThemeUtil::getDpiScale()
    {
        return EditorThemeInternal::dpiScale();
    }

    void EditorThemeUtil::loadFromConfig()
    {
        const EditorConfig& editorConfig = EditorConfig::getActive();

        const EditorThemePreset preset = EditorThemeInternal::findRowByConfigId( editorConfig._themePreset )._preset;
        applyPreset( preset );

        if ( preset != EditorThemePreset::ClassicDark )
        {
            EditorThemeConfig themeConfig = EditorThemeInternal::activeTheme();
            if ( editorConfig._themeAccentR > 0.0f || editorConfig._themeAccentG > 0.0f || editorConfig._themeAccentB > 0.0f )
                themeConfig._accentColor = Color4{ editorConfig._themeAccentR, editorConfig._themeAccentG, editorConfig._themeAccentB, 1.0f };

            if ( editorConfig._themeWindowRounding >= 0.0f )
            {
                themeConfig._windowRounding    = editorConfig._themeWindowRounding;
                themeConfig._frameRounding     = editorConfig._themeFrameRounding;
                themeConfig._tabRounding       = editorConfig._themeTabRounding;
                themeConfig._popupRounding     = editorConfig._themeWindowRounding;
                themeConfig._scrollbarRounding = editorConfig._themeFrameRounding * 2.0f;
                themeConfig._grabRounding      = editorConfig._themeFrameRounding;
            }

            applyTheme( themeConfig );
        }
    }

    void EditorThemeUtil::saveToConfig()
    {
        EditorConfig editorConfig = EditorConfig::getActive();

        const EditorThemeConfig& themeConfig = EditorThemeInternal::activeTheme();
        editorConfig._themePreset            = EditorThemeInternal::findRow( themeConfig._preset )._pConfigId;

        editorConfig._themeAccentR        = themeConfig._accentColor._r;
        editorConfig._themeAccentG        = themeConfig._accentColor._g;
        editorConfig._themeAccentB        = themeConfig._accentColor._b;
        editorConfig._themeWindowRounding = themeConfig._windowRounding;
        editorConfig._themeFrameRounding  = themeConfig._frameRounding;
        editorConfig._themeTabRounding    = themeConfig._tabRounding;

        EditorConfig::setActive( editorConfig );
        EditorConfig::saveToHost();
    }

    void EditorThemeUtil::setAccentColor( const Color4& accentColor )
    {
        EditorThemeConfig editorConfig = EditorThemeInternal::activeTheme();
        editorConfig._accentColor      = accentColor;
        applyTheme( editorConfig );
        saveToConfig();
    }

    const Color4& EditorThemeUtil::getAccentColor()
    {
        return EditorThemeInternal::activeTheme()._accentColor;
    }

    const Color4& EditorThemeUtil::getWindowBgColor()
    {
        return EditorThemeInternal::activeTheme()._windowBg;
    }

    const Color4& EditorThemeUtil::getPanelBgColor()
    {
        return EditorThemeInternal::activeTheme()._panelBg;
    }

    const Color4& EditorThemeUtil::getHeaderBgColor()
    {
        return EditorThemeInternal::activeTheme()._headerBg;
    }

    const Color4& EditorThemeUtil::getFrameBgColor()
    {
        return EditorThemeInternal::activeTheme()._frameBg;
    }

    const Color4& EditorThemeUtil::getBorderColor()
    {
        return EditorThemeInternal::activeTheme()._border;
    }

    const Color4& EditorThemeUtil::getSuccessColor()
    {
        return EditorThemeInternal::activeTheme()._successColor;
    }

    const Color4& EditorThemeUtil::getWarningColor()
    {
        return EditorThemeInternal::activeTheme()._warningColor;
    }

    const Color4& EditorThemeUtil::getErrorColor()
    {
        return EditorThemeInternal::activeTheme()._errorColor;
    }

    const Color4& EditorThemeUtil::getInfoColor()
    {
        return EditorThemeInternal::activeTheme()._infoColor;
    }

    const Color4& EditorThemeUtil::getTextMutedColor()
    {
        return EditorThemeInternal::activeTheme()._textMuted;
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

    void EditorThemeUtil::textInfo( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( getInfoColor() ), "%s", pText );
    }

    void EditorThemeUtil::pushTextColor( const Color4& color )
    {
        ImGui::PushStyleColor( ImGuiCol_Text, EditorThemeInternal::toImVec4( color ) );
    }

    void EditorThemeUtil::popTextColor()
    {
        ImGui::PopStyleColor();
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
            EditorThemeConfig editorConfig = EditorThemeInternal::activeTheme();

            // 1) 프리셋 선택. 이름과 순서는 프리셋 표에서 온다(열거형 값을 인덱스로 삼지 않는다).
            uint32                                           rowCount{ 0 };
            const EditorThemeInternal::ThemePresetRow* const pRow = EditorThemeInternal::getPresetRows( rowCount );

            const utf8* arrPresetName[static_cast<size_t>( EditorThemePreset::Count )]{};
            int32       currentPreset{ 0 };
            for ( uint32 index = 0; index < rowCount; ++index )
            {
                arrPresetName[index] = pRow[index]._pDisplayName;
                if ( pRow[index]._preset == editorConfig._preset )
                    currentPreset = static_cast<int32>( index );
            }

            if ( ImGui::Combo( "Theme Preset", &currentPreset, arrPresetName, static_cast<int32>( rowCount ) ) )
            {
                const uint32 selected = static_cast<uint32>( currentPreset );
                if ( selected < rowCount )
                {
                    applyPreset( pRow[selected]._preset );
                    saveToConfig();
                    editorConfig = EditorThemeInternal::activeTheme();
                }
            }
            EditorWidgets::drawTooltip( "에디터 전체의 테마 프리셋(Modern Dark, Deep Charcoal, Midnight Blue 등)을 선택합니다" );

            ImGui::Separator();
            ImGui::TextDisabled( "Color Palette Customization" );

            // 2) 액센트 컬러 피커
            float32 arrAccentRaw[4] = { editorConfig._accentColor._r, editorConfig._accentColor._g, editorConfig._accentColor._b, editorConfig._accentColor._a };
            if ( ImGui::ColorEdit4( "Accent Color", arrAccentRaw, ImGuiColorEditFlags_NoAlpha ) )
            {
                editorConfig._accentColor = Color4{ arrAccentRaw[0], arrAccentRaw[1], arrAccentRaw[2], arrAccentRaw[3] };
                applyTheme( editorConfig );
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
                    editorConfig = getActiveTheme();
                }
                EditorWidgets::drawTooltip( "추천 액센트 색상 팔레트" );
                ImGui::PopID();
            }

            ImGui::Separator();
            ImGui::TextDisabled( "Geometry & Rounding" );

            // 4) 라운딩 슬라이더
            bool bMetricsChanged = false;
            bMetricsChanged |= ImGui::SliderFloat( "Window Rounding", &editorConfig._windowRounding, 0.0f, 12.0f, "%.0f px" );
            EditorWidgets::drawTooltip( "에디터 창 및 팝업 대화상자 모서리의 둥글기(px)" );
            bMetricsChanged |= ImGui::SliderFloat( "Frame Rounding", &editorConfig._frameRounding, 0.0f, 8.0f, "%.0f px" );
            EditorWidgets::drawTooltip( "버튼, 입력 필드 및 컨트롤 프레임의 모서리 둥글기(px)" );
            bMetricsChanged |= ImGui::SliderFloat( "Tab Rounding", &editorConfig._tabRounding, 0.0f, 8.0f, "%.0f px" );
            EditorWidgets::drawTooltip( "도킹 탭 및 상단 패널 탭 모서리의 둥글기(px)" );

            if ( bMetricsChanged )
            {
                editorConfig._popupRounding     = editorConfig._windowRounding;
                editorConfig._scrollbarRounding = editorConfig._frameRounding * 2.0f;
                editorConfig._grabRounding      = editorConfig._frameRounding;
                applyTheme( editorConfig );
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
        // 폴더 색은 테마 액센트 색을 그대로 쓴다(UE5 · Rider 처럼 액센트와 같은 파란 계열)
        return getAccentColor();
    }

    const utf8* EditorThemeUtil::getFolderIcon( bool bOpened )
    {
        return bOpened ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;
    }

    const utf8* EditorThemeUtil::getAssetKindIcon( EditorAssetKind kind )
    {
        const EditorAssetKindInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
        return pInfo != nullptr ? pInfo->_pIcon : ICON_FA_FILE;
    }

    Color4 EditorThemeUtil::getAssetKindColor( EditorAssetKind kind )
    {
        const EditorAssetKindInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
        if ( pInfo == nullptr )
            return getTextMutedColor();
        return pInfo->_bAccentColor ? getAccentColor() : pInfo->_color;
    }

    const utf8* EditorThemeUtil::getAssetIconForPath( string_view path, bool bIsDirectory )
    {
        if ( bIsDirectory )
            return ICON_FA_FOLDER;
        return getAssetKindIcon( EditorAssetTypeRegistry::findKind( path ) );
    }

    Color4 EditorThemeUtil::getAssetColorForPath( string_view path, bool bIsDirectory )
    {
        if ( bIsDirectory )
            return getFolderColor();
        return getAssetKindColor( EditorAssetTypeRegistry::findKind( path ) );
    }
} // namespace sw::editor
