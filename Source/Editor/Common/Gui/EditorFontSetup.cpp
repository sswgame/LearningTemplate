#include "pch.h"

#include "Editor/Common/Gui/EditorFontSetup.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/EditorUtil.h"
#include "Editor/Common/Gui/EditorIconGlyphs.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Resource/ResourceUtil.h"
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
             * @brief 아이콘 폰트 크기 ÷ 본문 폰트 크기입니다.
             * @details 아이콘은 24 격자의 가운데 20 칸에 그려져 있어 em 의 5/6 을 차지합니다. 0.8 이면 아이콘 높이가 본문 크기의 2/3 쯤입니다.
             */
            static constexpr float32 kIconFontScale = 0.8f;

            /**
             * @brief 폰트 파일 이름을 프로젝트 에디터 폰트 폴더 및 OS 시스템 폰트 디렉터리에서 검색하여 절대 경로를 반환합니다.
             */
            static string resolveFontFile( const utf8* pFileName )
            {
                if ( StringUtil::isNullOrEmpty( pFileName ) )
                    return {};

                const string editorRoot = ResourceUtil::getDomainFolderPath( path::kEditorPack );
                if ( editorRoot.empty() == false )
                {
                    const string candidate = FileUtil::joinPath( FileUtil::joinPath( editorRoot, editor::EditorUtil::kFontsFolderName ), pFileName );
                    if ( FileUtil::isRegularFile( candidate ) )
                        return candidate;
                }

                const string& resourceRoot = ResourceUtil::getRootFolderPath();
                if ( resourceRoot.empty() == false )
                {
                    const string candidate = FileUtil::joinPath(
                        FileUtil::joinPath( FileUtil::joinPath( resourceRoot, path::kEditorPack ), editor::EditorUtil::kFontsFolderName ), pFileName );
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

    void EditorFontSetup::apply()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        // FreeType 폰트 로더를 연결한다(앤티에일리어싱 · 힌팅 품질)
        io.Fonts->SetFontLoader( ImGuiFreeType::GetFontLoader() );
        io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

        const EditorToolDefaults& data       = editor::getEditorToolDefaults();
        const string              basePath   = EditorFontSetupInternal::resolveFontFile( data._listBaseFont );
        const string              koreanPath = EditorFontSetupInternal::resolveFontFile( data._listKoreanFont );

        ImFont* pBaseFont{ nullptr };
        BLOCK( "Base Font" )
        {
            ImFontConfig baseConfig{};
            baseConfig.OversampleH = 1;
            baseConfig.OversampleV = 1;
            // 명시적 크기가 필요하다. MergeMode 는 명시 크기를 쓰는데,
            // 대상 폰트가 암시적 참조 크기(AddFontDefault)이면 ImGui 가 assert 한다.
            baseConfig.SizePixels = data._fontSize;

            if ( basePath.empty() == false )
            {
                pBaseFont = io.Fonts->AddFontFromFileTTF( basePath.c_str(), data._fontSize, &baseConfig,
                                                          io.Fonts->GetGlyphRangesDefault() );
                SW_LOG_TRACE( "Loaded base font: %#", basePath.c_str() );
            }

            if ( pBaseFont == nullptr )
            {
                pBaseFont = io.Fonts->AddFontDefault( &baseConfig );
                SW_LOG_WARNING( "No system UI font found - using ImGui default font." );
            }
        }

        BLOCK( "Korean Glyph Merge" )
        {
            if ( koreanPath.empty() == false )
            {
                ImFontConfig mergeConfig{};
                mergeConfig.MergeMode   = true;
                mergeConfig.OversampleH = 2;
                mergeConfig.OversampleV = 1;
                mergeConfig.PixelSnapH  = true;
                io.Fonts->AddFontFromFileTTF( koreanPath.c_str(), data._fontSize, &mergeConfig,
                                              io.Fonts->GetGlyphRangesKorean() );
                SW_LOG_TRACE( "Merged Korean glyphs from: %#", koreanPath.c_str() );
            }
            else
                SW_LOG_WARNING( "Korean font not found - Hangul may not render." );
        }

        BLOCK( "Editor icon font" )
        {
            // ImWchar 가 16 비트일 때만 헤더의 범위 배열을 그대로 넘길 수 있다(IMGUI_USE_WCHAR32 를 켜면 이 단언이 알려 준다).
            static_assert( sizeof( ImWchar ) == sizeof( editoricon::kArrGlyphRange[0] ), "editor icon glyph range must match ImWchar" );
            const string iconPath = EditorFontSetupInternal::resolveFontFile( EditorUtil::kIconFontFileName );
            if ( iconPath.empty() )
            {
                SW_LOG_ERROR( "Editor icon font '%#' not found under editor/fonts - icons render as empty boxes", EditorUtil::kIconFontFileName );
            }
            else
            {
                const float32 iconFontSize = data._fontSize * EditorFontSetupInternal::kIconFontScale;
                ImFontConfig  iconConfig{};
                iconConfig.MergeMode        = true;
                iconConfig.PixelSnapH       = true;
                iconConfig.GlyphMinAdvanceX = iconFontSize;
                io.Fonts->AddFontFromFileTTF( iconPath.c_str(), iconFontSize, &iconConfig,
                                              reinterpret_cast<const ImWchar*>( editoricon::kArrGlyphRange ) );
            }
        }

        io.FontDefault = pBaseFont;
    }
} // namespace sw::editor
