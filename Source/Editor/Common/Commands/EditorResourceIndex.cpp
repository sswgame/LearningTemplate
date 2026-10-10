#include "pch.h"

#include "Editor/Common/Commands/EditorResourceIndex.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

#include "Engine/Resource/ResourceUtil.h"

namespace sw::editor
{
    bool EditorResourceIndex::classifyFile( string_view filePath, EditorResourceIndexEntry& outEntry )
    {
        const EditorAssetType      kind  = EditorAssetTypeRegistry::findKind( filePath );
        const EditorAssetTypeInfo* pInfo = EditorAssetTypeRegistry::findKindInfo( kind );
        if ( pInfo == nullptr )
            return false;
        const string resourceID = ResourceUtil::toResourceID( filePath );
        if ( resourceID.empty() )
            return false;

        outEntry._kind     = kind;
        outEntry._category = pInfo->_pDisplayName;
        outEntry._title    = FileUtil::getFileNamePart( filePath );
        outEntry._detail   = resourceID;
        outEntry._path     = resourceID;
        return true;
    }

    void EditorResourceIndex::collectEntries( vector<EditorResourceIndexEntry>& outList )
    {
        outList.clear();

        const string& resourceFolder = ResourceUtil::getRootFolderPath();
        if ( resourceFolder.empty() )
            return;

        vector<string> listAllFile;
        FileUtil::collectFiles( resourceFolder, "", listAllFile, true );

        outList.reserve( listAllFile.size() );
        for ( const string& file : listAllFile )
        {
            EditorResourceIndexEntry entry{};
            if ( classifyFile( file, entry ) )
                outList.push_back( std::move( entry ) );
        }
    }

    void EditorResourceIndex::countFiles( const vector<string>& listFilePath, EditorResourceCatalogCounts& outCounts )
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

    void EditorResourceIndex::collectCatalogCounts( EditorResourceCatalogCounts& outCounts )
    {
        vector<string> listAllFile;
        const string&  resourceRootPath = ResourceUtil::getRootFolderPath();
        // 리소스 루트가 없거나 그새 사라졌으면 모든 종류가 0 개다.
        if ( resourceRootPath.empty() == false )
            (void)FileUtil::collectFiles( resourceRootPath, "", listAllFile, true );
        countFiles( listAllFile, outCounts );
    }
} // namespace sw::editor
