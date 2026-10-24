#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorFolderListing.h"

#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Common/EngineDefines.h"
#include "Engine/Resource/ResourcePaths.h"

namespace sw::editor
{
    namespace
    {
        struct EditorFolderListingInternal
        {
            /** @brief 폴더 읽기 횟수의 상태입니다. 에디터 범위의 상태 저장소가 소유해 에디터마다(테스트마다) 따로 센다. */
            struct ScanStats
            {
                atomic<uint64> _scanCount{ 0 }; ///< `collectChildFolders` 가 디스크를 읽은 횟수. `getChildFolderScanCount` 가 반환한다
            };

            static void appendFolderListingEntry( vector<EditorFolderListingEntry>& outList, const string& path, bool bIsDirectory,
                                                  const string& rootNorm )
            {
                EditorFolderListingEntry item;
                item._absolutePath = FileUtil::normalizeSeparators( path );
                item._name         = FileUtil::getFileNamePart( item._absolutePath );
                item._bIsDirectory = bIsDirectory;
                if ( item._bIsDirectory == false )
                {
                    item._extension = FileUtil::getExtension( item._name );
                    item._bReadOnly = FileUtil::isReadOnlyFile( item._absolutePath );
                }

                if ( rootNorm.empty() == false )
                {
                    const string absNorm = FileUtil::normalizePath( item._absolutePath );
                    if ( FileUtil::startsWithPathComponent( absNorm, rootNorm ) )
                        item._relativePath = FileUtil::suffixAfterPathComponent( absNorm, rootNorm );
                }
                if ( item._relativePath.empty() )
                    item._relativePath = FileUtil::normalizePath( item._name );

                if ( item._bIsDirectory == false && FileUtil::hasExtension( item._name, path::kMetaExtension ) )
                    return;

                outList.push_back( std::move( item ) );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorFolderListing::collectFolderListing( string_view folderAbs, vector<EditorFolderListingEntry>& outList, bool bRecursive )
    {
        outList.clear();
        if ( folderAbs.empty() || FileUtil::isDirectory( folderAbs ) == false )
            return;

        vector<string> listFolder;
        vector<string> listFile;
        FileUtil::collectFolders( folderAbs, listFolder, false );
        FileUtil::collectFiles( folderAbs, {}, listFile, bRecursive );

        const string& resourceRoot = ResourcePaths::getRootFolderPath();
        const string  rootNorm     = resourceRoot.empty() ? string{} : FileUtil::normalizePath( resourceRoot );

        for ( const string& folder : listFolder )
        {
            EditorFolderListingInternal::appendFolderListingEntry( outList, folder, true, rootNorm );
        }
        for ( const string& file : listFile )
        {
            EditorFolderListingInternal::appendFolderListingEntry( outList, file, false, rootNorm );
        }
    }

    void EditorFolderListing::collectChildFolders( string_view folderAbs, vector<string>& outList )
    {
        outList.clear();
        ++getEditorState<EditorFolderListingInternal::ScanStats>()._scanCount;
        if ( folderAbs.empty() || FileUtil::isDirectory( folderAbs ) == false )
            return;

        FileUtil::collectFolders( folderAbs, outList, false );
        for ( string& child : outList )
        {
            child = FileUtil::normalizeSeparators( child );
        }
    }

    uint64 EditorFolderListing::getChildFolderScanCount()
    {
        return getEditorState<EditorFolderListingInternal::ScanStats>()._scanCount.load();
    }
} // namespace sw::editor
