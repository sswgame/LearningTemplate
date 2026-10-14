#include "pch.h"

#include "Editor/Common/GUI/EditorThemeUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorColorUtil.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorThemeInternal
        {
            /** @brief 환경설정 섹션 Appearance 의 인스턴스입니다. 활성 EditorConfig 에 속합니다. */
            static void* getAppearanceInstance() { return EditorConfig::getActiveInstance(); }
            /** @brief 환경설정 섹션 Appearance 의 기본값입니다. */
            static const void* getAppearanceDefault()
            {
                static const EditorConfig s_default{};
                return &s_default;
            }

            /**
             * @brief 컨텍스트가 없을 때 반환할 기본 테마입니다. 상수이므로 상태가 아닙니다.
             * @details 색 게터가 `const Color&` 를 반환하므로 임시 객체를 참조로 넘길 수 없다.
             */
            static const EditorThemeConfig& fallbackTheme()
            {
                static const EditorThemeConfig kFallback{};
                return kFallback;
            }

            /** @brief 현재 적용된 테마를 반환합니다. 에디터 서브시스템 `EditorTheme` 이 들며, 에디터 범위가 없으면 기본값을 봅니다. */
            static const EditorThemeConfig& activeTheme()
            {
                const EditorTheme* pTheme = findSubsystem<EditorTheme>();
                return pTheme != nullptr ? pTheme->getConfig() : fallbackTheme();
            }

            /** @brief 테마를 교체합니다. 에디터 범위가 없으면 적용할 UI 가 없으므로 조용히 무시합니다. */
            static void setActiveTheme( const EditorThemeConfig& config )
            {
                if ( EditorTheme* pTheme = findSubsystem<EditorTheme>() )
                    pTheme->getConfig() = config;
            }

            /** @brief 테마 고정 색입니다. 팔레트에서 오지 않는 항목입니다. */
            static constexpr Color kText{ 0.92f, 0.94f, 0.97f, 1.0f };
            static constexpr Color kScrollbarBg{ 0.02f, 0.02f, 0.03f, 0.35f };
            static constexpr Color kDockingEmptyBg{ 0.05f, 0.05f, 0.07f, 1.0f };
            static constexpr Color kTableRowBgAlt{ 1.0f, 1.0f, 1.0f, 0.02f };
            static constexpr Color kClear{ 0.0f, 0.0f, 0.0f, 0.0f };
            /** @brief 뷰포트 호버 색이 강조색에서 흰색 쪽으로 가는 비율과 알파입니다. */
            static constexpr float32 kHoverWhiten = 0.45f;
            static constexpr float32 kHoverAlpha  = 0.75f;

            static ImVec4 toImVec4( const Color& color, float32 alphaMultiplier = 1.0f )
            {
                return EditorColorUtil::toImVec4( color.scaleAlpha( alphaMultiplier ) );
            }

            static ImVec4 adjustBrightness( const Color& color, float32 factor, float32 alpha = 1.0f )
            {
                return EditorColorUtil::toImVec4( color.scaleBrightness( factor ).saturate().withAlpha( alpha ) );
            }

            /**
             * @brief 프리셋 하나의 정의입니다. 이름과 팔레트가 한 항목에 모여 있습니다.
             * @details 팔레트 적용, 설정 읽기와 쓰기의 이름 변환, 대화상자의 콤보가 모두 이 테이블을 본다. 프리셋을 추가하려면
             *          항목 하나만 추가한다. 이름 배열을 따로 두고 열거형 순서로 인덱스를 삼으면 순서를 바꿀 때 콤보가 조용히 틀린 이름을 보여 준다.
             */
            struct ThemePresetRow
            {
                EditorThemePreset _preset;
                const utf8*       _pConfigID;    ///< EditorConfig 에 저장되는 이름
                const utf8*       _pDisplayName; ///< 콤보에 보이는 이름
                /** @brief true 이면 창 색을 엔진 팔레트로 덮지 않고 ImGui 기본 다크를 씁니다. */
                bool              _bUseImGuiDarkColors;
                EditorThemeConfig _config;
            };

            static const ThemePresetRow* getPresetRows( uint32& outCount );

            /** @brief 해당 프리셋의 항목을 반환합니다. 모르는 값이면 첫 항목(기본)입니다. */
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

            /** @brief 저장된 이름에 해당하는 항목을 반환합니다. 찾지 못하면 첫 항목(기본)입니다. */
            static const ThemePresetRow& findRowByConfigID( string_view configID )
            {
                uint32                      rowCount{ 0 };
                const ThemePresetRow* const pRow = getPresetRows( rowCount );
                for ( uint32 index = 0; index < rowCount; ++index )
                {
                    if ( configID == pRow[index]._pConfigID )
                        return pRow[index];
                }
                return pRow[0];
            }

            /** @brief DPI 배율입니다(`EditorThemeUtil::setDpiScale`). 테마는 96 DPI 기준 크기로 기록하고, 적용할 때 이 값을 곱합니다. */
            static float32& dpiScale()
            {
                static float32 s_dpiScale{ 1.0f };
                return s_dpiScale;
            }

            /** @brief 현재 ImGui 스타일의 지오메트리를 config 에 다시 읽어 옵니다. 스타일에는 DPI 배율이 곱해져 있으므로 나눠서 96 DPI 기준으로 기록합니다. */
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

            /**
             * @brief 스타일의 크기 항목을 ImGui 기본(96 DPI)으로 되돌립니다. 색과 글꼴 크기, 배율은 그대로 둡니다.
             * @details `ScaleAllSizes` 는 테마가 기록하지 않는 크기(도킹 구분선, 창 테두리 호버 여백, 창 최소 크기 등)까지 곱한다. 기본값에서 다시
             *          시작하지 않으면 테마를 적용할 때마다 배율이 거듭 곱해져, 150 % 모니터에서 몇 번만 반복해도 구분선의 호버 여백이 옆 패널의 클릭을 가로챈다.
             */
            static void resetSizesToDefault( ImGuiStyle& outStyle )
            {
                ImGuiStyle fresh;
                for ( int32 colorIndex = 0; colorIndex < ImGuiCol_COUNT; ++colorIndex )
                {
                    fresh.Colors[colorIndex] = outStyle.Colors[colorIndex];
                }
                fresh.FontSizeBase           = outStyle.FontSizeBase;
                fresh.FontScaleMain          = outStyle.FontScaleMain;
                fresh.FontScaleDpi           = outStyle.FontScaleDpi;
                fresh._NextFrameFontSizeBase = outStyle._NextFrameFontSizeBase;
                outStyle                     = fresh;
            }
        };

        const EditorThemeInternal::ThemePresetRow* EditorThemeInternal::getPresetRows( uint32& outCount )
        {
            static const ThemePresetRow arrRow[] = {
                {  EditorThemePreset::ModernDark,   "ModernDark", "Modern Dark (UE5 / JetBrains)", false,
                 EditorThemeConfig{ EditorThemePreset::ModernDark,
                 Color{ 0.27f, 0.57f, 1.0f, 1.0f }, // Electric Blue
                 Color{ 0.10f, 0.11f, 0.14f, 1.0f },
                 Color{ 0.13f, 0.15f, 0.19f, 1.0f },
                 Color{ 0.18f, 0.21f, 0.28f, 1.0f },
                 Color{ 0.15f, 0.17f, 0.22f, 1.0f },
                 Color{ 0.22f, 0.26f, 0.33f, 1.0f },
                 Color{ 0.20f, 0.75f, 0.35f, 1.0f },
                 Color{ 0.95f, 0.70f, 0.15f, 1.0f },
                 Color{ 0.95f, 0.30f, 0.25f, 1.0f },
                 Color{ 0.30f, 0.70f, 0.95f, 1.0f },
                 Color{ 0.55f, 0.60f, 0.68f, 1.0f },
                 4.0f, 3.0f, 4.0f, 4.0f, 6.0f, 3.0f }},

                {EditorThemePreset::DeepCharcoal, "DeepCharcoal",    "Deep Charcoal (Minimalist)", false,
                 EditorThemeConfig{ EditorThemePreset::DeepCharcoal,
                 Color{ 0.35f, 0.70f, 0.95f, 1.0f }, // Ice Blue
                 Color{ 0.08f, 0.08f, 0.09f, 1.0f },
                 Color{ 0.11f, 0.11f, 0.13f, 1.0f },
                 Color{ 0.16f, 0.16f, 0.19f, 1.0f },
                 Color{ 0.13f, 0.13f, 0.16f, 1.0f },
                 Color{ 0.20f, 0.20f, 0.24f, 1.0f },
                 Color{ 0.25f, 0.80f, 0.40f, 1.0f },
                 Color{ 0.90f, 0.75f, 0.20f, 1.0f },
                 Color{ 0.90f, 0.30f, 0.30f, 1.0f },
                 Color{ 0.35f, 0.75f, 0.90f, 1.0f },
                 Color{ 0.50f, 0.55f, 0.60f, 1.0f },
                 2.0f, 2.0f, 2.0f, 2.0f, 4.0f, 2.0f }},

                {EditorThemePreset::MidnightBlue, "MidnightBlue",     "Midnight Blue (High-Tech)", false,
                 EditorThemeConfig{ EditorThemePreset::MidnightBlue,
                 Color{ 0.40f, 0.55f, 1.0f, 1.0f }, // Neon Indigo
                 Color{ 0.07f, 0.09f, 0.14f, 1.0f },
                 Color{ 0.09f, 0.12f, 0.18f, 1.0f },
                 Color{ 0.14f, 0.19f, 0.28f, 1.0f },
                 Color{ 0.11f, 0.15f, 0.22f, 1.0f },
                 Color{ 0.18f, 0.25f, 0.36f, 1.0f },
                 Color{ 0.20f, 0.85f, 0.50f, 1.0f },
                 Color{ 1.0f, 0.75f, 0.20f, 1.0f },
                 Color{ 1.0f, 0.35f, 0.35f, 1.0f },
                 Color{ 0.40f, 0.75f, 1.0f, 1.0f },
                 Color{ 0.50f, 0.60f, 0.75f, 1.0f },
                 4.0f, 3.0f, 4.0f, 4.0f, 6.0f, 3.0f }},

                // 창 색은 ImGui 기본 다크를 그대로 쓰므로 아래 배경색은 적용되지 않는다. 다만 상태색
                // (textSuccess, textWarning 등)과 액센트는 이 테마에서도 필요하므로 값을 채워 둔다.
                // 이 프리셋도 활성 테마를 갱신해야 한다. 갱신하지 않으면 이전 테마의 색이 나온다.
                { EditorThemePreset::ClassicDark,  "ClassicDark",  "Classic Dark (Default ImGui)",  true,
                 EditorThemeConfig{ EditorThemePreset::ClassicDark,
                 Color{ 0.26f, 0.59f, 0.98f, 1.0f }, // ImGui 기본 파랑
                 Color{ 0.06f, 0.06f, 0.06f, 1.0f },
                 Color{ 0.10f, 0.10f, 0.10f, 1.0f },
                 Color{ 0.16f, 0.29f, 0.48f, 1.0f },
                 Color{ 0.16f, 0.29f, 0.48f, 1.0f },
                 Color{ 0.43f, 0.43f, 0.50f, 1.0f },
                 Color{ 0.20f, 0.75f, 0.35f, 1.0f },
                 Color{ 0.95f, 0.70f, 0.15f, 1.0f },
                 Color{ 0.95f, 0.30f, 0.25f, 1.0f },
                 Color{ 0.30f, 0.70f, 0.95f, 1.0f },
                 Color{ 0.50f, 0.50f, 0.50f, 1.0f },
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

    bool EditorThemeUtil::findPresetByConfigID( string_view configID, EditorThemePreset& outPreset )
    {
        uint32                                           rowCount{ 0 };
        const EditorThemeInternal::ThemePresetRow* const pRow = EditorThemeInternal::getPresetRows( rowCount );
        for ( uint32 index = 0; index < rowCount; ++index )
        {
            if ( StringUtil::equals( configID, pRow[index]._pConfigID, true ) )
            {
                outPreset = pRow[index]._preset;
                return true;
            }
        }
        return false;
    }

    void EditorThemeUtil::applyPreset( EditorThemePreset preset )
    {
        const EditorThemeInternal::ThemePresetRow& row = EditorThemeInternal::findRow( preset );

        EditorThemeConfig config = row._config;
        config._preset           = row._preset;

        if ( row._bUseImGuiDarkColors )
        {
            // 창 색은 ImGui 기본 다크를 그대로 쓴다. 지오메트리는 건드리지 않는다.
            ImGui::StyleColorsDark();

            // 여기서 그대로 return 하면 활성 테마가 이전 프리셋에 머물러서 세 가지 문제가 생긴다.
            // 이 선택이 저장되지 않고, 콤보가 이전 프리셋을 선택된 것으로 보여 주고, textWarning 등 상태색 API 가 이전 테마 색을 낸다.
            // 지오메트리는 현재 스타일에서 다시 읽어 기록이 화면과 어긋나지 않게 한다.
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
        EditorThemeInternal::resetSizesToDefault( style );

        // 1. 지오메트리와 레이아웃 메트릭
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

        const Color& accent   = config._accentColor;
        const Color& winBg    = config._windowBg;
        const Color& panBg    = config._panelBg;
        const Color& headerBg = config._headerBg;
        const Color& frmBg    = config._frameBg;
        const Color& border   = config._border;

        // 텍스트
        pColors[ImGuiCol_Text]         = EditorColorUtil::toImVec4( EditorThemeInternal::kText );
        pColors[ImGuiCol_TextDisabled] = EditorThemeInternal::toImVec4( config._textMuted, 1.0f );

        // 창 및 배경
        pColors[ImGuiCol_WindowBg]     = EditorThemeInternal::toImVec4( winBg, 1.0f );
        pColors[ImGuiCol_ChildBg]      = EditorThemeInternal::toImVec4( panBg, 1.0f );
        pColors[ImGuiCol_PopupBg]      = EditorThemeInternal::adjustBrightness( winBg, 1.15f, 0.98f );
        pColors[ImGuiCol_Border]       = EditorThemeInternal::toImVec4( border, 1.0f );
        pColors[ImGuiCol_BorderShadow] = EditorColorUtil::toImVec4( EditorThemeInternal::kClear );
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
        pColors[ImGuiCol_ScrollbarBg]          = EditorColorUtil::toImVec4( EditorThemeInternal::kScrollbarBg );
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
        pColors[ImGuiCol_DockingEmptyBg]     = EditorColorUtil::toImVec4( EditorThemeInternal::kDockingEmptyBg );

        // 구분선 & 기타
        pColors[ImGuiCol_Separator]         = EditorThemeInternal::toImVec4( border, 0.85f );
        pColors[ImGuiCol_SeparatorHovered]  = EditorThemeInternal::toImVec4( accent, 0.85f );
        pColors[ImGuiCol_SeparatorActive]   = EditorThemeInternal::toImVec4( accent, 1.0f );
        pColors[ImGuiCol_ResizeGrip]        = EditorColorUtil::toImVec4( EditorThemeInternal::kClear );
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
        pColors[ImGuiCol_TableRowBg]        = EditorColorUtil::toImVec4( EditorThemeInternal::kClear );
        pColors[ImGuiCol_TableRowBgAlt]     = EditorColorUtil::toImVec4( EditorThemeInternal::kTableRowBgAlt );
    }

    void EditorThemeUtil::setDpiScale( float32 dpiScale )
    {
        const float32 scale = ( dpiScale > 0.0f ) ? dpiScale : 1.0f;
        if ( EditorThemeInternal::dpiScale() == scale )
            return;
        // 현재 테마를 96 DPI 기준으로 다시 읽고 새 배율로 다시 적용한다. ImGui 기본 다크 프리셋은 지오메트리를 건드리지 않으므로, 바뀐 비율만큼
        // 현재 스타일을 늘린다.
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
        // 환경설정 파일은 ImGui 컨텍스트보다 먼저 읽는다. 섹션이 바뀐 뒤의 동작이 여기로 들어온다. 테마는 컨텍스트를 만든 뒤 ImGuiEditor 가 다시 적용한다.
        if ( ImGui::GetCurrentContext() == nullptr )
            return;
        const EditorConfig& editorConfig = EditorConfig::getActive();

        const EditorThemePreset preset = EditorThemeInternal::findRowByConfigID( editorConfig._themePreset )._preset;
        applyPreset( preset );

        if ( preset != EditorThemePreset::ClassicDark )
        {
            EditorThemeConfig themeConfig        = EditorThemeInternal::activeTheme();
            const bool        bHasAccentOverride = editorConfig._themeAccentR > 0.0f || editorConfig._themeAccentG > 0.0f || editorConfig._themeAccentB > 0.0f;
            if ( bHasAccentOverride )
                themeConfig._accentColor = Color{ editorConfig._themeAccentR, editorConfig._themeAccentG, editorConfig._themeAccentB, 1.0f };

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
        editorConfig._themePreset            = EditorThemeInternal::findRow( themeConfig._preset )._pConfigID;

        editorConfig._themeAccentR        = themeConfig._accentColor._r;
        editorConfig._themeAccentG        = themeConfig._accentColor._g;
        editorConfig._themeAccentB        = themeConfig._accentColor._b;
        editorConfig._themeWindowRounding = themeConfig._windowRounding;
        editorConfig._themeFrameRounding  = themeConfig._frameRounding;
        editorConfig._themeTabRounding    = themeConfig._tabRounding;

        EditorConfig::setActive( editorConfig );
        EditorConfig::saveToPreferences();
    }

    void EditorThemeUtil::setAccentColor( const Color& accentColor )
    {
        EditorThemeConfig editorConfig = EditorThemeInternal::activeTheme();
        editorConfig._accentColor      = accentColor;
        applyTheme( editorConfig );
        saveToConfig();
    }

    Color EditorThemeUtil::getViewportHoverColor()
    {
        const Color& accent = getAccentColor();
        return Color::lerp( accent, Color{ 1.0f, 1.0f, 1.0f, 1.0f }, EditorThemeInternal::kHoverWhiten ).withAlpha( EditorThemeInternal::kHoverAlpha );
    }

    const Color& EditorThemeUtil::getAccentColor()
    {
        return EditorThemeInternal::activeTheme()._accentColor;
    }

    const Color& EditorThemeUtil::getWindowBgColor()
    {
        return EditorThemeInternal::activeTheme()._windowBg;
    }

    const Color& EditorThemeUtil::getPanelBgColor()
    {
        return EditorThemeInternal::activeTheme()._panelBg;
    }

    const Color& EditorThemeUtil::getHeaderBgColor()
    {
        return EditorThemeInternal::activeTheme()._headerBg;
    }

    const Color& EditorThemeUtil::getFrameBgColor()
    {
        return EditorThemeInternal::activeTheme()._frameBg;
    }

    const Color& EditorThemeUtil::getBorderColor()
    {
        return EditorThemeInternal::activeTheme()._border;
    }

    const Color& EditorThemeUtil::getSuccessColor()
    {
        return EditorThemeInternal::activeTheme()._successColor;
    }

    const Color& EditorThemeUtil::getWarningColor()
    {
        return EditorThemeInternal::activeTheme()._warningColor;
    }

    const Color& EditorThemeUtil::getErrorColor()
    {
        return EditorThemeInternal::activeTheme()._errorColor;
    }

    const Color& EditorThemeUtil::getInfoColor()
    {
        return EditorThemeInternal::activeTheme()._infoColor;
    }

    const Color& EditorThemeUtil::getTextMutedColor()
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

    void EditorThemeUtil::pushTextColor( const Color& color )
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
        const Color& accent = getAccentColor();
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
        const Color& accent = getAccentColor();
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

        ImGui::SetNextWindowSize( ImVec2( 460.0f * EditorThemeUtil::getDpiScale(), 380.0f * EditorThemeUtil::getDpiScale() ), ImGuiCond_FirstUseEver );
        if ( ImGui::Begin( makeIconLabel( editoricon::kPalette, "Theme & Look and Feel" ), pOpen, ImGuiWindowFlags_NoCollapse ) )
        {
            EditorThemeConfig editorConfig = EditorThemeInternal::activeTheme();

            // 1) 프리셋 선택. 이름과 순서는 프리셋 테이블에서 온다. 열거형 값을 인덱스로 삼지 않는다.
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
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemePreset" ) );

            ImGui::Separator();
            ImGui::TextDisabled( "Color Palette Customization" );

            // 2) 액센트 컬러 피커
            float32 arrAccentRaw[4] = { editorConfig._accentColor._r, editorConfig._accentColor._g, editorConfig._accentColor._b, editorConfig._accentColor._a };
            if ( ImGui::ColorEdit4( "Accent Color", arrAccentRaw, ImGuiColorEditFlags_NoAlpha ) )
            {
                editorConfig._accentColor = Color{ arrAccentRaw[0], arrAccentRaw[1], arrAccentRaw[2], arrAccentRaw[3] };
                applyTheme( editorConfig );
                saveToConfig();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeAccentColor" ) );

            // 3) 빠른 액센트 팔레트 스와치
            ImGui::Text( "Accent Swatches:" );
            ImGui::SameLine();
            const Color arrSwatches[] = {
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
                if ( ImGui::ColorButton( "##swatch", EditorThemeInternal::toImVec4( arrSwatches[index] ), ImGuiColorEditFlags_NoAlpha, ImVec2( 22.0f * EditorThemeUtil::getDpiScale(), 22.0f * EditorThemeUtil::getDpiScale() ) ) )
                {
                    setAccentColor( arrSwatches[index] );
                    editorConfig = getActiveTheme();
                }
                if ( index == 2 )
                    EditorSelfTestMarks::note( "theme.swatch.violet" ); // 에디터 자체 테스트(input.classicDarkSwatch)가 누른다
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeAccentSwatch" ) );
                ImGui::PopID();
            }

            ImGui::Separator();
            ImGui::TextDisabled( "Geometry & Rounding" );

            // 4) 라운딩 슬라이더
            bool bMetricsChanged = false;
            bMetricsChanged |= ImGui::SliderFloat( "Window Rounding", &editorConfig._windowRounding, 0.0f, 12.0f, "%.0f px" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeWindowRounding" ) );
            bMetricsChanged |= ImGui::SliderFloat( "Frame Rounding", &editorConfig._frameRounding, 0.0f, 8.0f, "%.0f px" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeFrameRounding" ) );
            bMetricsChanged |= ImGui::SliderFloat( "Tab Rounding", &editorConfig._tabRounding, 0.0f, 8.0f, "%.0f px" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeTabRounding" ) );

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
            if ( ImGui::Button( makeIconLabel( editoricon::kUndo, "Reset to Modern Dark" ) ) )
            {
                applyPreset( EditorThemePreset::ModernDark );
                saveToConfig();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeReset" ) );
        }
        ImGui::End();
    }

    Color EditorThemeUtil::getFolderColor()
    {
        // 폴더 색은 테마 액센트 색을 그대로 쓴다. UE5 와 Rider 처럼 액센트와 같은 파란 계열이다
        return getAccentColor();
    }

    const utf8* EditorThemeUtil::getFolderIcon( bool bOpened )
    {
        return bOpened ? editoricon::kFolderOpen : editoricon::kFolder;
    }

    const utf8* EditorThemeUtil::getAssetTypeIcon( EditorAssetType kind )
    {
        const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
        return pInfo != nullptr ? pInfo->_pIcon : editoricon::kFile;
    }

    Color EditorThemeUtil::getAssetTypeColor( EditorAssetType kind )
    {
        const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
        if ( pInfo == nullptr )
            return getTextMutedColor();
        return pInfo->_bAccentColor ? getAccentColor() : pInfo->_color;
    }

    const utf8* EditorThemeUtil::getAssetIconForPath( string_view path, bool bIsDirectory )
    {
        if ( bIsDirectory )
            return editoricon::kFolder;
        return getAssetTypeIcon( EditorAssetTypeRegistry::findKind( path ) );
    }

    Color EditorThemeUtil::getAssetColorForPath( string_view path, bool bIsDirectory )
    {
        if ( bIsDirectory )
            return getFolderColor();
        return getAssetTypeColor( EditorAssetTypeRegistry::findKind( path ) );
    }

    void EditorThemeUtil::drawCenteredGlyph( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos, const utf8* pGlyph, const Color& color, float32 sizeRatio )
    {
        const bool bNothingToDraw = pDrawList == nullptr || pGlyph == nullptr || pGlyph[0] == '\0';
        if ( bNothingToDraw )
            return;
        ImFont*       pFont    = ImGui::GetFont();
        const float32 boxSize  = MathUtil::min( maxPos._x - minPos._x, maxPos._y - minPos._y );
        const float32 fontSize = boxSize * sizeRatio;
        if ( pFont == nullptr || fontSize <= 0.0f )
            return;
        const ImVec2 glyphSize = pFont->CalcTextSizeA( fontSize, MathUtil::kMaxFloat, 0.0f, pGlyph );
        const ImVec2 position{ ( minPos._x + maxPos._x - glyphSize.x ) * 0.5f, ( minPos._y + maxPos._y - glyphSize.y ) * 0.5f };
        pDrawList->AddText( pFont, fontSize, position, EditorColorUtil::toImU32( color ), pGlyph );
    }

    const utf8* EditorThemeUtil::makeIconLabel( const utf8* pIcon, const utf8* pLabel )
    {
        // UI 스레드 하나가 호출하지만 자체 테스트 스레드에서도 호출할 수 있으므로 스레드마다 버퍼를 둔다.
        thread_local fixed_string<constant::kMaxBuffer256> s_label;
        formatstring( s_label.data(), s_label.capacity(), "%#  %#", pIcon, pLabel );
        return s_label.c_str();
    }
} // namespace sw::editor

namespace sw::editor
{
    // 테마는 환경설정 섹션 "Editor/Appearance" 다. 저장은 EditorPreferences.json 하나에 하고, 값을 고치면(창에서 편집하거나 파일을 다시 읽으면) 테마를 다시 적용한다.
    SW_EDITOR_REGISTER( EditorSettingsRegistration, Settings_EditorConfig, { "appearance", 200 }, "Editor/Appearance", &EditorConfig::StaticType,
                        &EditorThemeInternal::getAppearanceInstance, &EditorThemeInternal::getAppearanceDefault, &EditorThemeUtil::loadFromConfig, nullptr );
} // namespace sw::editor
