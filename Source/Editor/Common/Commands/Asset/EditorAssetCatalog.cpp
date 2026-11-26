#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorAssetCatalog.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Asset/EditorAssetIndex.h"
#include "Editor/Common/Asset/EditorContentScan.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSubsystems.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"

#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    bool EditorAssetCatalog::classifyFile( const AssetPaths& assetPaths, const AbsolutePath& filePath, EditorAssetIndexEntry& outEntry )
    {
        const EditorAssetTypeRegistration* pInfo = EditorAssetTypeRegistry::findKindInfoForPath( filePath.str() );
        if ( pInfo == nullptr )
            return false;
        const string assetID = assetPaths.toAssetID( filePath );
        if ( assetID.empty() )
            return false;

        outEntry._kind     = pInfo->getType();
        outEntry._category = pInfo->_pDisplayName;
        outEntry._title    = FileUtil::getFileNamePart( filePath.str() );
        outEntry._detail   = assetID;
        outEntry._path     = assetID;
        return true;
    }

    void EditorAssetCatalog::collectEntries( const AssetPaths& assetPaths, vector<EditorAssetIndexEntry>& outList, const StopToken& stopToken )
    {
        outList.clear();

        const AbsolutePath resourceFolder = assetPaths.getRootFolderPath();
        if ( resourceFolder.empty() )
            return;

        vector<AbsolutePath> listAllFile;
        // 검색용 색인이다 — 도중에 멈춘 목록은 collectFiles 가 경로와 함께 알리고, 모은 데까지 검색된다(다음 색인이 다시 훑는다).
        // 종료 신호가 오르면 목록이 비거나 짧다 — 부른 잡이 신호를 다시 읽고 버린다. 옛 작업 도메인 자리의 찌꺼기는 빠진다(EditorContentScan)
        (void)EditorContentScan::collectFiles( resourceFolder, "", listAllFile, true, stopToken );

        outList.reserve( listAllFile.size() );
        for ( const AbsolutePath& file : listAllFile )
        {
            if ( stopToken.isStopRequested() )
                return;
            EditorAssetIndexEntry entry{};
            if ( classifyFile( assetPaths, file, entry ) )
                outList.push_back( std::move( entry ) );
        }
    }

    void EditorAssetCatalog::collectEntries( const AssetPaths& assetPaths, const EditorAssetIndex& index, vector<EditorAssetIndexEntry>& outList )
    {
        outList.clear();
        const AbsolutePath& resourceFolder = assetPaths.getRootFolderPath();
        if ( resourceFolder.empty() )
            return;
        // 디스크 표기(대소문자 그대로)로 짓는다 — 제목 · 경로가 트리 걷기와 같게 보인다. 색인의 열쇠(소문자)는 쓰지 않는다.
        vector<string> listDisplayPath;
        index.collectDisplayPaths( listDisplayPath );
        outList.reserve( listDisplayPath.size() );
        for ( const string& displayPath : listDisplayPath )
        {
            EditorAssetIndexEntry entry{};
            if ( classifyFile( assetPaths, resourceFolder.join( displayPath ), entry ) )
                outList.push_back( std::move( entry ) );
        }
    }

    const EditorAssetIndex* EditorAssetCatalog::findReadyIndex()
    {
        EditorContext* const pContext = editor::findSubsystem<EditorContext>();
        if ( pContext == nullptr )
            return nullptr;
        const EditorAssetIndex& index = pContext->getSubsystem<AssetHotReload>().getReferenceIndex();
        return index.isReady() ? &index : nullptr;
    }

    void EditorAssetCatalog::collectEntriesOnUIThread( const AssetPaths& assetPaths, vector<EditorAssetIndexEntry>& outList )
    {
        const EditorAssetIndex* const pIndex = findReadyIndex();
        if ( pIndex != nullptr )
            collectEntries( assetPaths, *pIndex, outList );
        else
            collectEntries( assetPaths, outList ); // 첫 전체 스캔 전 — 한 번 걷는다
    }

    void EditorAssetCatalog::collectCatalogCounts( const AssetPaths& assetPaths, const EditorAssetIndex& index, EditorResourceCatalogCounts& outCounts )
    {
        vector<string> listDisplayPath;
        index.collectDisplayPaths( listDisplayPath );
        const AbsolutePath&  resourceFolder = assetPaths.getRootFolderPath();
        vector<AbsolutePath> listFilePath;
        listFilePath.reserve( listDisplayPath.size() );
        for ( const string& displayPath : listDisplayPath )
        {
            listFilePath.push_back( resourceFolder.join( displayPath ) ); // `countFiles` 는 트리 걷기와 같은 디스크 경로를 받는다
        }
        countFiles( listFilePath, outCounts );
    }

    void EditorAssetCatalog::countFiles( const vector<AbsolutePath>& listFilePath, EditorResourceCatalogCounts& outCounts )
    {
        const uint32 kindCount = EditorAssetTypeRegistry::getKindCount();
        outCounts._listKindCount.clear();
        outCounts._listKindCount.reserve( kindCount );
        for ( uint32 index = 0; index < kindCount; ++index )
        {
            const EditorAssetTypeRegistration& registration = EditorAssetTypeRegistry::getKindAt( index );
            outCounts._listKindCount.push_back( EditorResourceCatalogCount{ registration.getType(), registration._pBrowserLabel, 0 } );
        }

        for ( const AbsolutePath& filePath : listFilePath )
        {
            const EditorAssetType kind = EditorAssetTypeRegistry::findKind( filePath.str() );
            for ( EditorResourceCatalogCount& row : outCounts._listKindCount )
            {
                if ( row._kind != kind )
                    continue;
                ++row._count;
                break;
            }
        }
    }

    void EditorAssetCatalog::collectCatalogCounts( const AssetPaths& assetPaths, EditorResourceCatalogCounts& outCounts, const StopToken& stopToken )
    {
        vector<AbsolutePath> listAllFile;
        const AbsolutePath   resourceRootPath = assetPaths.getRootFolderPath();
        // 리소스 루트가 없거나 그새 사라졌으면 모든 종류가 0 개다.
        if ( resourceRootPath.empty() == false )
            (void)EditorContentScan::collectFiles( resourceRootPath, "", listAllFile, true, stopToken ); // 표시용 개수 — 도중에 멈춘 것은 collectFiles 가 알리고 모은 데까지 센다
        countFiles( listAllFile, outCounts );
    }
} // namespace sw::editor
