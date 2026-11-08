#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserModel.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Backend/FileDialog.h"
#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorSelectionSync.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"

// ContentBrowserImportQueue
// ContentBrowserTypeFilter
namespace sw::editor
{
    ContentBrowserTypeFilter::ContentBrowserTypeFilter()
        : _listFilter{}
        , _selectedKind{}
        , _selectedIndex{ 0 }
        , _generation{ 0 }
        , _bSelectedOther{ SW_FALSE }
        , _bBuilt{ SW_FALSE }
    {
    }

    void ContentBrowserTypeFilter::sync()
    {
        const uint32 generation = EditorAssetTypeRegistry::getGeneration();
        if ( _bBuilt && generation == _generation )
            return;
        _bBuilt     = SW_TRUE;
        _generation = generation;
        EditorAssetTypeRegistry::collectBrowserFilters( _listFilter );

        // 고른 필터를 종류로 다시 찾는다. 없으면 All(0) 이다.
        _selectedIndex = 0;
        for ( uint32 index = 0; index < static_cast<uint32>( _listFilter.size() ); ++index )
        {
            const EditorAssetBrowserFilter& filter = _listFilter[index];
            if ( filter._bOther == _bSelectedOther && filter._kind == _selectedKind )
            {
                _selectedIndex = index;
                return;
            }
        }
        _selectedKind   = EditorAssetType{};
        _bSelectedOther = SW_FALSE;
    }

    const utf8* ContentBrowserTypeFilter::getSelectedLabel() const
    {
        return _selectedIndex < _listFilter.size() ? _listFilter[_selectedIndex]._pLabel : "All";
    }

    void ContentBrowserTypeFilter::select( uint32 index )
    {
        if ( index >= _listFilter.size() )
            return;
        _selectedIndex  = index;
        _selectedKind   = _listFilter[index]._kind;
        _bSelectedOther = _listFilter[index]._bOther;
    }

    bool ContentBrowserTypeFilter::passes( string_view path ) const
    {
        if ( _bSelectedOther )
            return EditorAssetTypeRegistry::matchesOther( path );
        if ( _selectedKind.isUnknown() )
            return true;
        return EditorAssetTypeRegistry::matches( _selectedKind, path );
    }
} // namespace sw::editor

namespace sw::editor
{
    ContentBrowserImportQueue::ContentBrowserImportQueue()
        : _pendingPath{}
    {
    }

    void ContentBrowserImportQueue::openDialog( string_view initialFolderAbs )
    {
        FileDialogParams params{};
        params._type                = FileDialogParams::Type::Open;
        params._title               = "Import to Content Browser";
        params._description         = "Assets";
        params._bEnableMultiselect  = true;
        params._initialDirectory    = string{ initialFolderAbs };
        params._listFilterExtension = {};
        EditorAssetTypeRegistry::appendImportExtensions( params._listFilterExtension );

        FileDialog::open( params, SW_DELEGATE_METHOD( FileDialogDelegate, &ContentBrowserImportQueue::onDialogResult, this ) );
    }

    void ContentBrowserImportQueue::onDialogResult( const vector<string>& listPath )
    {
        _pendingPath.pushRange( listPath );
    }

    bool ContentBrowserImportQueue::takePending( vector<string>& outListPath )
    {
        outListPath.clear();
        return _pendingPath.takeAll( outListPath );
    }
} // namespace sw::editor

namespace sw::editor
{
    namespace
    {
        struct ContentBrowserModelInternal
        {
            /** @brief 게임 팩 루트의 표시 이름 접두입니다(`game/<팩>`). */
            static constexpr const utf8* kGameDomainPrefix = "game/";

            /** @brief 에디터가 본 `Content/` 변경 번호입니다. 창은 에디터 컨텍스트가 있어야 도므로 컨텍스트와 핫 리로드는 필수입니다. */
            [[nodiscard]] static AssetHotReload& getHotReload() { return editor::getSubsystem<EditorContext>().getSubsystem<AssetHotReload>(); }
            [[nodiscard]] static uint64          getContentChangeSerial() { return getHotReload().getContentChangeSerial(); }

