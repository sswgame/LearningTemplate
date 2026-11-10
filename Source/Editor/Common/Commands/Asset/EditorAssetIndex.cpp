#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorAssetIndex.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/Asset/EditorAssetType.h"

#include "Engine/Automation/Scenario/AutomationScratch.h"
#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    bool EditorAssetIndex::classifyFile( const AssetPaths& assetPaths, string_view filePath, EditorAssetIndexEntry& outEntry )
    {
        const EditorAssetTypeRegistration* pInfo = EditorAssetTypeRegistry::findKindInfoForPath( filePath );
        if ( pInfo == nullptr )
            return false;
        const string assetID = assetPaths.toAssetID( filePath );
        // 다른 프로세스의 시나리오 사본(`scratch/<이름>_p<pid>/…` — 같은 체크아웃의 다른 App · 끝난 실행의 잔여)은 이 에디터의 에셋이 아니다.
        // 색인에 넣으면 Quick Open 이 원본과 같은 파일 이름의 사본을 먼저 집어 시퀀서가 씬 플레이어를 찾지 못했다(파일 감시와 같은 규칙).
        if ( assetID.empty() || AutomationScratch::isOtherProcessInstance( assetID ) )
            return false;

        outEntry._kind     = pInfo->getType();
        outEntry._category = pInfo->_pDisplayName;
        outEntry._title    = FileUtil::getFileNamePart( filePath );
        outEntry._detail   = assetID;
        outEntry._path     = assetID;
        return true;
    }

    void EditorAssetIndex::collectEntries( const AssetPaths& assetPaths, vector<EditorAssetIndexEntry>& outList, const StopToken& stopToken )
    {
        outList.clear();

        const string& resourceFolder = assetPaths.getRootFolderPath();
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
            if ( classifyFile( assetPaths, file, entry ) )
                outList.push_back( std::move( entry ) );
        }
    }

    void EditorAssetIndex::countFiles( const vector<string>& listFilePath, EditorResourceCatalogCounts& outCounts )
    {
        const uint32 kindCount = EditorAssetTypeRegistry::getKindCount();
        outCounts._listKindCount.clear();
        outCounts._listKindCount.reserve( kindCount );
        for ( uint32 index = 0; index < kindCount; ++index )
        {
            const EditorAssetTypeRegistration& registration = EditorAssetTypeRegistry::getKindAt( index );
            outCounts._listKindCount.push_back( EditorResourceCatalogCount{ registration.getType(), registration._pBrowserLabel, 0 } );
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

    void EditorAssetIndex::collectCatalogCounts( const AssetPaths& assetPaths, EditorResourceCatalogCounts& outCounts, const StopToken& stopToken )
    {
        vector<string> listAllFile;
        const string&  resourceRootPath = assetPaths.getRootFolderPath();
        // 리소스 루트가 없거나 그새 사라졌으면 모든 종류가 0 개다.
        if ( resourceRootPath.empty() == false )
            (void)FileUtil::collectFiles( resourceRootPath, "", listAllFile, true, stopToken ); // 표시용 개수 — 도중에 멈춘 것은 collectFiles 가 알리고 모은 데까지 센다
        countFiles( listAllFile, outCounts );
    }
} // namespace sw::editor
