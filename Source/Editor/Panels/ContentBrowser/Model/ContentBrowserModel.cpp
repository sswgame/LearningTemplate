#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserModel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/FileSystem/AssetPaths.h"

namespace sw::editor
{
    namespace
    {
        struct ContentBrowserModelInternal
        {
            /** @brief 게임 팩 루트의 표시 이름 접두입니다(`game/<팩>`). */
            static constexpr const utf8* kGameDomainPrefix = "game/";

            /** @brief 에디터가 본 `Content/` 변경 번호입니다. 창은 에디터 컨텍스트가 있어야 도므로 컨텍스트와 핫 리로드는 필수입니다. */
            [[nodiscard]] static uint64 getContentChangeSerial()
            {
                return editor::getSubsystem<EditorContext>().getSubsystem<AssetHotReload>().getContentChangeSerial();
            }

            static EditorSourceControl& getSourceControl() { return editor::getSubsystem<EditorContext>().getSubsystem<EditorSourceControl>(); }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "ContentBrowserModel" );

    ContentBrowserModel::ContentBrowserModel() noexcept
        : _listRoot{}
        , _listEntry{}
        , _selectedFolderAbs{}
        , _listCrumb{}
        , _selectedAssetAbs{}
        , _history{}
        , _fileActions{}
        , _referenceSearch{}
        , _importQueue{}
        , _typeFilter{}
        , _folderJob{}
        , _folderCache{ &EditorFolderListing::collectChildFolders }
        , _seenContentChangeSerial{ 0 }
        , _bRootsDirty{ true }
        , _bFolderDirty{ true }
        , _bShowAllPacks{ false }
        , _bShowAllPacksInitialized{ false }
        , _bListingRecursive{ false }
    {
    }

    void ContentBrowserModel::tick( bool bSearchActive )
    {
        if ( bSearchActive != _bListingRecursive )
        {
            _bListingRecursive = bSearchActive;
            _bFolderDirty      = true;
        }

        if ( _bRootsDirty )
            refreshRoots();
        processPendingImports();
        syncWithContentChanges();
        AssetHotReload& hotReload = editor::getSubsystem<EditorContext>().getSubsystem<AssetHotReload>(); // 번호의 출처이기도 하다
        _referenceSearch.update( hotReload.getContentChangeSerial(), &hotReload );
        if ( _bFolderDirty )
            refreshCurrentFolder();
        vector<EditorFolderListingEntry> listNewEntry;
        if ( _folderJob.take( listNewEntry ) == EditorTaskPoll::Ready )
            applyFolderListing( listNewEntry );
    }

    void ContentBrowserModel::refreshAll()
    {
        _folderCache.clear();
        _listEntry.clear();
        _selectedAssetAbs.clear();
        refreshRoots();
        refreshCurrentFolder();
    }

    void ContentBrowserModel::refreshRoots()
    {
        _listRoot.clear();

        const auto addRoot = [this]( string_view name, string_view path )
        {
            if ( path.empty() || FileUtil::isDirectory( path ) == false )
                return;
            ContentBrowserRoot root;
            root._displayName  = string{ name };
            root._absolutePath = FileUtil::normalizeSeparators( path );
            _listRoot.push_back( std::move( root ) );
        };

        // 기본은 활성 게임 팩 하나다. 언리얼의 Content Browser 가 프로젝트 콘텐츠만 보이는 것과 같다. All packs 를 켜면 game/ 의 팩마다 루트가 하나씩 생긴다.
        if ( _bShowAllPacksInitialized == false )
        {
            _bShowAllPacks            = getPreferences<EditorContentBrowserPreferences>()._bShowAllPacksByDefault;
            _bShowAllPacksInitialized = true;
        }
        const string& packRoot = editor::getSubsystem<const GameConfig>()._packRoot.get();
        if ( packRoot.empty() == false )
            addRoot( packRoot, AssetPaths::findLooseDomainFolder( packRoot ).getDiskPath() );
        if ( _bShowAllPacks || packRoot.empty() )
        {
            vector<string> listPackFolder;
            (void)FileUtil::collectFolders( AssetPaths::findLooseDomainFolder( "game" ).getDiskPath(), listPackFolder, false ); // game/ 이 없으면 팩 루트가 없다
            std::sort( listPackFolder.begin(), listPackFolder.end() );
            for ( const string& packFolder : listPackFolder )
            {
                const string packName = string( ContentBrowserModelInternal::kGameDomainPrefix ) + FileUtil::normalizePath( FileUtil::getFileNamePart( packFolder ) );
                if ( packName != FileUtil::normalizePath( packRoot ) )
                    addRoot( packName, packFolder );
            }
        }
        addRoot( "engine", AssetPaths::findLooseDomainFolder( "engine" ).getDiskPath() );
        addRoot( "common", AssetPaths::findLooseDomainFolder( "common" ).getDiskPath() );
        addRoot( "editor", AssetPaths::findLooseDomainFolder( "editor" ).getDiskPath() );

        // 지금 폴더가 숨긴 팩 안에 있으면 첫 루트로 이동한다.
        const bool bSelectedIsUnderRoot = VectorUtil::containsIf( _listRoot, [&]( const ContentBrowserRoot& root )
        { return FileUtil::startsWithPathComponent( FileUtil::normalizePath( _selectedFolderAbs ), FileUtil::normalizePath( root._absolutePath ) ); } );
        if ( bSelectedIsUnderRoot == false && _listRoot.empty() == false )
            openFolder( _listRoot.front()._absolutePath );

        _bRootsDirty = false;
    }

