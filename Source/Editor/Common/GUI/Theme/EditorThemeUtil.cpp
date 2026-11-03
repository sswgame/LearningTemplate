#include "pch.h"

#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/VectorMath.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/GUI/EditorChrome.h"
#include "Editor/Common/GUI/Theme/EditorColorUtil.h"
#include "Editor/Common/GUI/Theme/EditorFontSetup.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
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
            /**
             * @brief 환경설정 섹션 Appearance 의 값을 고칠 수 있게 돌려줍니다. 값은 등록 줄(`SW_EDITOR_SETTINGS`)이 가리키는 그 인스턴스 하나다 —
             *        테마를 저장할 때(`saveToConfig`)만 쓴다. 읽기는 `getPreferences<EditorConfig>()` 다.
             */
            static EditorConfig& editAppearance() { return editPreferences<EditorConfig>(); }

            /**
             * @brief 환경설정 섹션 Appearance 의 값이 바뀌었을 때(창에서 고침 · 파일을 다시 읽음 · 섹션 되돌림) 테마를 다시 적용합니다.
             * @details 환경설정 파일은 ImGui 컨텍스트보다 먼저 읽는다 — 그때는 적용할 UI 가 없고, 컨텍스트를 만든 뒤 `ImGuiEditor` 가 적용하며 실패하면 기동을 멈춘다.
             *          실행 중에 없는 벌 이름을 적으면 오류를 남기고 지금 테마를 그대로 둔다.
             */
            static void onAppearanceChanged()
            {
                if ( ImGui::GetCurrentContext() != nullptr && EditorThemeUtil::loadFromConfig() == false )
                    SW_LOG_ERROR_ID( "EditorThemeUtil.EditorAppearanceNamesTheme", "Editor/Appearance names a theme that cannot be applied - the current theme stays until it is fixed" );
            }

            /**
             * @brief 현재 적용된 테마를 반환합니다. 에디터 서브시스템 `EditorTheme` 이 듭니다.
             * @details 테마는 에디터 범위 안에서만 뜻이 있다. 범위 밖에서 색을 물으면 지어낸 빈 테마로 답하지 않고 `getSubsystem` 이 멈춘다.
             */
            static const EditorThemeConfig& activeTheme() { return getSubsystem<EditorTheme>().getConfig(); }

            /** @brief 테마를 교체합니다. */
            static void setActiveTheme( const EditorThemeConfig& config ) { getSubsystem<EditorTheme>().getConfig() = config; }

            /** @brief 폴더에 있는 토큰 벌을 모두 읽어 둡니다. 읽지 못한 벌은 이유와 함께 로그로 남기고 뺀다. */
            static void loadAvailableThemes( vector<EditorThemeConfig>& outListTheme )
            {
                outListTheme.clear();
                vector<string> listName;
                EditorThemeTokens::listThemeNames( listName );
                for ( const string& name : listName )
                {
                    EditorThemeConfig config{};
                    string            error;
                    string            mismatchKey;
                    if ( EditorThemeTokens::loadTheme( name, config, error ) == false )
                        SW_LOG_ERROR_ID( "EditorThemeUtil.ThemeTokensNotLoaded", "Theme tokens '%#' were not loaded: %#", name.c_str(), error.c_str() );
                    else if ( outListTheme.empty() == false && EditorThemeTokens::findKeyMismatch( outListTheme.front(), config, mismatchKey ) )
                        SW_LOG_ERROR_ID( "EditorThemeUtil.ThemeTokensNotLoaded2", "Theme tokens '%#' were not loaded: key '%#' is in only one of '%#' and '%#' - every theme needs the same keys", name.c_str(),
                                         mismatchKey.c_str(), outListTheme.front()._name.c_str(), name.c_str() );
                    else
                        outListTheme.push_back( std::move( config ) );
                }
                if ( outListTheme.empty() )
                    SW_LOG_ERROR_ID( "EditorThemeUtil.NoEditorThemeToken", "No editor theme token file under %# - no theme can be applied", EditorThemeTokens::getFolderPath().c_str() );
            }

            /** @brief 읽어 둔 벌 목록입니다. 비었으면 지금 읽습니다. */
            static vector<EditorThemeConfig>& availableThemes()
            {
                EditorTheme& theme = getSubsystem<EditorTheme>();
                if ( theme.getAvailable().empty() )
                    loadAvailableThemes( theme.getAvailable() );
                return theme.getAvailable();
            }

            static const EditorThemeConfig* findAvailable( string_view name )
            {
                for ( const EditorThemeConfig& theme : availableThemes() )
                {
                    if ( StringUtil::equals( theme._name, name, true ) )
                        return &theme;
                }
                return nullptr;
            }

            /** @brief 뷰포트 호버 색이 강조색에서 흰색 쪽으로 가는 비율과 알파입니다. */
            static constexpr float32 kHoverWhiten = 0.45f;
            static constexpr float32 kHoverAlpha  = 0.75f;
            /** @brief 둥글기를 한 슬라이더로 묶을 때 스크롤바 둥글기는 입력 필드 둥글기의 이 배수입니다. */
            static constexpr float32 kScrollbarRadiusPerFrameRadius = 2.0f;

            static ImVec4 toImVec4( const Color& color, float32 alphaMultiplier = 1.0f )
            {
                return EditorColorUtil::toImVec4( color.scaleAlpha( alphaMultiplier ) );
            }

            static ImVec4 adjustBrightness( const Color& color, float32 factor, float32 alpha = 1.0f )
            {
                return EditorColorUtil::toImVec4( color.scaleBrightness( factor ).saturate().withAlpha( alpha ) );
            }

            /**
             * @brief 호버 · 누름 상태의 한 단계입니다 — 어두운 바탕은 밝게, 밝은 바탕은 어둡게 옮긴다(흰 프레임을 밝히면 그대로라 상태가 보이지 않는다).
             * @details 상태는 밝기로 보인다. 강조색으로 칠하지 않는다(앱 화면 규칙: 색은 데이터와 선택에만 — 언리얼 5 · Godot 4 의 다크 테마도 호버가 회색 단계다).
             */
            static ImVec4 stepTone( const Color& color, float32 amount, float32 alpha = 1.0f )
            {
                const float32 luminance = 0.2126f * color._r + 0.7152f * color._g + 0.0722f * color._b;
                return adjustBrightness( color, luminance < 0.5f ? 1.0f + amount : 1.0f - amount * 0.25f, alpha );
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

            /** @brief 사용자가 덮어쓰는 토큰 키입니다(강조색 · 둥글기 세 슬라이더). 나머지 둥글기는 이 셋에서 정한다. */
            static inline const hashed_string kAccentKey{ "Base.Accent" };

            /** @brief 둥글기 슬라이더 셋의 값을 테마에 씁니다. 팝업은 창, 스크롤바 · 그랩은 입력 필드를 따른다. */
            static void setRadius( float32 window, float32 frame, float32 tab, EditorThemeConfig& inoutTheme )
            {
                inoutTheme._mapMetric[hashed_string( "Radius.Window" )]    = window;
                inoutTheme._mapMetric[hashed_string( "Radius.Frame" )]     = frame;
                inoutTheme._mapMetric[hashed_string( "Radius.Tab" )]       = tab;
                inoutTheme._mapMetric[hashed_string( "Radius.Popup" )]     = window;
                inoutTheme._mapMetric[hashed_string( "Radius.Scrollbar" )] = frame * kScrollbarRadiusPerFrameRadius;
                inoutTheme._mapMetric[hashed_string( "Radius.Grab" )]      = frame;
            }

            /** @brief 사용자 설정(강조색 · 둥글기 덮어쓰기)을 테마 위에 얹습니다. 값이 음수면 덮어쓰지 않은 것입니다. */
            static void applyUserOverrides( const EditorConfig& editorConfig, EditorThemeConfig& inoutTheme )
            {
                const bool bHasAccentOverride = editorConfig._themeAccentR >= 0.0f && editorConfig._themeAccentG >= 0.0f && editorConfig._themeAccentB >= 0.0f;
                if ( bHasAccentOverride )
                {
                    inoutTheme._mapColor[kAccentKey] = Color{ editorConfig._themeAccentR, editorConfig._themeAccentG, editorConfig._themeAccentB, 1.0f };
                    inoutTheme._bAccentOverridden    = SW_TRUE;
                }
                if ( editorConfig._themeWindowRounding >= 0.0f )
                {
                    setRadius( editorConfig._themeWindowRounding, editorConfig._themeFrameRounding, editorConfig._themeTabRounding, inoutTheme );
                    inoutTheme._bRadiusOverridden = SW_TRUE;
                }
            }

            /** @brief 1 단계: 지오메트리와 레이아웃 메트릭(토큰)을 스타일에 씁니다. 여백 · 간격에는 밀도를, 크기에는 모니터 배율을 곱합니다. 지금 테마(`applyTheme` 이 먼저 바꿔 둔다)를 키로 읽는다. */
            static void applyMetrics( ImGuiStyle& style )
            {
                const float32 density   = SW_THEMEMETRIC( "Layout", "Density" );
                style.WindowRounding    = SW_THEMEMETRIC( "Radius", "Window" );
                style.ChildRounding     = SW_THEMEMETRIC( "Radius", "Frame" );
                style.FrameRounding     = SW_THEMEMETRIC( "Radius", "Frame" );
                style.PopupRounding     = SW_THEMEMETRIC( "Radius", "Popup" );
                style.ScrollbarRounding = SW_THEMEMETRIC( "Radius", "Scrollbar" );
                style.GrabRounding      = SW_THEMEMETRIC( "Radius", "Grab" );
                style.TabRounding       = SW_THEMEMETRIC( "Radius", "Tab" );

                style.WindowBorderSize = SW_THEMEMETRIC( "Stroke", "Window" );
                style.ChildBorderSize  = SW_THEMEMETRIC( "Stroke", "Child" );
                style.PopupBorderSize  = SW_THEMEMETRIC( "Stroke", "Popup" );
                style.FrameBorderSize  = SW_THEMEMETRIC( "Stroke", "Frame" );
                style.TabBorderSize    = SW_THEMEMETRIC( "Stroke", "Tab" );

                style.WindowPadding    = ImVec2( SW_THEMEMETRIC( "Spacing", "WindowPaddingX" ) * density, SW_THEMEMETRIC( "Spacing", "WindowPaddingY" ) * density );
                style.FramePadding     = ImVec2( SW_THEMEMETRIC( "Spacing", "FramePaddingX" ) * density, SW_THEMEMETRIC( "Spacing", "FramePaddingY" ) * density );
                style.ItemSpacing      = ImVec2( SW_THEMEMETRIC( "Spacing", "ItemSpacingX" ) * density, SW_THEMEMETRIC( "Spacing", "ItemSpacingY" ) * density );
                style.ItemInnerSpacing = ImVec2( SW_THEMEMETRIC( "Spacing", "ItemInnerSpacingX" ) * density, SW_THEMEMETRIC( "Spacing", "ItemInnerSpacingY" ) * density );
                style.IndentSpacing    = SW_THEMEMETRIC( "Spacing", "Indent" ) * density;
                style.ScrollbarSize    = SW_THEMEMETRIC( "Spacing", "ScrollbarSize" );
                style.GrabMinSize      = SW_THEMEMETRIC( "Spacing", "GrabMinSize" );
                // 떠 있는 창 제목줄의 접기 단추(▼)를 뺀다 — 접힌 창은 쓸 일이 없고 제목 앞 빈 단추로만 보였다.
                style.WindowMenuButtonPosition = ImGuiDir_None;
                // 위 크기는 96 DPI 기준이다. 모니터 배율을 곱한다(`setDPIScale`).
                const float32 dpiScale = getSubsystem<EditorTheme>().getDPIScale();
                if ( dpiScale != 1.0f )
                    style.ScaleAllSizes( dpiScale );
                // 글자 크기도 토큰이다(em px). ImGui 1.92 동적 글꼴이라 다음 프레임부터 그 크기로 래스터한다(배율은 `FontScaleDpi` 가 따로 곱한다).
                style.FontSizeBase = EditorFontSetup::toBaseFontSize( SW_THEMEMETRIC( "Typography", "FontSize" ) );
            }

            /** @brief 2 단계: 컬러 팔레트를 스타일에 씁니다. 상태(호버 · 활성)는 토큰에서 밝기와 알파로 만듭니다. */
            static void applyPalette( ImGuiStyle& style )
            {
                ImVec4* pColors = style.Colors;

                const Color& accent   = SW_THEMECOLOR( "Base", "Accent" );
                const Color& winBg    = SW_THEMECOLOR( "Base", "WindowBg" );
                const Color& panBg    = SW_THEMECOLOR( "Base", "PanelBg" );
                const Color& headerBg = SW_THEMECOLOR( "Base", "HeaderBg" );
                const Color& frmBg    = SW_THEMECOLOR( "Base", "FrameBg" );
                const Color& border   = SW_THEMECOLOR( "Base", "Border" );

                // 텍스트
                pColors[ImGuiCol_Text]         = EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Base", "Text" ) );
                pColors[ImGuiCol_TextDisabled] = EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Base", "TextMuted" ), 1.0f );

                // 창 및 배경
                pColors[ImGuiCol_WindowBg]     = EditorThemeInternal::toImVec4( winBg, 1.0f );
                pColors[ImGuiCol_ChildBg]      = EditorThemeInternal::toImVec4( panBg, 1.0f );
                pColors[ImGuiCol_PopupBg]      = EditorThemeInternal::adjustBrightness( winBg, 1.15f, 0.98f );
                pColors[ImGuiCol_Border]       = EditorThemeInternal::toImVec4( border, 1.0f );
                pColors[ImGuiCol_BorderShadow] = EditorThemeInternal::toImVec4( border, 0.0f );
                pColors[ImGuiCol_MenuBarBg]    = EditorThemeInternal::adjustBrightness( winBg, 0.85f, 1.0f );

                // 입력 프레임
                pColors[ImGuiCol_FrameBg]        = EditorThemeInternal::toImVec4( frmBg, 1.0f );
                pColors[ImGuiCol_FrameBgHovered] = EditorThemeInternal::stepTone( frmBg, 0.25f );
                pColors[ImGuiCol_FrameBgActive]  = EditorThemeInternal::stepTone( frmBg, 0.45f );

                // 타이틀바
                pColors[ImGuiCol_TitleBg]          = EditorThemeInternal::adjustBrightness( winBg, 0.9f, 1.0f );
                pColors[ImGuiCol_TitleBgActive]    = EditorThemeInternal::toImVec4( headerBg, 1.0f );
                pColors[ImGuiCol_TitleBgCollapsed] = EditorThemeInternal::toImVec4( winBg, 0.75f );

                // 스크롤바
                pColors[ImGuiCol_ScrollbarBg]          = EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Base", "ScrollbarBg" ) );
                pColors[ImGuiCol_ScrollbarGrab]        = EditorThemeInternal::adjustBrightness( border, 1.1f, 0.8f );
                pColors[ImGuiCol_ScrollbarGrabHovered] = EditorThemeInternal::adjustBrightness( border, 1.4f, 1.0f );
                pColors[ImGuiCol_ScrollbarGrabActive]  = EditorThemeInternal::toImVec4( accent, 0.9f );

                // 버튼
                pColors[ImGuiCol_Button]        = EditorThemeInternal::stepTone( frmBg, 0.1f );
                pColors[ImGuiCol_ButtonHovered] = EditorThemeInternal::stepTone( frmBg, 0.4f );
                pColors[ImGuiCol_ButtonActive]  = EditorThemeInternal::stepTone( frmBg, 0.65f );

                // 헤더
                pColors[ImGuiCol_Header]        = EditorThemeInternal::toImVec4( headerBg, 1.0f );
                pColors[ImGuiCol_HeaderHovered] = EditorThemeInternal::stepTone( headerBg, 0.25f );
                pColors[ImGuiCol_HeaderActive]  = EditorThemeInternal::stepTone( headerBg, 0.45f );

                // 탭 & 도킹
                pColors[ImGuiCol_Tab]                = EditorThemeInternal::adjustBrightness( winBg, 0.95f, 1.0f );
                pColors[ImGuiCol_TabHovered]         = EditorThemeInternal::stepTone( panBg, 0.3f );
                pColors[ImGuiCol_TabActive]          = EditorThemeInternal::toImVec4( panBg, 1.0f );
                pColors[ImGuiCol_TabUnfocused]       = EditorThemeInternal::adjustBrightness( winBg, 0.80f, 1.0f );
                pColors[ImGuiCol_TabUnfocusedActive] = EditorThemeInternal::adjustBrightness( panBg, 0.90f, 1.0f );
                pColors[ImGuiCol_DockingPreview]     = EditorThemeInternal::toImVec4( accent, 0.45f );
                pColors[ImGuiCol_DockingEmptyBg]     = EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Base", "DockingEmptyBg" ) );

                // 구분선 & 기타
                pColors[ImGuiCol_Separator]         = EditorThemeInternal::toImVec4( border, 0.85f );
                pColors[ImGuiCol_SeparatorHovered]  = EditorThemeInternal::toImVec4( accent, 0.85f );
                pColors[ImGuiCol_SeparatorActive]   = EditorThemeInternal::toImVec4( accent, 1.0f );
                pColors[ImGuiCol_ResizeGrip]        = EditorThemeInternal::toImVec4( accent, 0.0f );
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
                pColors[ImGuiCol_TableRowBg]        = EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Base", "TableRowBgAlt" ), 0.0f );
                pColors[ImGuiCol_TableRowBgAlt]     = EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Base", "TableRowBgAlt" ) );
            }

            /** @brief 종류 등록의 아이콘입니다. 종류가 없으면 일반 파일 아이콘입니다. */
            static const utf8* getIconOf( const EditorAssetTypeRegistration* pRegistration )
            {
                return pRegistration != nullptr ? EditorIconUtil::findGlyphByName( pRegistration->_pIconName ) : SW_EDITORICON( "File" );
            }

            /** @brief 종류 등록의 테마 색(키는 등록 줄이 든다)입니다. 종류가 없으면 흐린 글자 색입니다. */
            static Color getColorOf( const EditorAssetTypeRegistration* pRegistration )
            {
                if ( pRegistration == nullptr )
                    return SW_THEMECOLOR( "Base", "TextMuted" );
                return EditorThemeUtil::findColor( hashed_string{ pRegistration->_pColorKey } );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    const EditorThemeConfig& EditorThemeUtil::getActiveTheme()
    {
        return EditorThemeInternal::activeTheme();
    }

    bool EditorThemeUtil::applyThemeByName( string_view name )
    {
        const EditorThemeConfig* pTheme = EditorThemeInternal::findAvailable( name );
        if ( pTheme == nullptr )
            return false;
        applyTheme( *pTheme );
        return true;
    }

    void EditorThemeUtil::listThemes( vector<string>& outListName )
    {
        outListName.clear();
        for ( const EditorThemeConfig& theme : EditorThemeInternal::availableThemes() )
        {
            outListName.push_back( theme._name );
        }
    }

    void EditorThemeUtil::reloadThemes()
    {
        EditorTheme& theme = getSubsystem<EditorTheme>();
        EditorThemeInternal::loadAvailableThemes( theme.getAvailable() );
        // 지금 벌을 새 값으로 다시 적용하고, 사용자가 덮어쓴 강조색 · 둥글기는 설정에서 다시 적용한다. 못 하면 오류를 남기고 지금 테마를 둔다.
        if ( loadFromConfig() )
            SW_LOG_INFO_ID( "EditorThemeUtil.EditorThemeTokensReloaded", "Editor theme tokens reloaded (%# set(s))", theme.getAvailable().size() );
    }

    void EditorThemeUtil::applyTheme( const EditorThemeConfig& config )
    {
        EditorThemeInternal::setActiveTheme( config );

        ImGuiStyle& style = ImGui::GetStyle();
        EditorThemeInternal::resetSizesToDefault( style );

        EditorThemeInternal::applyMetrics( style );
        EditorThemeInternal::applyPalette( style );
    }

    void EditorThemeUtil::setDPIScale( float32 dpiScale )
    {
        const float32 scale = ( dpiScale > 0.0f ) ? dpiScale : 1.0f;
        EditorTheme&  theme = getSubsystem<EditorTheme>();
        if ( theme.getDPIScale() == scale )
            return;
        // 테마는 96 DPI 기준 값을 보관하고 있으므로 새 배율로 다시 적용하면 된다.
        theme.setDPIScale( scale );
        applyTheme( getActiveTheme() );
        ImGui::GetStyle().FontScaleDpi = scale;
    }

    float32 EditorThemeUtil::getDPIScale()
    {
        return getSubsystem<EditorTheme>().getDPIScale();
    }

    bool EditorThemeUtil::loadFromConfig()
    {
        // 테마 이름의 원본은 설정(`EditorConfig::_themeName`)이다. 그 벌이 없으면 다른 벌로 바꿔 끼우지 않는다 — 고친 이름이 무시된 것을 아무도 모른다.
        const EditorConfig&      editorConfig = getPreferences<EditorConfig>();
        const EditorThemeConfig* pTheme       = EditorThemeInternal::findAvailable( editorConfig._themeName );
        if ( pTheme == nullptr )
        {
            SW_LOG_ERROR_ID( "EditorThemeUtil.EditorThemeEditorAppearance", "Editor theme '%#' (Editor/Appearance) has no token file under %#", editorConfig._themeName.c_str(), EditorThemeTokens::getFolderPath().c_str() );
            return false;
        }

        EditorThemeConfig themeConfig = *pTheme;
        EditorThemeInternal::applyUserOverrides( editorConfig, themeConfig );
        applyTheme( themeConfig );
        return true;
    }

    void EditorThemeUtil::saveToConfig()
    {
        EditorConfig& editorConfig = EditorThemeInternal::editAppearance();

        const EditorThemeConfig& themeConfig = EditorThemeInternal::activeTheme();
        editorConfig._themeName              = themeConfig._name;

        // 덮어쓰지 않은 값은 음수로 둔다. 그래야 토큰 파일이 바뀌면 그 값을 따른다.
        const bool   bAccent       = themeConfig._bAccentOverridden == SW_TRUE;
        const Color& accent        = SW_THEMECOLOR( "Base", "Accent" );
        editorConfig._themeAccentR = bAccent ? accent._r : EditorConfig::kNotOverridden;
        editorConfig._themeAccentG = bAccent ? accent._g : EditorConfig::kNotOverridden;
        editorConfig._themeAccentB = bAccent ? accent._b : EditorConfig::kNotOverridden;

        const bool bRadius                = themeConfig._bRadiusOverridden == SW_TRUE;
        editorConfig._themeWindowRounding = bRadius ? SW_THEMEMETRIC( "Radius", "Window" ) : EditorConfig::kNotOverridden;
        editorConfig._themeFrameRounding  = bRadius ? SW_THEMEMETRIC( "Radius", "Frame" ) : EditorConfig::kNotOverridden;
        editorConfig._themeTabRounding    = bRadius ? SW_THEMEMETRIC( "Radius", "Tab" ) : EditorConfig::kNotOverridden;

        EditorConfig::saveToPreferences();
    }

    void EditorThemeUtil::setAccentColor( const Color& accentColor )
    {
        EditorThemeConfig editorConfig                          = EditorThemeInternal::activeTheme();
        editorConfig._mapColor[EditorThemeInternal::kAccentKey] = accentColor;
        editorConfig._bAccentOverridden                         = SW_TRUE;
        applyTheme( editorConfig );
        saveToConfig();
    }

    Color EditorThemeUtil::getViewportHoverColor()
    {
        return Color::lerp( SW_THEMECOLOR( "Base", "Accent" ), SW_THEMECOLOR( "Base", "Text" ), EditorThemeInternal::kHoverWhiten ).withAlpha( EditorThemeInternal::kHoverAlpha );
    }

    const Color& EditorThemeUtil::findColor( const hashed_string& key )
    {
        static constexpr Color kMissingColor{ 1.0f, 0.0f, 1.0f, 1.0f };
        const Color*           pColor = EditorThemeInternal::activeTheme().findColor( key );
        if ( pColor != nullptr )
            return *pColor;
        if ( getSubsystem<EditorTheme>().markMissingKey( key ) )
            SW_LOG_ERROR_ID( "EditorTheme.MissingKey", "Theme color '%#' is not in the '%#' token file (_color)", key.c_str(), EditorThemeInternal::activeTheme()._name.c_str() );
        return kMissingColor;
    }

    float32 EditorThemeUtil::findMetric( const hashed_string& key )
    {
        const float32* pValue = EditorThemeInternal::activeTheme().findMetric( key );
        if ( pValue != nullptr )
            return *pValue;
        if ( getSubsystem<EditorTheme>().markMissingKey( key ) )
            SW_LOG_ERROR_ID( "EditorTheme.MissingKey", "Theme metric '%#' is not in the '%#' token file (_metric)", key.c_str(), EditorThemeInternal::activeTheme()._name.c_str() );
        return 0.0f;
    }

    void EditorThemeUtil::textAccent( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Base", "Accent" ) ), "%s", pText );
    }

    void EditorThemeUtil::textSuccess( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Signal", "Success" ) ), "%s", pText );
    }

    void EditorThemeUtil::textInfo( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Signal", "Info" ) ), "%s", pText );
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
            ImGui::TextColored( EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Signal", "Warning" ) ), "%s", pText );
    }

    void EditorThemeUtil::textError( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Signal", "Error" ) ), "%s", pText );
    }

    void EditorThemeUtil::textMuted( const utf8* pText )
    {
        if ( pText != nullptr )
            ImGui::TextColored( EditorThemeInternal::toImVec4( SW_THEMECOLOR( "Base", "TextMuted" ) ), "%s", pText );
    }

    void EditorThemeUtil::pushAccentButton( float32 alpha )
    {
        const Color& accent = SW_THEMECOLOR( "Base", "Accent" );
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
        const Color& accent = SW_THEMECOLOR( "Base", "Accent" );
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

        EditorChrome::setNextWindowExtentFirstUse( EditorTextExtent::ofText( 61.0f, 20.0f ) );
        if ( ImGui::Begin( makeIconLabel( SW_EDITORICON( "Palette" ), "Theme & Look and Feel" ), pOpen, ImGuiWindowFlags_NoCollapse ) )
        {
            EditorThemeConfig editorConfig = EditorThemeInternal::activeTheme();

            // 1) 테마(토큰 벌) 선택. 목록은 `Content/editor/theme/*.tokens.json` 이다.
            if ( ImGui::BeginCombo( "Theme", editorConfig._displayName.c_str() ) )
            {
                for ( const EditorThemeConfig& theme : EditorThemeInternal::availableThemes() )
                {
                    const bool bSelected = theme._name == editorConfig._name;
                    if ( ImGui::Selectable( theme._displayName.c_str(), bSelected ) )
                    {
                        applyTheme( theme );
                        saveToConfig();
                        editorConfig = EditorThemeInternal::activeTheme();
                    }
                    if ( bSelected )
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemePreset" ) );

            ImGui::Separator();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "ColorPaletteCustomization" ) );

            // 2) 액센트 컬러 피커
            const Color& accent          = SW_THEMECOLOR( "Base", "Accent" );
            float32      arrAccentRaw[4] = { accent._r, accent._g, accent._b, accent._a };
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "AccentColor" ) );
            if ( ImGui::ColorEdit4( "##AccentColor", arrAccentRaw, ImGuiColorEditFlags_NoAlpha ) )
                setAccentColor( Color{ arrAccentRaw[0], arrAccentRaw[1], arrAccentRaw[2], arrAccentRaw[3] } );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeAccentColor" ) );

            // 3) 빠른 액센트 팔레트 스와치(테마와 무관한 추천 색이다)
            ImGui::TextUnformatted( SW_LOCKEY( "EdText", "AccentSwatches" ) );
            ImGui::SameLine();
            static constexpr Color kArrSwatch[] = {
                {0.27f, 0.57f,  1.0f, 1.0f}, // Electric Blue
                {0.15f, 0.75f, 0.55f, 1.0f}, // Emerald Cyan
                {0.65f, 0.35f, 0.95f, 1.0f}, // Neon Violet
                {0.95f, 0.55f, 0.15f, 1.0f}, // Amber Gold
                {0.90f, 0.25f, 0.35f, 1.0f}  // Crimson
            };
            const float32 swatchSize = ImGui::GetFrameHeight();
            for ( int32 index = 0; index < static_cast<int32>( SW_COUNT_OF( kArrSwatch ) ); ++index )
            {
                if ( index > 0 )
                    ImGui::SameLine();
                ImGui::PushID( index );
                if ( ImGui::ColorButton( "##swatch", EditorThemeInternal::toImVec4( kArrSwatch[index] ), ImGuiColorEditFlags_NoAlpha, ImVec2( swatchSize, swatchSize ) ) )
                {
                    setAccentColor( kArrSwatch[index] );
                    editorConfig = getActiveTheme();
                }
                if ( index == 2 )
                    EditorSelfTestMarks::note( "theme.swatch.violet" ); // 에디터 자체 테스트(input.themeSwatch)가 누른다
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeAccentSwatch" ) );
                ImGui::PopID();
            }

            ImGui::Separator();
            ImGui::TextDisabled( "%s", SW_LOCKEY( "EdText", "GeometryRounding" ) );

            // 4) 라운딩 슬라이더
            float32 windowRadius    = SW_THEMEMETRIC( "Radius", "Window" );
            float32 frameRadius     = SW_THEMEMETRIC( "Radius", "Frame" );
            float32 tabRadius       = SW_THEMEMETRIC( "Radius", "Tab" );
            bool    bMetricsChanged = false;
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "WindowRounding" ) );
            bMetricsChanged |= ImGui::SliderFloat( "##WindowRounding", &windowRadius, 0.0f, 12.0f, "%.0f px" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeWindowRounding" ) );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "FrameRounding" ) );
            bMetricsChanged |= ImGui::SliderFloat( "##FrameRounding", &frameRadius, 0.0f, 8.0f, "%.0f px" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeFrameRounding" ) );
            EditorWidgets::drawPropertyRowLabel( SW_LOCKEY( "EdField", "TabRounding" ) );
            bMetricsChanged |= ImGui::SliderFloat( "##TabRounding", &tabRadius, 0.0f, 8.0f, "%.0f px" );
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeTabRounding" ) );

            if ( bMetricsChanged )
            {
                EditorThemeInternal::setRadius( windowRadius, frameRadius, tabRadius, editorConfig );
                editorConfig._bRadiusOverridden = SW_TRUE;
                applyTheme( editorConfig );
                saveToConfig();
            }

            ImGui::Separator();

            // 5) 덮어쓴 강조색 · 둥글기를 버리고 토큰 값으로 되돌린다.
            if ( ImGui::Button( makeIconLabel( SW_EDITORICON( "Undo" ), "Reset to Theme Defaults" ) ) )
            {
                (void)applyThemeByName( editorConfig._name ); // 목록에 있는 벌이다(콤보가 고른 것)
                saveToConfig();
            }
            EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ThemeReset" ) );
        }
        ImGui::End();
    }

    Color EditorThemeUtil::getFolderColor()
    {
        // 폴더 색은 테마 액센트 색을 그대로 쓴다. UE5 와 Rider 처럼 액센트와 같은 파란 계열이다
        return SW_THEMECOLOR( "Base", "Accent" );
    }

    const utf8* EditorThemeUtil::getFolderIcon( bool bOpened )
    {
        return bOpened ? SW_EDITORICON( "FolderOpen" ) : SW_EDITORICON( "Folder" );
    }

    const utf8* EditorThemeUtil::getAssetTypeIcon( const EditorAssetType& kind )
    {
        return EditorThemeInternal::getIconOf( EditorAssetTypeRegistry::findKindInfo( kind ) );
    }

    Color EditorThemeUtil::getAssetTypeColor( const EditorAssetType& kind )
    {
        return EditorThemeInternal::getColorOf( EditorAssetTypeRegistry::findKindInfo( kind ) );
    }

    const utf8* EditorThemeUtil::getAssetIconForPath( string_view path, bool bIsDirectory )
    {
        if ( bIsDirectory )
            return SW_EDITORICON( "Folder" );
        return EditorThemeInternal::getIconOf( EditorAssetTypeRegistry::findKindInfoForPath( path ) );
    }

    Color EditorThemeUtil::getAssetColorForPath( string_view path, bool bIsDirectory )
    {
        if ( bIsDirectory )
            return getFolderColor();
        return EditorThemeInternal::getColorOf( EditorAssetTypeRegistry::findKindInfoForPath( path ) );
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

    EditorIconLabel::EditorIconLabel( const utf8* pIcon, const utf8* pLabel )
        : _arrText{}
    {
        formatstring( _arrText, constant::buffer::kMax256, "%#  %#", pIcon, pLabel );
    }

    EditorIconLabel EditorThemeUtil::makeIconLabel( const utf8* pIcon, const utf8* pLabel )
    {
        return EditorIconLabel{ pIcon, pLabel };
    }
} // namespace sw::editor

namespace sw::editor
{
    // 테마는 환경설정 섹션 "Editor/Appearance" 다. 저장은 EditorPreferences.json 하나에 하고, 값을 고치면(창에서 편집하거나 파일을 다시 읽으면) 테마를 다시 적용한다.
    SW_EDITOR_SETTINGS( EditorConfig, "appearance", "Editor/Appearance", 200, &EditorThemeInternal::onAppearanceChanged );
} // namespace sw::editor