            static EditorSourceControl& getSourceControl() { return editor::getSubsystem<EditorContext>().getSubsystem<EditorSourceControl>(); }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    ContentBrowserModel::ContentBrowserModel() noexcept
        : _listRoot{}
        , _listEntry{}
        , _selectedFolderAbs{}
        , _listCrumb{}
        , _selectedAssetAbs{}
        , _importFolderAbs{}
        , _history{}
        , _fileActions{}
        , _pendingMoves{}
        , _moveRequestSourceAbs{}
        , _moveRequestFolderAbs{}
        , _moveRequestPlan{}
        , _referenceSearch{}
        , _importQueue{}
        , _typeFilter{}
        , _folderJob{}
        , _folderCache{ &EditorFolderListing::collectChildFolders }
        , _seenContentChangeSerial{ 0 }
        , _bRootsDirty{ SW_TRUE }
        , _bFolderDirty{ SW_TRUE }
        , _bShowAllPacks{ SW_FALSE }
        , _bShowAllPacksInitialized{ SW_FALSE }
        , _bListingRecursive{ SW_FALSE }
        , _bMoveRequestPlanned{ SW_FALSE }
    {
    }

    void ContentBrowserModel::tick( bool bSearchActive )
    {
        if ( bSearchActive != _bListingRecursive )
        {
            _bListingRecursive = bSearchActive;
            _bFolderDirty      = SW_TRUE;
        }

        if ( _bRootsDirty )
            refreshRoots();
        processPendingImports();
        syncWithContentChanges();
        applyPendingMove();
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
            _bShowAllPacksInitialized = SW_TRUE;
        }
        const string& packRoot = editor::getSubsystem<const GameConfig>()._packRoot.get();
        if ( packRoot.empty() == false )
            addRoot( packRoot, EditorPaths::findContentFolder( packRoot ) );
        if ( _bShowAllPacks || packRoot.empty() )
        {
            vector<string> listPackDomain;
            (void)AssetPaths::collectGamePackDomains( editor::getSubsystem<AssetPaths>().getRootFolderPath(), listPackDomain ); // game/ 이 없으면 팩 루트가 없다
            for ( const string& packDomain : listPackDomain )
            {
                const string packName = FileUtil::normalizePath( packDomain );
                if ( packName != FileUtil::normalizePath( packRoot ) )
                    addRoot( packName, EditorPaths::findContentFolder( packDomain ) );
            }
        }
        addRoot( constant::path::kEnginePack, EditorPaths::findContentFolder( constant::path::kEnginePack ) );
        addRoot( constant::path::kCommonPack, EditorPaths::findContentFolder( constant::path::kCommonPack ) );
        addRoot( constant::path::kEditorPack, EditorPaths::findContentFolder( constant::path::kEditorPack ) );

        // 처음이거나 지금 폴더가 숨긴 게임 팩 안에 있으면 첫 루트로 이동한다. 루트가 아닌 다른 도메인(자동화 사본 `scratch/` - `content.open` 이 연다)은 그대로 둔다 -
        // 예전에는 "어느 루트 아래도 아니면" 이동해, 루트를 다시 만드는 순간(시나리오 단계 초기화 뒤) 연 사본 폴더에서 쫓겨났다.
        const string selectedFolder       = FileUtil::normalizePath( _selectedFolderAbs );
        const string gamePacksFolder      = EditorPaths::findContentFolder( constant::path::kGamePack );
        const bool   bSelectedIsUnderRoot = VectorUtil::containsIf( _listRoot, [&]( const ContentBrowserRoot& root )
          { return FileUtil::startsWithPathComponent( selectedFolder, FileUtil::normalizePath( root._absolutePath ) ); } );
        const bool   bInHiddenGamePack =
            bSelectedIsUnderRoot == false && gamePacksFolder.empty() == false && FileUtil::startsWithPathComponent( selectedFolder, FileUtil::normalizePath( gamePacksFolder ) );
        if ( ( selectedFolder.empty() || bInHiddenGamePack ) && _listRoot.empty() == false )
            openFolder( _listRoot.front()._absolutePath );

        _bRootsDirty = SW_FALSE;
    }

    void ContentBrowserModel::setShowAllPacks( bool bShowAllPacks )
    {
        _bShowAllPacks            = bShowAllPacks;
        _bShowAllPacksInitialized = SW_TRUE;
        _bRootsDirty              = SW_TRUE;
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
        _bFolderDirty = SW_FALSE;
    }