    void ContentBrowserModel::setShowAllPacks( bool bShowAllPacks )
    {
        _bShowAllPacks            = bShowAllPacks;
        _bShowAllPacksInitialized = true;
        _bRootsDirty              = true;
    }

    uint32 ContentBrowserModel::getGameRootCount() const
    {
        uint32 gameRootCount{ 0 };
        for ( const ContentBrowserRoot& root : _listRoot )
        {
            if ( StringUtil::startsWith( root._displayName, ContentBrowserModelInternal::kGameDomainPrefix ) )
                ++gameRootCount;
        }
        return gameRootCount;
    }

    bool ContentBrowserModel::hasRoot( string_view displayName ) const
    {
        return VectorUtil::containsIf( _listRoot, [&]( const ContentBrowserRoot& root )
        { return FileUtil::normalizePath( root._displayName ) == FileUtil::normalizePath( displayName ); } );
    }

    void ContentBrowserModel::refreshCurrentFolder()
    {
        _folderJob.request( _selectedFolderAbs, _bListingRecursive );
        _bFolderDirty = false;
    }

    void ContentBrowserModel::applyFolderListing( vector<EditorFolderListingEntry>& listEntry )
    {
        // 목록을 보기만 하고 `.meta` 를 쓰지 않는다. GUID 는 그 에셋을 쓰는 시스템(머티리얼 캐시, 프리팹, 씬 저장, 임포트)이 만든다.
        _listEntry = std::move( listEntry );
        std::sort( _listEntry.begin(), _listEntry.end(), []( const AssetEntry& entryA, const AssetEntry& entryB )
        {
            if ( entryA._bIsDirectory != entryB._bIsDirectory )
                return entryA._bIsDirectory > entryB._bIsDirectory;
            return entryA._name < entryB._name;
        } );
    }

    void ContentBrowserModel::syncWithContentChanges()
    {
        const uint64 serial = ContentBrowserModelInternal::getContentChangeSerial();
        if ( serial == _seenContentChangeSerial )
            return;
        _seenContentChangeSerial = serial;
        _bFolderDirty            = true;
        _folderCache.clear();
    }

    void ContentBrowserModel::openFolder( string_view absolutePath )
    {
        vector<ContentBrowserCrumb> listCrumb;
        makeTrailForFolder( absolutePath, listCrumb );
        selectFolder( absolutePath, listCrumb );
    }

    void ContentBrowserModel::selectFolder( string_view absolutePath, const vector<ContentBrowserCrumb>& listCrumb, bool bRecordHistory )
    {
        const string normalizedPath = FileUtil::normalizeSeparators( absolutePath );
        if ( bRecordHistory )
            _history.record( normalizedPath, listCrumb );

        // 탐색과 I/O 는 실제 파일 시스템의 대소문자를 쓰고, 저장 경로는 AssetPaths::makeSavePath 가 상대 경로 조각을 소문자로 바꾼다.
        _selectedFolderAbs = normalizedPath;
        _listCrumb         = listCrumb;
        _selectedAssetAbs.clear();
        _bFolderDirty = true;
    }

    void ContentBrowserModel::selectCrumb( size_t crumbIndex )
    {
        if ( crumbIndex >= _listCrumb.size() )
            return;
        // 조각까지 잘라 둔 사본을 넘긴다 — selectFolder 가 `_listCrumb` 를 덮어쓴다.
        const vector<ContentBrowserCrumb> listCrumb( _listCrumb.begin(), _listCrumb.begin() + static_cast<std::ptrdiff_t>( crumbIndex + 1 ) );
        selectFolder( listCrumb.back()._absolutePath, listCrumb );
    }

