#include "pch.h"

#include "Editor/Common/Commands/EditorAssetFileCommands.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/File/PlatformFileUtil.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/EditorReferenceIndex.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/AssetManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw::editor
{
    SW_LOG_CALLER( "EditorAssetFileCommands" );

    namespace
    {
        struct EditorAssetFileCommandsInternal
        {
            /** @brief 새 씬의 글입니다. */
            static constexpr const utf8* kSceneText = "<Scene formatVersion=\"1\" name=\"NewScene\">\n\t<entities>\n\t</entities>\n</Scene>\n";
            /** @brief 새 프리팹의 글입니다. */
            static constexpr const utf8* kPrefabText = "<Prefab formatVersion=\"0\" name=\"NewPrefab\">\n\t<GameObject _schemaVersion=\"0\" _name=\"NewPrefab\" _bActive=\"true\">\n"
                                                       "\t\t<_listComponent>\n\t\t</_listComponent>\n\t</GameObject>\n</Prefab>\n";
            /**
             * @brief 새 에셋 틀의 표입니다. 한 줄이 Add 메뉴 항목 하나입니다.
             * @details 머티리얼은 엔진 머티리얼을 베낀다(손으로 지은 머티리얼 XML 은 퍼뮤테이션을 빠뜨리기 쉽다).
             */
            static constexpr EditorNewAssetTemplate kArrNewAssetTemplate[] = {
                {"material", "Material", "new_material",   ".material", "engine/materials/defaultmaterial.material",     nullptr, "name=\"DefaultMaterial\""},
                {   "scene",    "Scene",    "new_scene",  ".scene.xml",                                     nullptr,  kSceneText,                    nullptr},
                {  "prefab",   "Prefab",   "new_prefab", ".prefab.xml",                                     nullptr, kPrefabText,                    nullptr},
            };
            /** @brief `_2` 부터 세다 멈추는 상한입니다. */
            static constexpr uint32 kMaxUniqueSuffix = 10000;

            static bool isNameChar( utf8 ch ) { return ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '-' || ch == '.'; }

            static bool isPathChar( utf8 ch )
            {
                const utf8 lower = StringUtil::toLowerChar( ch );
                return isNameChar( lower ) || lower == '/';
            }

            /** @brief 파일 하나를 옮깁니다(복사한 뒤 원본을 지운다). 실패하면 대상을 지워 원본만 남긴다. */
            [[nodiscard]] static bool moveFile( const string& sourceAbs, const string& destinationAbs )
            {
                if ( FileUtil::copyFile( sourceAbs, destinationAbs ) == false )
                    return false;
                if ( FileUtil::removeFile( sourceAbs ) )
                    return true;
                (void)FileUtil::tryRemoveFile( destinationAbs ); // 원본이 남았으니 사본을 거둔다 — 실패해도 원본은 그대로다
                return false;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    bool EditorAssetFileCommands::isValidAssetName( string_view name )
    {
        if ( name.empty() || name.front() == '.' )
            return false;
        for ( const utf8 ch : name )
        {
            if ( EditorAssetFileCommandsInternal::isNameChar( ch ) == false )
                return false;
        }
        return true;
    }

    void EditorAssetFileCommands::splitAssetName( string_view fileName, string& outStem, string& outSuffix )
    {
        const size_t dot = fileName.find( '.' );
        if ( dot == string_view::npos || dot == 0 )
        {
            outStem = string{ fileName };
            outSuffix.clear();
            return;
        }
        outStem   = string{ fileName.substr( 0, dot ) };
        outSuffix = string{ fileName.substr( dot ) };
    }

    string EditorAssetFileCommands::makeUniquePath( string_view folderAbs, string_view stem, string_view suffix )
    {
        string                                 candidate = FileUtil::joinPath( folderAbs, string( stem ) + string( suffix ) );
        StringBuilder<constant::kMaxBuffer512> name;
        for ( uint32 nameSuffix = 2; FileUtil::exists( candidate ) && nameSuffix < EditorAssetFileCommandsInternal::kMaxUniqueSuffix; ++nameSuffix )
        {
            name.clear();
            name.append( stem ).append( '_' ).append( nameSuffix ).append( suffix );
            candidate = FileUtil::joinPath( folderAbs, name.view() );
        }
        return candidate;
    }

    uint32 EditorAssetFileCommands::replaceReference( string& inoutText, string_view oldID, string_view newID )
    {
        if ( oldID.empty() )
            return 0;
        uint32 replaceCount{ 0 };
        size_t offset{ 0 };
        while ( offset + oldID.size() <= inoutText.size() )
        {
            const string_view window{ inoutText.data() + offset, oldID.size() };
            const bool        bMatched          = StringUtil::startsWith( window, oldID, true );
            const bool        bBefore           = offset == 0 || EditorAssetFileCommandsInternal::isPathChar( inoutText[offset - 1] ) == false;
            const size_t      endIndex          = offset + oldID.size();
            const bool        bAfter            = endIndex == inoutText.size() || EditorAssetFileCommandsInternal::isPathChar( inoutText[endIndex] ) == false;
            const bool        bReplaceReference = bMatched && bBefore && bAfter;
            if ( bReplaceReference )
            {
                inoutText.replace( offset, oldID.size(), newID.data(), newID.size() );
                offset += newID.size();
                ++replaceCount;
                continue;
            }
            ++offset;
        }
        return replaceCount;
    }

    bool EditorAssetFileCommands::createFolder( string_view parentAbs, string& outFolderAbs )
    {
        outFolderAbs = makeUniquePath( parentAbs, "new_folder", "" );
        if ( FileUtil::ensureDirectoryExists( outFolderAbs ) )
            return true;
        SW_LOG_ERROR( "Could not create the folder '%#'", outFolderAbs );
        return false;
    }

    span<const EditorNewAssetTemplate> EditorAssetFileCommands::getNewAssetTemplateList()
    {
        return EditorAssetFileCommandsInternal::kArrNewAssetTemplate;
    }

    bool EditorAssetFileCommands::createAsset( string_view folderAbs, const EditorNewAssetTemplate& assetTemplate, string& outAssetAbs )
    {
        string text;
        if ( assetTemplate._pSourcePath == nullptr )
        {
            text = assetTemplate._pText;
        }
        else if ( ResourceIO::readTextResource( assetTemplate._pSourcePath, text ) == false )
        {
            SW_LOG_ERROR( "Could not read the new asset template '%#'", assetTemplate._pSourcePath );
            return false;
        }
        outAssetAbs = makeUniquePath( folderAbs, assetTemplate._pStem, assetTemplate._pSuffix );
        if ( assetTemplate._pNameAttribute != nullptr )
        {
            string uniqueStem;
            string uniqueSuffix;
            splitAssetName( FileUtil::getFileNamePart( outAssetAbs ), uniqueStem, uniqueSuffix );
            (void)replaceReference( text, assetTemplate._pNameAttribute, "name=\"" + uniqueStem + "\"" ); // 이름 속성이 없으면 템플릿 이름 그대로다
        }
        if ( FileUtil::writeTextFile( outAssetAbs, text ) )
        {
            SW_LOG_INFO( "Created '%#'", ResourcePaths::toResourceID( outAssetAbs ) );
            return true;
        }
        SW_LOG_ERROR( "Could not write the new asset '%#'", outAssetAbs );
        return false;
    }

    bool EditorAssetFileCommands::duplicateAsset( string_view sourceAbs, string& outAssetAbs )
    {
        string stem;
        string suffix;
        splitAssetName( FileUtil::getFileNamePart( sourceAbs ), stem, suffix );
        outAssetAbs = makeUniquePath( FileUtil::getDirectoryPart( sourceAbs ), stem, suffix );
        return FileUtil::copyFile( sourceAbs, outAssetAbs );
    }

    bool EditorAssetFileCommands::moveAsset( string_view sourceAbs, string_view destinationAbs, EditorReferenceIndex& inoutIndex, uint32& outFixedFileCount )
    {
        outFixedFileCount                = 0;
        const string source              = FileUtil::normalizeSeparators( sourceAbs );
        const string destination         = FileUtil::normalizeSeparators( destinationAbs );
        const string oldID               = ResourcePaths::toResourceID( source );
        const string newID               = ResourcePaths::toResourceID( destination );
        const bool   bInvalidResourceIDs = oldID.empty() || newID.empty() || oldID == newID;
        if ( bInvalidResourceIDs )
            return false;
        if ( isValidAssetName( FileUtil::getFileNamePart( destination ) ) == false )
        {
            SW_LOG_WARNING( "'%#' is not a resource name - use lowercase letters, digits, '_', '-' and '.'", FileUtil::getFileNamePart( destination ) );
            return false;
        }
        if ( FileUtil::exists( destination ) )
        {
            SW_LOG_WARNING( "'%#' already exists - nothing was moved", newID );
            return false;
        }
        if ( inoutIndex.isReady() == false )
        {
            SW_LOG_WARNING( "The reference index is still building - try again in a moment" );
            return false;
        }

        vector<EditorAssetReference> listReference;
        inoutIndex.findReferrers( oldID, listReference );
        if ( EditorAssetFileCommandsInternal::moveFile( source, destination ) == false )
            return false;

        // 짝 .meta 는 GUID 가 이어지게 같이 옮기고, 안의 sourcePath 도 새 id 로 고친다.
        const string sourceMeta = AssetDatabase::metaPathFor( source );
        if ( FileUtil::exists( sourceMeta ) )
        {
            string metaText;
            if ( FileUtil::readTextFile( sourceMeta, metaText ) )
            {
                (void)replaceReference( metaText, oldID, newID ); // 경로를 적지 않은 .meta 는 그대로 옮긴다
                if ( FileUtil::writeTextFile( AssetDatabase::metaPathFor( destination ), metaText ) == false || FileUtil::removeFile( sourceMeta ) == false )
                    SW_LOG_WARNING( "Moved '%#' but its .meta could not follow", newID );
            }
        }

        const string& resourceRoot = ResourcePaths::getRootFolderPath();
        inoutIndex.refreshFile( resourceRoot, oldID );
        inoutIndex.refreshFile( resourceRoot, newID );

        // 참조자는 파일마다 한 번 고친다(한 파일이 여러 줄에서 적을 수 있다).
        const string* pPreviousReferrer = nullptr;
        for ( const EditorAssetReference& reference : listReference )
        {
            if ( pPreviousReferrer != nullptr && *pPreviousReferrer == reference._referrerPath )
                continue;
            pPreviousReferrer        = &reference._referrerPath;
            const string referrerID  = reference._referrerPath == oldID ? newID : reference._referrerPath;
            const string referrerAbs = FileUtil::joinPath( resourceRoot, referrerID );
            string       referrerText;
            if ( FileUtil::readTextFile( referrerAbs, referrerText ) == false )
                continue;
            if ( replaceReference( referrerText, oldID, newID ) == 0 )
                continue;
            if ( FileUtil::writeTextFile( referrerAbs, referrerText ) == false )
            {
                SW_LOG_WARNING( "Could not fix the reference in '%#' - it still names '%#'", referrerID, oldID );
                continue;
            }
            ++outFixedFileCount;
            inoutIndex.refreshFile( resourceRoot, referrerID );
        }
        SW_LOG_INFO( "Moved '%#' to '%#' and fixed %# referencing file(s)", oldID, newID, outFixedFileCount );
        return true;
    }

    bool EditorAssetFileCommands::moveToTrash( string_view absolutePath )
    {
        // 휴지통이 없는 플랫폼(Linux)은 지운다.
        if ( PlatformFileUtil::hasRecycleBin() == false )
            return AssetDatabase::deleteAssetFile( absolutePath );

        // 에셋과 짝 .meta 를 한 번에 보낸다.
        const string   assetPath = FileUtil::normalizeSeparators( absolutePath );
        const string   metaPath  = AssetDatabase::metaPathFor( assetPath );
        vector<string> listPath{ assetPath };
        if ( FileUtil::exists( metaPath ) )
            listPath.push_back( metaPath );
        if ( PlatformFileUtil::moveToRecycleBin( listPath ) )
        {
            SW_LOG_INFO( "Moved '%#' to the recycle bin", ResourcePaths::toResourceID( assetPath ) );
            return true;
        }
        SW_LOG_ERROR( "Could not move '%#' to the recycle bin - it is kept", assetPath );
        return false;
    }

    bool EditorAssetFileCommands::deleteAsset( string_view absolutePath )
    {
        return AssetDatabase::deleteAssetFile( absolutePath );
    }

    uint32 EditorAssetFileCommands::importFiles( string_view destFolderAbs, const vector<string>& listSourcePath )
    {
        if ( destFolderAbs.empty() )
        {
            SW_LOG_WARNING( "Import cancelled — no destination folder." );
            return 0;
        }

        uint32 copied{ 0 };
        for ( const string& sourcePath : listSourcePath )
        {
            if ( FileUtil::exists( sourcePath ) == false )
            {
                SW_LOG_WARNING( "Import skipped (missing): %#", sourcePath.c_str() );
                continue;
            }

            const string fileName = FileUtil::getFileNamePart( sourcePath );

            if ( FileUtil::pathsEqualNormalized( sourcePath, ResourcePaths::makeSavePath( destFolderAbs, fileName ) ) )
            {
                SW_LOG_TRACE( "Already in folder: %#", fileName.c_str() );
                continue;
            }

            // 이미 있는 애셋을 덮어쓰지 않는다. 같은 이름의 파일을 드롭해 폴더에 있던 것을 덮으면 아무 말 없이 사라지고,
            // 에디터의 임포트에는 되돌리기가 없으므로 그대로 잃는다.
            // "원래 있던 것과 같은 파일인가" 는 바로 위에서 경로로 이미 걸렀으므로, 여기까지 온 것은 다른 파일인데 이름만 같은
            // 경우다.
            const string destPath = ResourcePaths::makeUniqueSavePath( destFolderAbs, fileName );

            FileUtil::ensureParentDirectoryExists( destPath );
            if ( FileUtil::copyFile( sourcePath, destPath ) == false )
            {
                SW_LOG_ERROR( "Failed to import: %#", sourcePath.c_str() );
                continue;
            }

            ++copied;
            const string rel = AssetDatabase::toRelativePath( destPath );
            if ( rel.empty() == false )
            {
                AssetManager* pResources = editor::findSubsystem<AssetManager>();
                if ( pResources != nullptr )
                    pResources->getAssetDatabase().ensureMeta( rel, true );
            }
            SW_LOG_TRACE( "Imported: %# -> %#", sourcePath.c_str(), destPath.c_str() );
        }
        return copied;
    }

    bool EditorAssetFileCommands::showInFileExplorer( string_view absolutePath )
    {
        if ( absolutePath.empty() )
            return false;

        const string path = FileUtil::normalizeSeparators( absolutePath );
        string       command;
#if defined( SW_PLATFORM_WINDOWS )
        // explorer 는 백슬래시만 받는다. 슬래시를 주면 선택이 안 되고 내 문서를 연다.
        string windowsPath = path;
        for ( utf8& ch : windowsPath )
        {
            if ( ch == '/' )
                ch = '\\';
        }
        command = "explorer.exe /select,\"" + windowsPath + "\"";
#else
        // 리눅스 파일 관리자에는 "선택한 채로 열기" 표준이 없다. 그래서 폴더까지만 연다.
        command = "xdg-open \"" + FileUtil::getDirectoryPart( path ) + "\"";
#endif

        // 실행하고 기다리지 않는다. `Process::execute` 로 기다리면 UI 스레드가 탐색기(또는 탐색기가 띄운 창)가 출력 파이프를 놓을
        // 때까지 멈추고, `explorer.exe /select,` 는 성공해도 종료 코드 1 을 반환한다. 알 수 있는 실패는 "실행하지 못했다" 뿐이다.
        if ( Process::launchDetached( command ) == false )
        {
            SW_LOG_WARNING( "Failed to open file explorer for %#", path.c_str() );
            return false;
        }
        return true;
    }
} // namespace sw::editor