    void ContentBrowserModel::applyFolderListing( vector<EditorFolderListingEntry>& listEntry )
    {
        _listEntry = std::move( listEntry );
        VectorUtil::sort( _listEntry, []( const AssetEntry& entryA, const AssetEntry& entryB )
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
        _bFolderDirty            = SW_TRUE;
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

        // 탐색과 I/O 는 실제 파일 시스템의 대소문자를 쓰고, 새로 만드는 파일 이름은 소문자다(리소스 id 규칙).
        _selectedFolderAbs = normalizedPath;
        _listCrumb         = listCrumb;
        _selectedAssetAbs.clear();
        _bFolderDirty = SW_TRUE;
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
                ContentBrowserCrumb::makeFolderTrail( root._displayName, root._absolutePath, folderAbs, outListCrumb );
                return;
            }
        }
        ContentBrowserCrumb::makeFolderTrail( {}, {}, folderAbs, outListCrumb );
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
        if ( entry._bIsDirectory )
            return;
        editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().setFocusedAssetPath( entry._relativePath.c_str() );
        // 콘텐츠 브라우저 문맥의 칸 — 메인 씬 선택을 바꾸지 않는다(문맥마다 따로, `EditorSelectionSync`).
        EditorSelectionSync* pSync = editor::findSubsystem<EditorSelectionSync>();
        if ( pSync != nullptr )
            pSync->publish( EditorSelectionContext::ContentBrowser, entry._relativePath, 0 );
    }