    void ContentBrowserModel::makeTrailForFolder( string_view folderAbs, vector<ContentBrowserCrumb>& outListCrumb ) const
    {
        const string folderNorm = FileUtil::normalizePath( folderAbs );
        for ( const ContentBrowserRoot& root : _listRoot )
        {
            if ( FileUtil::startsWithPathComponent( folderNorm, FileUtil::normalizePath( root._absolutePath ) ) )
            {
                ContentBrowserLogic::makeFolderTrail( root._displayName, root._absolutePath, folderAbs, outListCrumb );
                return;
            }
        }
        ContentBrowserLogic::makeFolderTrail( {}, {}, folderAbs, outListCrumb );
    }

    void ContentBrowserModel::navigateBack()
    {
        // 이력을 적지 않고 고르므로 항목 포인터는 selectFolder 안에서도 유효하다.
        const ContentBrowserHistoryEntry* pEntry = _history.stepBack();
        if ( pEntry != nullptr )
            selectFolder( pEntry->_folderPathAbs, pEntry->_listCrumb, false );
    }

    void ContentBrowserModel::navigateForward()
    {
        const ContentBrowserHistoryEntry* pEntry = _history.stepForward();
        if ( pEntry != nullptr )
            selectFolder( pEntry->_folderPathAbs, pEntry->_listCrumb, false );
    }

    void ContentBrowserModel::collectVisibleEntries( string_view searchText, vector<const AssetEntry*>& outListVisible ) const
    {
        const EditorListFilter filter{ searchText };
        outListVisible.clear();
        outListVisible.reserve( _listEntry.size() );
        for ( const AssetEntry& entry : _listEntry )
        {
            const bool bPassesType = entry._bIsDirectory || _typeFilter.passes( entry._name );
            if ( bPassesType && filter.matches( entry._name ) )
                outListVisible.push_back( &entry );
        }
    }

    void ContentBrowserModel::selectAsset( const AssetEntry& entry )
    {
        _selectedAssetAbs = entry._absolutePath;
        if ( entry._bIsDirectory == false )
            editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().setFocusedAssetPath( entry._relativePath.c_str() );
    }

    void ContentBrowserModel::selectAssetPath( string_view absolutePath )
    {
        for ( const AssetEntry& entry : _listEntry )
        {
            if ( FileUtil::pathsEqualNormalized( entry._absolutePath, absolutePath ) )
            {
                selectAsset( entry );
                return;
            }
        }
    }

    void ContentBrowserModel::openAsset( const AssetEntry& entry )
    {
        if ( entry._bIsDirectory )
        {
            vector<ContentBrowserCrumb> listCrumb = _listCrumb;
            ContentBrowserLogic::appendChildCrumb( entry._absolutePath, listCrumb );
            selectFolder( entry._absolutePath, listCrumb );
            return;
        }

        selectAsset( entry );
        SW_LOG_TRACE( "Open: %#", entry._relativePath.c_str() );
        (void)EditorAssetCommands::openPath( entry._relativePath ); // 실패는 openPath 가 알린다
    }

    bool ContentBrowserModel::isSelected( const AssetEntry& entry, bool bFolderToo ) const
    {
        if ( FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedAssetAbs ) )
            return true;
        return bFolderToo && entry._bIsDirectory && FileUtil::pathsEqualNormalized( entry._absolutePath, _selectedFolderAbs );
    }

    void ContentBrowserModel::selectAfterFileChange( string_view absolutePath )
    {
        // 감시도 같은 변경을 알리지만 한두 프레임 늦다. 결과를 아는 위치에서 바로 다시 읽는다.
        _selectedAssetAbs = FileUtil::normalizeSeparators( absolutePath );
        _bFolderDirty     = true;
        _folderCache.clear();
        // 이 변경 전에 시작한 스캔은 이동이 고친 색인을 오래된 결과로 덮어쓴다. 그 결과를 버리고 새로 스캔한다.
        _referenceSearch.restartPendingScan( ContentBrowserModelInternal::getContentChangeSerial() );
    }

    bool ContentBrowserModel::createNewFolder()
    {
        string folderAbs;
        if ( ContentBrowserFileActions::createFolder( _selectedFolderAbs, folderAbs ) == false )
            return false;
        selectAfterFileChange( folderAbs );
        return true;
    }

