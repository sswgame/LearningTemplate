#include "pch.h"

#include "Editor/Common/GUI/Theme/EditorFontSetup.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Config/EditorThemeConfig.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetPaths.h"
#include "Engine/Text/SystemFontLocator.h"

#include <imgui.h>
#include <imgui_freetype.h>

namespace sw::editor
{
    namespace
    {
        struct EditorFontSetupInternal
        {
            /**
             * @brief 글꼴 크기와 아이콘 비율을 설정한 테마 토큰(`_typography`)에서 읽습니다. 테마 서브시스템보다 먼저 불리므로 파일을 직접 읽는다.
             * @details 아이콘은 24 격자의 가운데 20 칸에 그려져 있어 em 의 5/6 을 차지합니다. 비율 0.8 이면 아이콘 높이가 본문 크기의 2/3 쯤입니다.
             * @return 토큰을 읽었으면 true. 못 읽으면 이유를 오류로 남기고 false 다(다른 벌이나 ImGui 기본 크기로 대신하지 않는다).
             */
            [[nodiscard]] static bool readTypography( float32& outFontSize, float32& outIconScale )
            {
                const string&     themeName = getPreferences<EditorConfig>()._themeName;
                EditorThemeConfig theme{};
                string            error;
                if ( EditorThemeTokens::loadTheme( themeName, theme, error ) == false )
                {
                    SW_LOG_ERROR( "Editor font size needs the theme '%#' tokens: %#", themeName.c_str(), error.c_str() );
                    return false;
                }
                outFontSize  = theme._typography._fontSize;
                outIconScale = theme._typography._iconScale;
                return true;
            }

            /**
             * @brief 폰트 파일 이름을 프로젝트 에디터 폰트 폴더 및 OS 시스템 폰트 디렉터리에서 검색하여 절대 경로를 반환합니다.
             */
            static string resolveFontFile( const utf8* pFileName )
            {
                if ( StringUtil::isNullOrEmpty( pFileName ) )
                    return {};

                const string editorRoot = AssetPaths::findLooseDomainFolder( path::kEditorPack ).getDiskPath();
                if ( editorRoot.empty() == false )
                {
                    const string candidate = FileUtil::joinPath( FileUtil::joinPath( editorRoot, editor::EditorPaths::kFontsFolderName ), pFileName );
                    if ( FileUtil::isRegularFile( candidate ) )
                        return candidate;
                }

                const string& resourceRoot = AssetPaths::getRootFolderPath();
                if ( resourceRoot.empty() == false )
                {
                    const string candidate = FileUtil::joinPath(
                        FileUtil::joinPath( FileUtil::joinPath( resourceRoot, path::kEditorPack ), editor::EditorPaths::kFontsFolderName ), pFileName );
                    if ( FileUtil::isRegularFile( candidate ) )
                        return candidate;
                }

                // 프로젝트 안에 없으면 OS 시스템 폰트 폴더를 직접 찾는다(I/O 지연을 막으려고 재귀 스캔은 하지 않는다)
                for ( const string& fontsDir : SystemFontLocator::getSystemFontDirectories() )
                {
                    string direct = FileUtil::joinPath( fontsDir, pFileName );
                    if ( FileUtil::isRegularFile( direct ) )
                        return string( std::move( direct ) );
                }

                return {};
            }

