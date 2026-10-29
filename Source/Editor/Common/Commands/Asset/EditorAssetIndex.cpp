#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorAssetIndex.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/FileSystem/AssetPaths.h"

namespace sw::editor
{
    bool EditorAssetIndex::classifyFile( string_view filePath, EditorAssetIndexEntry& outEntry )
    {
        const EditorAssetType      kind  = EditorAssetTypeRegistry::findKind( filePath );
        const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
        if ( pInfo == nullptr )
            return false;
        const string assetID = AssetPaths::toAssetID( filePath );
        if ( assetID.empty() )
            return false;

        outEntry._kind     = kind;
        outEntry._category = pInfo->_pDisplayName;
        outEntry._title    = FileUtil::getFileNamePart( filePath );
        outEntry._detail   = assetID;
        outEntry._path     = assetID;
        return true;
    }

    void EditorAssetIndex::collectEntries( vector<EditorAssetIndexEntry>& outList, const StopToken& stopToken )
    {
        outList.clear();

        const string& resourceFolder = AssetPaths::getRootFolderPath();
        if ( resourceFolder.empty() )
            return;

        vector<string> listAllFile;
        // 검색용 색인이다 — 도중에 멈춘 목록은 collectFiles 가 경로와 함께 알리고, 모은 데까지 검색된다(다음 색인이 다시 훑는다).
        (void)FileUtil::collectFiles( resourceFolder, "", listAllFile, true, stopToken ); // 종료 신호가 오르면 목록이 비거나 짧다 — 부른 잡이 신호를 다시 읽고 버린다

        outList.reserve( listAllFile.size() );
        for ( const string& file : listAllFile )
        {
            if ( stopToken.isStopRequested() )
                return;
            EditorAssetIndexEntry entry{};
            if ( classifyFile( file, entry ) )
                outList.push_back( std::move( entry ) );
        }
    }

    void EditorAssetIndex::countFiles( const vector<string>& listFilePath, EditorResourceCatalogCounts& outCounts )
    {
        uint32                           kindCount{ 0 };
        const EditorAssetTypeInfo* const pInfo = EditorAssetTypeRegistry::getKindInfos( kindCount );
        outCounts._listKindCount.clear();
        outCounts._listKindCount.reserve( kindCount );
        for ( uint32 index = 0; index < kindCount; ++index )
        {
            outCounts._listKindCount.push_back( EditorResourceCatalogCount{ pInfo[index]._kind, pInfo[index]._pBrowserLabel, 0 } );
        }

        for ( const string& filePath : listFilePath )
        {
            const EditorAssetType kind = EditorAssetTypeRegistry::findKind( filePath );
            for ( EditorResourceCatalogCount& row : outCounts._listKindCount )
            {
                if ( row._kind != kind )
                    continue;
                ++row._count;
                break;
            }
        }
    }

    void EditorAssetIndex::collectCatalogCounts( EditorResourceCatalogCounts& outCounts, const StopToken& stopToken )
    {
        vector<string> listAllFile;
        const string&  resourceRootPath = AssetPaths::getRootFolderPath();
        // 리소스 루트가 없거나 그새 사라졌으면 모든 종류가 0 개다.
        if ( resourceRootPath.empty() == false )
            (void)FileUtil::collectFiles( resourceRootPath, "", listAllFile, true, stopToken ); // 표시용 개수 — 도중에 멈춘 것은 collectFiles 가 알리고 모은 데까지 센다
        countFiles( listAllFile, outCounts );
    }
} // namespace sw::editor
