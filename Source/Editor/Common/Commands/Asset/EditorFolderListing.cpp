#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorFolderListing.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/EditorStateStore.h"

#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineDefines.h"

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
                                                  const string& resourceRoot )
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

                string relative;
                if ( resourceRoot.empty() == false && FileUtil::tryMakePathUnderRoot( resourceRoot, item._absolutePath, relative ) ) // 대소문자는 OS 규칙
                    item._relativePath = FileUtil::normalizePath( relative );                                                        // 상대 키는 소문자다
                if ( item._relativePath.empty() )
                    item._relativePath = FileUtil::normalizePath( item._name );

                outList.push_back( std::move( item ) );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorFolderListing::collectFolderListing( const AssetPaths& assetPaths, string_view folderAbs, vector<EditorFolderListingEntry>& outList, bool bRecursive,
                                                    const StopToken& stopToken )
    {
        outList.clear();
        if ( folderAbs.empty() || FileUtil::isDirectory( folderAbs ) == false )
            return;

        vector<string> listFolder;
        vector<string> listFile;
        // 표시용 목록이다(폴더는 위에서 확인했다) — 도중에 멈춘 목록은 collect 가 경로와 함께 알리고, 모은 데까지 보인다.
        (void)FileUtil::collectFolders( folderAbs, listFolder, false );                 // 표시용 — 모은 데까지 보인다
        (void)FileUtil::collectFiles( folderAbs, {}, listFile, bRecursive, stopToken ); // 표시용 — 모은 데까지 보인다

        const string& resourceRoot = assetPaths.getRootFolderPath(); // 워커(`EditorFolderListingJob`)에서 돈다 — 부른 쪽이 넘긴 경로 상태다

        for ( const string& folder : listFolder )
        {
            EditorFolderListingInternal::appendFolderListingEntry( outList, folder, true, resourceRoot );
        }
        for ( const string& file : listFile )
        {
            if ( stopToken.isStopRequested() )
                return;
            EditorFolderListingInternal::appendFolderListingEntry( outList, file, false, resourceRoot );
        }
        // 곁 파일(텍스처 형식 변형 · 파생 데이터 — `SW_ASSET_COMPANION`)은 원본 에셋의 일부다 — 따로 보이지 않는다. 보이면 썸네일 · 열기가 이 플랫폼이 못 읽는
        // 형식(ASTC · ETC2 를 데스크톱 D3D12)을 텍스처로 만들다 실패했다(언리얼 콘텐츠 브라우저도 플랫폼 파생 데이터를 에셋으로 보이지 않는다).
        vector<string> listCompanion;
        for ( const EditorFolderListingEntry& entry : outList )
        {
            if ( entry._bIsDirectory == false )
                AssetCompanionRegistry::appendCompanionPaths( entry._relativePath, listCompanion );
        }
        for ( string& companion : listCompanion )
        {
            companion = FileUtil::normalizePath( companion );
        }
        (void)VectorUtil::removeIf( outList, [&listCompanion]( const EditorFolderListingEntry& entry )
        { return entry._bIsDirectory == false && VectorUtil::contains( listCompanion, FileUtil::normalizePath( entry._relativePath ) ); } );
    }

    void EditorFolderListing::collectChildFolders( string_view folderAbs, vector<string>& outList )
    {
        outList.clear();
        ++getEditorState<EditorFolderListingInternal::ScanStats>()._scanCount;
        if ( folderAbs.empty() || FileUtil::isDirectory( folderAbs ) == false )
            return;

        (void)FileUtil::collectFolders( folderAbs, outList, false ); // 표시용 목록 — 도중에 멈춘 것은 collectFolders 가 알리고 모은 데까지 보인다
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