    bool ContentBrowserModel::createNewAsset( const EditorNewAssetTemplate& assetTemplate )
    {
        string assetAbs;
        if ( ContentBrowserFileActions::createAsset( _selectedFolderAbs, assetTemplate, assetAbs ) == false )
            return false;
        selectAfterFileChange( assetAbs );
        return true;
    }

    bool ContentBrowserModel::duplicateAsset( string_view absolutePath )
    {
        string assetAbs;
        if ( ContentBrowserFileActions::duplicateAsset( absolutePath, assetAbs ) == false )
            return false;
        selectAfterFileChange( assetAbs );
        return true;
    }

    bool ContentBrowserModel::beginRename( string_view absolutePath, string& outStem )
    {
        return _fileActions.beginRename( absolutePath, outStem );
    }

    EditorReferenceIndex* ContentBrowserModel::findCurrentReferenceIndex()
    {
        EditorReferenceIndex* pIndex = _referenceSearch.findCurrentIndex( ContentBrowserModelInternal::getContentChangeSerial() );
        if ( pIndex == nullptr )
            SW_LOG_WARNING( "The reference index has not caught up with the last file change yet - nothing was moved, try again in a moment" );
        return pIndex;
    }

    bool ContentBrowserModel::commitRename( string_view newName )
    {
        // 색인이 따라오기 전이면 입력란을 열어 둔 채 돌아온다 — Enter 를 다시 누르면 된다.
        EditorReferenceIndex* pIndex = findCurrentReferenceIndex();
        if ( pIndex == nullptr )
            return false;
        string destinationAbs;
        if ( _fileActions.commitRename( newName, *pIndex, destinationAbs ) == false )
            return false;
        selectAfterFileChange( destinationAbs );
        return true;
    }

    bool ContentBrowserModel::isRenaming( const AssetEntry& entry ) const
    {
        const string& renamingAbs = _fileActions.getRenamingPath();
        return renamingAbs.empty() == false && FileUtil::pathsEqualNormalized( entry._absolutePath, renamingAbs );
    }

    bool ContentBrowserModel::moveAssetToFolder( string_view sourceAbs, string_view folderAbs )
    {
        EditorReferenceIndex* pIndex = findCurrentReferenceIndex();
        if ( pIndex == nullptr )
            return false;
        string destinationAbs;
        if ( ContentBrowserFileActions::moveAssetToFolder( sourceAbs, folderAbs, *pIndex, destinationAbs ) == false )
            return false;
        selectAfterFileChange( destinationAbs );
        return true;
    }

    bool ContentBrowserModel::confirmDeleteAsset()
    {
        if ( _fileActions.confirmDelete() == false )
            return false;
        // 지운 파일이 목록과 선택에 남지 않도록 지금 폴더를 다시 읽는다. 파일 감시도 같은 변경을 알리지만 한두 프레임 늦다.
        _selectedAssetAbs.clear();
        _bFolderDirty = true;
        _folderCache.clear();
        return true;
    }

    void ContentBrowserModel::importFilesFromDialog()
    {
        if ( _selectedFolderAbs.empty() )
        {
            SW_LOG_WARNING( "Select a destination folder before importing." );
            return;
        }
        _importQueue.openDialog( _selectedFolderAbs );
    }

    void ContentBrowserModel::processPendingImports()
    {
        vector<string> listPath;
        if ( _importQueue.takePending( listPath ) == false )
            return;
        if ( _selectedFolderAbs.empty() )
        {
            SW_LOG_WARNING( "Import cancelled — no destination folder." );
            return;
        }
        if ( EditorAssetFileCommands::importFiles( _selectedFolderAbs, listPath ) > 0 )
            _bFolderDirty = true;
    }

    bool ContentBrowserModel::isReferenceIndexCurrent() const
    {
        return _referenceSearch.isCurrent( ContentBrowserModelInternal::getContentChangeSerial() );
    }

    string ContentBrowserModel::describeSourceControlStatus( const AssetEntry& entry )
    {
        if ( entry._bIsDirectory )
            return {};
        return ContentBrowserModelInternal::getSourceControl().describeStatus( entry._absolutePath, entry._bReadOnly );
    }

    bool ContentBrowserModel::isLocked( const AssetEntry& entry )
    {
        return ContentBrowserModelInternal::getSourceControl().findLock( entry._absolutePath ) != nullptr;
    }
} // namespace sw::editor