            /**
             * @brief 우선순위 순서로 나열된 폰트 파일명 목록 중 가장 먼저 존재하는 폰트의 절대 경로를 반환합니다.
             */
            static string resolveFontFile( const vector<string>& listFileName )
            {
                for ( const string& name : listFileName )
                {
                    if ( name.empty() )
                        continue;
                    string found = resolveFontFile( name.c_str() );
                    if ( found.empty() == false )
                        return found;
                }
                return {};
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorFontSetup" );

    bool EditorFontSetup::apply( const EditorToolDefaults& toolDefaults )
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        // FreeType 폰트 로더를 연결한다(앤티에일리어싱 · 힌팅 품질)
        io.Fonts->SetFontLoader( ImGuiFreeType::GetFontLoader() );
        io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

        const EditorToolDefaults& data = toolDefaults;
        float32                   fontSize{ 0.0f };
        float32                   iconScale{ 0.0f };
        if ( EditorFontSetupInternal::readTypography( fontSize, iconScale ) == false )
            return false;
        // 후보가 데이터다. 하나도 없으면 ImGui 기본 글꼴로 대신하지 않고 멈춘다 — 대신하면 한글 · 아이콘이 빈 상자로 그려져도 원인이 로그 한 줄에 묻힌다.
        const string basePath   = EditorFontSetupInternal::resolveFontFile( data._listBaseFont );
        const string koreanPath = EditorFontSetupInternal::resolveFontFile( data._listKoreanFont );
        const string iconPath   = EditorFontSetupInternal::resolveFontFile( EditorPaths::kIconFontFileName );
        if ( basePath.empty() )
            SW_LOG_ERROR( "No base font from editortooldefaults.json _listBaseFont exists under editor/fonts or the OS font folders" );
        if ( koreanPath.empty() )
            SW_LOG_ERROR( "No Korean font from editortooldefaults.json _listKoreanFont exists under editor/fonts or the OS font folders" );
        if ( iconPath.empty() )
            SW_LOG_ERROR( "Editor icon font '%#' not found under editor/fonts", EditorPaths::kIconFontFileName );
        const bool bFontMissing = basePath.empty() || koreanPath.empty() || iconPath.empty();
        if ( bFontMissing )
            return false;

        ImFont* pBaseFont{ nullptr };
        BLOCK( "Base Font" )
        {
            ImFontConfig baseConfig{};
            baseConfig.OversampleH = 1;
            baseConfig.OversampleV = 1;
            // 명시적 크기가 필요하다. MergeMode 는 명시 크기를 쓴다.
            baseConfig.SizePixels = fontSize;
            pBaseFont             = io.Fonts->AddFontFromFileTTF( basePath.c_str(), fontSize, &baseConfig, io.Fonts->GetGlyphRangesDefault() );
            if ( pBaseFont == nullptr )
            {
                SW_LOG_ERROR( "Base font %# could not be loaded", basePath.c_str() );
                return false;
            }
            SW_LOG_TRACE( "Loaded base font: %#", basePath.c_str() );
        }

        BLOCK( "Korean Glyph Merge" )
        {
            ImFontConfig mergeConfig{};
            mergeConfig.MergeMode   = true;
            mergeConfig.OversampleH = 2;
            mergeConfig.OversampleV = 1;
            mergeConfig.PixelSnapH  = true;
            io.Fonts->AddFontFromFileTTF( koreanPath.c_str(), fontSize, &mergeConfig, io.Fonts->GetGlyphRangesKorean() );
            SW_LOG_TRACE( "Merged Korean glyphs from: %#", koreanPath.c_str() );
        }

        BLOCK( "Editor icon font" )
        {
            // ImWchar 가 16 비트일 때만 헤더의 범위 배열을 그대로 넘길 수 있다(IMGUI_USE_WCHAR32 를 켜면 이 단언이 알려 준다).
            static_assert( sizeof( ImWchar ) == sizeof( editoricon::kArrGlyphRange[0] ), "editor icon glyph range must match ImWchar" );
            const float32 iconFontSize = fontSize * iconScale;
            ImFontConfig  iconConfig{};
            iconConfig.MergeMode        = true;
            iconConfig.PixelSnapH       = true;
            iconConfig.GlyphMinAdvanceX = iconFontSize;
            io.Fonts->AddFontFromFileTTF( iconPath.c_str(), iconFontSize, &iconConfig, reinterpret_cast<const ImWchar*>( editoricon::kArrGlyphRange ) );
        }

        io.FontDefault = pBaseFont;
        return true;
    }
} // namespace sw::editor
