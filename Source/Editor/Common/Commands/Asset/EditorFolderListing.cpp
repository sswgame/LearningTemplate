#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorFolderListing.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"

#include "Editor/Common/Asset/EditorContentScan.h"
#include "Editor/Common/Workspace/EditorService.h"
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

            static void appendFolderListingEntry( vector<EditorFolderListingEntry>& outList, const AbsolutePath& path, bool bIsDirectory, const AssetPaths& assetPaths )
            {
                EditorFolderListingEntry item;
                item._absolutePath = path;
                item._name         = FileUtil::getFileNamePart( item._absolutePath.str() );
                item._bIsDirectory = bIsDirectory;
                if ( item._bIsDirectory == false )
                {
                    item._extension = FileUtil::getExtension( item._name );
                    item._bReadOnly = FileUtil::isReadOnlyFile( item._absolutePath );
                }

                // 리소스 id(소문자) — 리소스 루트 아래와 작업 도메인(실행 뿌리 아래 `scratch/`)을 한 규칙으로 푼다(`AssetPaths::toAssetID`, 대소문자는 OS 규칙).
                item._relativePath = assetPaths.toAssetID( item._absolutePath );
                if ( item._relativePath.empty() )
                    item._relativePath = FileUtil::normalizePath( item._name ).str();

                outList.push_back( std::move( item ) );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorFolderListing::collectFolderListing( const AssetPaths& assetPaths, const AbsolutePath& folderAbs, vector<EditorFolderListingEntry>& outList, bool bRecursive,
                                                    const StopToken& stopToken )
    {
        outList.clear();
        if ( folderAbs.empty() || FileUtil::isDirectory( folderAbs ) == false )
            return;

        // 워커(`EditorFolderListingJob`)에서 돈다 — 경로 상태는 부른 쪽이 넘긴 @p assetPaths 다(읽기만 한다).
        vector<AbsolutePath> listFolder;
        vector<AbsolutePath> listFile;
        // 표시용 목록이다(폴더는 위에서 확인했다) — 도중에 멈춘 목록은 collect 가 경로와 함께 알리고, 모은 데까지 보인다.
        (void)EditorContentScan::collectFolders( folderAbs, listFolder, false );
        (void)EditorContentScan::collectFiles( folderAbs, {}, listFile, bRecursive, stopToken );

        for ( const AbsolutePath& folder : listFolder )
        {
            EditorFolderListingInternal::appendFolderListingEntry( outList, folder, true, assetPaths );
        }
        for ( const AbsolutePath& file : listFile )
        {
            if ( stopToken.isStopRequested() )
                return;
            EditorFolderListingInternal::appendFolderListingEntry( outList, file, false, assetPaths );
        }
        // 곁 파일(텍스처 형식 변형 · 파생 데이터, `SW_ASSET_COMPANION`)은 원본 에셋의 일부라 따로 보이지 않는다. 보이면 썸네일 · 열기가 이 플랫폼이 못 읽는
        // 형식(ASTC · ETC2 를 데스크톱 D3D12)을 텍스처로 만들다 실패했다(언리얼 콘텐츠 브라우저도 플랫폼 파생 데이터를 에셋으로 보이지 않는다).
        vector<string> listCompanion;
        for ( const EditorFolderListingEntry& entry : outList )
        {
            if ( entry._bIsDirectory == false )
                AssetCompanionRule::appendCompanionPaths( entry._relativePath, listCompanion );
        }
        unordered_set<string> uniqueCompanion; // 파일마다 곁 파일 목록을 훑지 않는다(폴더 파일 수 × 곁 파일 후보 수)
        for ( const string& companion : listCompanion )
        {
            uniqueCompanion.insert( FileUtil::normalizePath( companion ).str() );
        }
        (void)VectorUtil::removeIf( outList, [&uniqueCompanion]( const EditorFolderListingEntry& entry )
        { return entry._bIsDirectory == false && uniqueCompanion.contains( FileUtil::normalizePath( entry._relativePath ).str() ); } );
    }

    void EditorFolderListing::collectChildFolders( const AbsolutePath& folderAbs, vector<AbsolutePath>& outList )
    {
        outList.clear();
        ++getEditorState<EditorFolderListingInternal::ScanStats>()._scanCount;
        if ( folderAbs.empty() || FileUtil::isDirectory( folderAbs ) == false )
            return;

        // 표시용 목록 — 도중에 멈춘 것은 collectFolders 가 알리고 모은 데까지 보인다.
        (void)EditorContentScan::collectFolders( folderAbs, outList, false );
    }

    uint64 EditorFolderListing::getChildFolderScanCount()
    {
        return getEditorState<EditorFolderListingInternal::ScanStats>()._scanCount.load();
    }
} // namespace sw::editor
