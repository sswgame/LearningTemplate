#include "pch.h"

#include "Editor/Common/Commands/EditorAssetFileCommands.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Commands/EditorReferenceIndex.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/ResourceUtil.h"

#if defined( SW_PLATFORM_WINDOWS )
    #include "Core/Common/PlatformOsHeaders.h"

    #include <shellapi.h>
#endif

namespace sw::editor
{
    SW_LOG_CALLER( "EditorAssetFileCommands" );

    namespace
    {
        struct EditorAssetFileCommandsInternal
        {
            /** @brief 새 머티리얼이 베끼는 엔진 머티리얼입니다(손으로 지은 머티리얼 XML 은 퍼뮤테이션을 빠뜨리기 쉽다). */
            static constexpr const utf8* kMaterialTemplatePath = "engine/materials/defaultmaterial.material";
            /** @brief 그 머티리얼의 이름 속성입니다. 새 머티리얼은 이 자리를 자기 줄기로 바꾼다. */
            static constexpr const utf8* kMaterialTemplateName = "name=\"DefaultMaterial\"";
            /** @brief `_2` 부터 세다 멈추는 상한입니다. */
            static constexpr uint32 kMaxUniqueSuffix = 10000;

            static bool isNameChar( utf8 ch ) { return ( 'a' <= ch && ch <= 'z' ) || ( '0' <= ch && ch <= '9' ) || ch == '_' || ch == '-' || ch == '.'; }

            static bool isPathChar( utf8 ch )
            {
                const utf8 lower = StringUtil::toLowerChar( ch );
                return isNameChar( lower ) || lower == '/';
            }

            /** @brief 새 에셋 종류의 기본 이름과 글입니다. */
            static bool makeNewAssetText( EditorNewAssetKind kind, string& outStem, string& outSuffix, string& outText )
            {
                switch ( kind )
                {
                    case EditorNewAssetKind::Material:
                    {
                        outStem   = "new_material";
                        outSuffix = ".material";
                        return ResourceUtil::readTextResource( kMaterialTemplatePath, outText );
                    }
                    case EditorNewAssetKind::Scene:
                    {
                        outStem   = "new_scene";
                        outSuffix = ".scene.xml";
                        outText   = "<Scene formatVersion=\"1\" name=\"NewScene\">\n\t<entities>\n\t</entities>\n</Scene>\n";
                        return true;
                    }
                    case EditorNewAssetKind::Prefab:
                    {
                        outStem   = "new_prefab";
                        outSuffix = ".prefab.xml";
                        outText   = "<Prefab formatVersion=\"0\" name=\"NewPrefab\">\n\t<GameObject _schemaVersion=\"0\" _name=\"NewPrefab\" _bActive=\"true\">\n"
                                    "\t\t<_listComponent>\n\t\t</_listComponent>\n\t</GameObject>\n</Prefab>\n";
                        return true;
                    }
                }
                return false;
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
            const bool        bMatched = StringUtil::startsWith( window, oldID, true );
            const bool        bBefore  = offset == 0 || EditorAssetFileCommandsInternal::isPathChar( inoutText[offset - 1] ) == false;
            const size_t      endIndex = offset + oldID.size();
            const bool        bAfter   = endIndex == inoutText.size() || EditorAssetFileCommandsInternal::isPathChar( inoutText[endIndex] ) == false;
            if ( bMatched && bBefore && bAfter )
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

    bool EditorAssetFileCommands::createAsset( string_view folderAbs, EditorNewAssetKind kind, string& outAssetAbs )
    {
        string stem;
        string suffix;
        string text;
        if ( EditorAssetFileCommandsInternal::makeNewAssetText( kind, stem, suffix, text ) == false )
        {
            SW_LOG_ERROR( "Could not read the new asset template" );
            return false;
        }
        outAssetAbs = makeUniquePath( folderAbs, stem, suffix );
        if ( kind == EditorNewAssetKind::Material )
        {
            string uniqueStem;
            string uniqueSuffix;
            splitAssetName( FileUtil::getFileNamePart( outAssetAbs ), uniqueStem, uniqueSuffix );
            (void)replaceReference( text, EditorAssetFileCommandsInternal::kMaterialTemplateName, "name=\"" + uniqueStem + "\"" ); // 이름 속성이 없으면 템플릿 이름 그대로다
        }
        if ( FileUtil::writeTextFile( outAssetAbs, text ) )
        {
            SW_LOG_INFO( "Created '%#'", ResourceUtil::toResourceID( outAssetAbs ) );
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
        outFixedFileCount        = 0;
        const string source      = FileUtil::normalizeSeparators( sourceAbs );
        const string destination = FileUtil::normalizeSeparators( destinationAbs );
        const string oldID       = ResourceUtil::toResourceID( source );
        const string newID       = ResourceUtil::toResourceID( destination );
        if ( oldID.empty() || newID.empty() || oldID == newID )
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

        const string& resourceRoot = ResourceUtil::getRootFolderPath();
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
#if defined( SW_PLATFORM_WINDOWS )
        // 휴지통은 셸 파일 작업으로만 간다(FOF_ALLOWUNDO). 경로 목록은 경로마다 널 하나, 끝에 널 하나 더 — 에셋과 짝 .meta 를 한 번에 보낸다.
        const string   assetPath = FileUtil::normalizeSeparators( absolutePath );
        const string   metaPath  = AssetDatabase::metaPathFor( assetPath );
        vector<string> listPath{ assetPath };
        if ( FileUtil::exists( metaPath ) )
            listPath.push_back( metaPath );
        vector<utf16> listWideChar;
        for ( const string& path : listPath )
        {
            const wstring widePath = StringUtil::utf8ToUtf16( path.c_str() );
            for ( const utf16 ch : widePath )
            {
                listWideChar.push_back( ch == L'/' ? L'\\' : ch );
            }
            listWideChar.push_back( L'\0' );
        }
        listWideChar.push_back( L'\0' );

        SHFILEOPSTRUCTW operation{};
        operation.wFunc    = FO_DELETE;
        operation.pFrom    = listWideChar.data();
        operation.fFlags   = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        const int32 result = SHFileOperationW( &operation );
        if ( result == 0 && operation.fAnyOperationsAborted == FALSE )
        {
            SW_LOG_INFO( "Moved '%#' to the recycle bin", ResourceUtil::toResourceID( assetPath ) );
            return true;
        }
        SW_LOG_ERROR( "Could not move '%#' to the recycle bin (code %#) - it is kept", assetPath, result );
        return false;
#else
        // 휴지통 규약(freedesktop Trash)은 데스크톱마다 다르다. 지운다.
        return AssetDatabase::deleteAssetFile( absolutePath );
#endif
    }
} // namespace sw::editor
