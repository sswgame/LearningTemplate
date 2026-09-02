#include "pch.h"

#include "Editor/Common/EditorUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Config/EditorConfig.h"
#include "Editor/Common/Config/EditorData.h"
#include "Editor/Common/EditorPlaySession.h"
#include "Editor/Common/EditorSessionPolicy.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Resource/ResourceManager.h"

#include <imgui.h>
#include <imgui_freetype.h>
#include <fa_solid_900.h>
#include <IconsFontAwesome6.h>

namespace sw::editor
{
    namespace
    {
        struct EditorUtilInternal
        {
            /**
             * @brief 해당 디렉터리가 실제로 존재하는 경우에만 정규화하여 출력 목록에 추가합니다.
             */
            static void appendIfDirectory( vector<string>& outList, const string& candidate )
            {
                if ( candidate.empty() == false && FileUtil::directoryExists( candidate ) )
                    outList.push_back( FileUtil::normalizeSeparators( candidate ) );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "EditorUtil" );

    /**
     * @brief 현재 운영체제의 기본 시스템 폰트 디렉터리 경로 목록을 반환합니다.
     */
    vector<string> EditorUtil::getSystemFontsDirectories()
    {
        vector<string> listDir;

#if defined( SW_PLATFORM_WINDOWS )
        utf16 windowsDir[constant::kMaxPathSize];
        if ( GetWindowsDirectoryW( reinterpret_cast<LPWSTR>( windowsDir ), constant::kMaxPathSize ) != 0 )
        {
            EditorUtilInternal::appendIfDirectory( listDir, FileUtil::joinPath( StringUtil::utf16ToUtf8( windowsDir ), "Fonts" ) );
        }

#elif defined( SW_PLATFORM_LINUX )
        EditorUtilInternal::appendIfDirectory( listDir, "/usr/share/fonts" );
        EditorUtilInternal::appendIfDirectory( listDir, "/usr/local/share/fonts" );
        if ( const utf8* pHome = std::getenv( "HOME" ) )
        {
            EditorUtilInternal::appendIfDirectory( listDir, FileUtil::joinPath( pHome, ".local/share/fonts" ) );
        }

#elif defined( SW_PLATFORM_MACOS )
        EditorUtilInternal::appendIfDirectory( listDir, "/System/Library/Fonts" );
        EditorUtilInternal::appendIfDirectory( listDir, "/Library/Fonts" );
        if ( const utf8* pHome = std::getenv( "HOME" ) )
        {
            EditorUtilInternal::appendIfDirectory( listDir, FileUtil::joinPath( pHome, "Library/Fonts" ) );
        }
        else
        {
            struct passwd* pPw = getpwuid( getuid() );
            if ( pPw != nullptr && pPw->pw_dir != nullptr )
            {
                EditorUtilInternal::appendIfDirectory( listDir, FileUtil::joinPath( pPw->pw_dir, "Library/Fonts" ) );
            }
        }

#endif
        return listDir;
    }

    /**
     * @brief 폰트 파일 이름을 프로젝트 에디터 폰트 폴더 및 OS 시스템 폰트 디렉터리에서 검색하여 절대 경로를 반환합니다.
     */
    string EditorUtil::resolveFontFile( const utf8* pFileName )
    {
        if ( StringUtil::isNullOrEmpty( pFileName ) )
            return {};

        const EditorData& data       = editor::getEditorData();
        const string&     editorRoot = ResourceUtil::getEditorFolderPath();
        if ( editorRoot.empty() == false )
        {
            const string candidate = FileUtil::joinPath( FileUtil::joinPath( editorRoot, data._fontsFolder ), pFileName );
            if ( FileUtil::fileExists( candidate ) )
                return candidate;
        }

        const string& resourceRoot = ResourceUtil::getRootFolderPath();
        if ( resourceRoot.empty() == false && data._editorFolder.empty() == false )
        {
            const string candidate = FileUtil::joinPath(
                FileUtil::joinPath( FileUtil::joinPath( resourceRoot, data._editorFolder ), data._fontsFolder ),
                pFileName );
            if ( FileUtil::fileExists( candidate ) )
                return candidate;
        }

        // 프로젝트 내부에 없으면 OS 시스템 폰트 폴더 직접 검색 (재귀 스캔 제외하여 I/O 지연 방지)
        for ( const string& fontsDir : getSystemFontsDirectories() )
        {
            string direct = FileUtil::joinPath( fontsDir, pFileName );
            if ( FileUtil::fileExists( direct ) )
                return string( std::move( direct ) );
        }

        return {};
    }

    /**
     * @brief 우선순위 순서로 나열된 폰트 파일명 목록 중 가장 먼저 존재하는 폰트의 절대 경로를 반환합니다.
     */
    string EditorUtil::resolveFontFile( const vector<string>& listFileName )
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