    bool ContentBrowserModel::revealAsset( string_view assetID )
    {
        const auto filePath = editor::getSubsystem<AssetPaths>().findLooseFile( FileUtil::normalizePath( assetID ) ); // 디스크 파일(없으면 비어 있다)
        if ( filePath.has_value() == false )
            return false; // 쿠킹 묶음에만 있는 애셋 — 보일 폴더가 없다
        openFolder( FileUtil::getDirectoryPart( filePath->str() ) );
        // 폴더를 고르면 선택이 비므로 그 뒤에 고른다 — 목록이 새로 와도 경로로 고르므로 그 타일이 고른 채 선다.
        _selectedAssetAbs = FileUtil::normalizeSeparators( filePath->str() );
        editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ assetID }.c_str() );
        return true;
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
            ContentBrowserCrumb::appendChildCrumb( entry._absolutePath, listCrumb );
            selectFolder( entry._absolutePath, listCrumb );
            return;
        }

        selectAsset( entry );
        SW_LOG_TRACE_ID( "ContentBrowserModel.Open", "Open: %#", entry._relativePath.c_str() );
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
        _bFolderDirty     = SW_TRUE;
        _folderCache.clear();
        // 이 변경 전에 시작한 스캔은 이동이 고친 색인을 오래된 결과로 덮어쓴다. 그 결과를 버리고 새로 스캔한다.
        ContentBrowserModelInternal::getHotReload().restartPendingReferenceScan();
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
        const bool bStarted = _fileActions.beginRename( absolutePath, outStem );
        if ( bStarted )
            _pendingMoves.discardRename();
        return bStarted;
    }

    void ContentBrowserModel::cancelRename()
    {
        _fileActions.cancelRename();
        _pendingMoves.discardRename();
    }

    EditorReferenceIndex* ContentBrowserModel::findCurrentReferenceIndex()
    {
        return ContentBrowserModelInternal::getHotReload().findCurrentReferenceIndex();
    }

    void ContentBrowserModel::applyPendingMove()
    {
        // 옮기기 하나가 역색인을 다시 읽게 하므로 다음 옮기기는 역색인이 다시 따라온 뒤에 돈다.
        ContentBrowserMoveQueue::Move move;
        if ( _pendingMoves.isEmpty() || findCurrentReferenceIndex() == nullptr || _pendingMoves.takeNext( move ) == false )
            return;
        if ( move._bRename )
            (void)commitRename( move._newName ); // 실패(이름 규칙 · 이미 있음)는 moveAsset 이 알린다
        else
            (void)moveAssetToFolder( move._sourceAbs, move._folderAbs );
    }

    bool ContentBrowserModel::commitRename( string_view newName )
    {
        EditorReferenceIndex* pIndex = findCurrentReferenceIndex();
        if ( pIndex == nullptr )
        {
            _pendingMoves.addRename( newName );
            SW_LOG_INFO_ID( "ContentBrowserModel.MoveWaitsForReferenceIndex", "Rename waits for the reference index to catch up with the last file change" );
            return true;
        }
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
        {
            _pendingMoves.addMove( sourceAbs, folderAbs );
            SW_LOG_INFO_ID( "ContentBrowserModel.MoveWaitsForReferenceIndex", "Move waits for the reference index to catch up with the last file change" );
            return true;
        }
        string destinationAbs;
        if ( ContentBrowserFileActions::moveAssetToFolder( sourceAbs, folderAbs, *pIndex, destinationAbs ) == false )
            return false;
        selectAfterFileChange( destinationAbs );
        return true;
    }

    void ContentBrowserModel::requestMoveToFolder( string_view sourceAbs, string_view folderAbs )
    {
        _moveRequestSourceAbs = FileUtil::normalizeSeparators( sourceAbs );
        _moveRequestFolderAbs = FileUtil::normalizeSeparators( folderAbs );
        _moveRequestPlan      = EditorAssetMovePlan{};
        _bMoveRequestPlanned  = SW_FALSE;
    }

    bool ContentBrowserModel::updateMoveRequest()
    {
        if ( hasMoveRequest() == false )
            return false;
        if ( _bMoveRequestPlanned == SW_TRUE )
            return true;
        const EditorReferenceIndex* pIndex = findCurrentReferenceIndex();
        if ( pIndex == nullptr )
            return false;                                                                                                             // 역색인이 마지막 변경을 따라오면 계획한다 — 낡은 색인으로 세운 계획은 고칠 참조를 빠뜨린다
        (void)ContentBrowserFileActions::planMoveToFolder( _moveRequestSourceAbs, _moveRequestFolderAbs, *pIndex, _moveRequestPlan ); // 옮길 수 없으면 `_failure` 를 창이 보인다
        _bMoveRequestPlanned = SW_TRUE;
        return true;
    }

    bool ContentBrowserModel::confirmMoveRequest()
    {
        const string sourceAbs = _moveRequestSourceAbs;
        const string folderAbs = _moveRequestFolderAbs;
        cancelMoveRequest();
        if ( sourceAbs.empty() )
            return false;
        return moveAssetToFolder( sourceAbs, folderAbs ); // 적용 때 계획을 다시 세운다(확인을 기다리는 사이 파일이 바뀌었을 수 있다)
    }

    void ContentBrowserModel::cancelMoveRequest()
    {
        _moveRequestSourceAbs.clear();
        _moveRequestFolderAbs.clear();
        _moveRequestPlan     = EditorAssetMovePlan{};
        _bMoveRequestPlanned = SW_FALSE;
    }

    bool ContentBrowserModel::confirmDeleteAsset()
    {
        if ( _fileActions.confirmDelete() == false )
            return false;
        // 지운 파일이 목록과 선택에 남지 않도록 지금 폴더를 다시 읽는다. 파일 감시도 같은 변경을 알리지만 한두 프레임 늦다.
        _selectedAssetAbs.clear();
        _bFolderDirty = SW_TRUE;
        _folderCache.clear();
        return true;
    }

    void ContentBrowserModel::importFilesFromDialog()
    {
        if ( _selectedFolderAbs.empty() )
        {
            SW_LOG_WARNING_ID( "ContentBrowserModel.SelectDestinationFolderBefore", "Select a destination folder before importing." );
            return;
        }
        _importFolderAbs = _selectedFolderAbs;
        _importQueue.openDialog( _importFolderAbs );
    }

    void ContentBrowserModel::processPendingImports()
    {
        vector<string> listPath;
        if ( _importQueue.takePending( listPath ) == false )
            return;
        if ( _importFolderAbs.empty() )
        {
            SW_LOG_WARNING_ID( "ContentBrowserModel.ImportCancelledNoDestination", "Import cancelled — no destination folder." );
            return;
        }
        if ( EditorAssetFileCommands::importFiles( _importFolderAbs, listPath ) > 0 )
            _bFolderDirty = SW_TRUE;
    }

    bool ContentBrowserModel::isReferenceIndexCurrent() const
    {
        return ContentBrowserModelInternal::getHotReload().isReferenceIndexCurrent();
    }

    void ContentBrowserModel::showReferences( string_view absolutePath, bool bDependencies )
    {
        _referenceSearch.query( getReferenceIndex(), absolutePath, bDependencies );
    }

    const EditorReferenceIndex& ContentBrowserModel::getReferenceIndex()
    {
        return ContentBrowserModelInternal::getHotReload().getReferenceIndex();
    }

    bool ContentBrowserModel::isReferenceScanPending()
    {
        return ContentBrowserModelInternal::getHotReload().isReferenceScanPending();
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