    void EditorUtil::setupFonts()
    {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        // FreeType 폰트 로더 연동 (고속 래스터라이징 및 서브픽셀 앤티에일리어싱/힌팅)
        io.Fonts->SetFontLoader( ImGuiFreeType::GetFontLoader() );
        io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

        const EditorData& data       = editor::getEditorData();
        const string      basePath   = resolveFontFile( data._listBaseFont );
        const string      koreanPath = resolveFontFile( data._listKoreanFont );

        ImFont* pBaseFont{ nullptr };
        BLOCK( "Base Font" )
        {
            ImFontConfig baseConfig{};
            baseConfig.OversampleH = 1;
            baseConfig.OversampleV = 1;
            // 명시적 크기가 필요합니다: MergeMode가 명시 크기를 쓰는데
            // 대상 폰트가 암시적 참조 크기(AddFontDefault)이면 ImGui가 assert합니다.
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

        BLOCK( "Font Awesome 6 (ImGuiNotify icons)" )
        {
            const float32            iconFontSize      = data._fontSize * 2.0f / 3.0f;
            static constexpr ImWchar kArrIconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
            ImFontConfig             iconsConfig{};
            iconsConfig.MergeMode            = true;
            iconsConfig.PixelSnapH           = true;
            iconsConfig.GlyphMinAdvanceX     = iconFontSize;
            iconsConfig.FontDataOwnedByAtlas = false;
            io.Fonts->AddFontFromMemoryTTF( const_cast<void*>( static_cast<const void*>( fa_solid_900 ) ),
                                            sizeof( fa_solid_900 ), iconFontSize, &iconsConfig,
                                            kArrIconsRanges );
        }

        io.FontDefault = pBaseFont;
    }

    string EditorUtil::getProjectRootPath()
    {
        const string& projectRoot = ResourceUtil::getProjectFolderPath();
        if ( projectRoot.empty() == false )
            return projectRoot;

        const string& resourceRoot = ResourceUtil::getRootFolderPath();
        if ( resourceRoot.empty() )
            return {};

        return FileUtil::getDirectoryPart( FileUtil::trimTrailingSlashes( resourceRoot ) );
    }

    string EditorUtil::getEditorConfigDirectory()
    {
        const EditorConfig& editorCfg   = EditorConfig::getActive();
        const string        projectRoot = getProjectRootPath();
        if ( projectRoot.empty() )
            return {};

        const string configDir =
            FileUtil::joinPath( FileUtil::joinPath( projectRoot, editorCfg._configFolder ), editorCfg._editorConfigFolder );
        const string markerFile = FileUtil::joinPath( configDir, editorCfg._imguiIniFile );
        FileUtil::createDirectory( markerFile );
        return configDir;
    }

    string EditorUtil::resolveEditorConfigFile( const utf8* pFileName )
    {
        if ( StringUtil::isNullOrEmpty( pFileName ) )
            return {};

        const string configDir = getEditorConfigDirectory();
        if ( configDir.empty() )
            return {};

        return FileUtil::joinPath( configDir, pFileName );
    }

    bool EditorUtil::isPrefabAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Prefab, pPath );
    }

    bool EditorUtil::isTextureAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Texture, pPath );
    }

    bool EditorUtil::isMaterialAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Material, pPath );
    }

    bool EditorUtil::isSceneAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Scene, pPath );
    }

    bool EditorUtil::isShaderAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Shader, pPath );
    }

    bool EditorUtil::isAudioAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Audio, pPath );
    }

    bool EditorUtil::isDataAssetPath( const utf8* pPath )
    {
        return EditorAssetTypeRegistry::matches( EditorAssetKind::Data, pPath );
    }

    GameObject* EditorUtil::spawnPrefabFromAssetPath( GameObjectManager* pManager, const utf8* pPath, GameObject* pParent )
    {
        if ( pManager == nullptr || StringUtil::isNullOrEmpty( pPath ) )
            return nullptr;

        if ( isPrefabAssetPath( pPath ) == false )
        {
            SW_LOG_TRACE( "Not a prefab path: %#", pPath );
            return nullptr;
        }

        GameObject* pSpawned = editor::getService<ResourceManager>()->getPrefabManager().spawn( pManager, pPath );
        if ( pSpawned == nullptr )
        {
            SW_LOG_WARNING( "Failed to spawn prefab: %#", pPath );
            return nullptr;
        }

        if ( pParent != nullptr )
            pSpawned->attachToParent( pParent );

        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getWorkspace().setGameObjectPrefabPath( pSpawned->getObjectId(), pPath );

        SW_LOG_TRACE( "Spawned prefab from %#", pPath );
        return pSpawned;
    }

    bool EditorUtil::areSceneEditsAllowed()
    {
        return EditorSessionPolicy::areSceneEditsAllowed( EditorPlaySession::isStopped() );
    }
} // namespace sw::editor
