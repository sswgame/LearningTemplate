#include "pch.h"

#include "Editor/Panels/ContentBrowser/Model/ContentBrowserViewModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Backend/FileDialog.h"
#include "Editor/Common/Commands/Asset/EditorAssetCommands.h"
#include "Editor/Common/Commands/Asset/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/Asset/EditorFolderListing.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/Config/EditorPreferences.h"
#include "Editor/Common/Config/EditorSettingsRegistry.h"
#include "Editor/Common/Config/EditorToolDefaults.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Widgets/Model/EditorListFilter.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPanelID.h"
#include "Editor/Common/Workspace/EditorSelectionSync.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFileDrop.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"

// ContentBrowserImportQueue
// ContentBrowserTypeFilter
namespace sw::editor
{
    namespace
    {
        struct ContentBrowserViewModelRequests
        {
            /** @brief 에셋 명령이 쌓은 요청과 창이 내건 선택입니다(에디터 범위 상태 — 명령은 Model 에 있고 창 인스턴스를 모른다). */
            struct State
            {
                vector<ContentBrowserRequest> _listRequest;
                AbsolutePath                  _selectedAssetAbs;
            };
        };
    } // namespace

    void ContentBrowserViewModel::postRequest( ContentBrowserRequestKind kind, const AbsolutePath& absolutePath )
    {
        getEditorState<ContentBrowserViewModelRequests::State>()._listRequest.push_back( ContentBrowserRequest{ kind, absolutePath } );
        EditorContext* pContext = editor::findSubsystem<EditorContext>();
        if ( pContext != nullptr )
            pContext->getSubsystem<EditorWorkspace>().requestOpenPanelByID( editorpanel::kContentBrowser.getView() );
    }

    AbsolutePath ContentBrowserViewModel::findPublishedSelection()
    {
        return getEditorState<ContentBrowserViewModelRequests::State>()._selectedAssetAbs;
    }

    bool ContentBrowserViewModel::takeViewRequest( ContentBrowserRequest& outRequest )
    {
        vector<ContentBrowserRequest>& listRequest = getEditorState<ContentBrowserViewModelRequests::State>()._listRequest;
        while ( listRequest.empty() == false )
        {
            ContentBrowserRequest request = std::move( listRequest.front() );
            listRequest.erase( listRequest.begin() );
            switch ( request._kind )
            {
                case ContentBrowserRequestKind::Reveal:
                {
                    openFolder( request._absolutePath.getParent() );
                    selectAssetPath( request._absolutePath );
                    break;
                }
                case ContentBrowserRequestKind::OpenFolder:
                {
                    openFolder( request._absolutePath );
                    break;
                }
                case ContentBrowserRequestKind::Duplicate:
                {
                    (void)duplicateAsset( request._absolutePath ); // 실패는 copyFile 이 알린다
                    break;
                }
                case ContentBrowserRequestKind::ShowReferences:
                case ContentBrowserRequestKind::ShowDependencies:
                case ContentBrowserRequestKind::Rename:
                case ContentBrowserRequestKind::Delete:
                {
                    outRequest = std::move( request );
                    return true;
                }
            }
        }
        return false;
    }
} // namespace sw::editor

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

    bool ContentBrowserTypeFilter::matches( string_view path ) const
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
        params._type                = FileDialogParams::Kind::Open;
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
        struct ContentBrowserViewModelInternal
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
    ContentBrowserViewModel::ContentBrowserViewModel() noexcept
        : _listRoot{}
        , _listEntry{}
        , _selectedFolderAbs{}
        , _listBreadcrumb{}
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

    void ContentBrowserViewModel::update( bool bSearchActive )
    {
        getEditorState<ContentBrowserViewModelRequests::State>()._selectedAssetAbs = _selectedAssetAbs; // 대상 없는 에셋 명령(단축키)이 읽는다 — 명령은 창 인스턴스를 모른다
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

    void ContentBrowserViewModel::refreshAll()
    {
        _folderCache.clear();
        _listEntry.clear();
        _selectedAssetAbs = {};
        refreshRoots();
        refreshCurrentFolder();
    }

    void ContentBrowserViewModel::refreshRoots()
    {
        _listRoot.clear();

        const auto addRoot = [this]( string_view name, const AbsolutePath& path )
        {
            if ( path.empty() || FileUtil::isDirectory( path ) == false )
                return;
            ContentBrowserRoot root;
            root._displayName  = string{ name };
            root._absolutePath = path;
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
            addRoot( packRoot, findAssetFile( packRoot ) );
        if ( _bShowAllPacks || packRoot.empty() )
        {
            vector<string> listPackDomain;
            (void)AssetPaths::collectGamePackDomains( editor::getSubsystem<AssetPaths>().getRootFolderPath(), listPackDomain ); // game/ 이 없으면 팩 루트가 없다
            for ( const string& packDomain : listPackDomain )
            {
                const string packName = FileUtil::normalizePath( packDomain ).str();
                if ( packName != FileUtil::normalizePath( packRoot ).str() )
                    addRoot( packName, findAssetFile( packDomain ) );
            }
        }
        addRoot( constant::path::kEnginePack, findAssetFile( constant::path::kEnginePack ) );
        addRoot( constant::path::kCommonPack, findAssetFile( constant::path::kCommonPack ) );
        addRoot( constant::path::kEditorPack, findAssetFile( constant::path::kEditorPack ) );

        // 처음이거나 지금 폴더가 숨긴 게임 팩 안에 있으면 첫 루트로 이동한다. 루트가 아닌 다른 도메인(자동화 사본 `scratch/` - `content.open` 이 연다)은 그대로 둔다 -
        // 예전에는 "어느 루트 아래도 아니면" 이동해, 루트를 다시 만드는 순간(시나리오 단계 초기화 뒤) 연 사본 폴더에서 쫓겨났다.
        const ComparableDiskPath selectedFolder       = FileUtil::makeComparableDiskPath( _selectedFolderAbs.str() );
        const string             gamePacksFolder      = findAssetFile( constant::path::kGamePack ).str();
        const bool               bSelectedIsUnderRoot = VectorUtil::containsIf( _listRoot, [&]( const ContentBrowserRoot& root )
                      { return FileUtil::startsWithPathComponent( selectedFolder, FileUtil::makeComparableDiskPath( root._absolutePath.str() ) ); } );
        const bool               bInHiddenGamePack =
            bSelectedIsUnderRoot == false && gamePacksFolder.empty() == false && FileUtil::startsWithPathComponent( selectedFolder, FileUtil::makeComparableDiskPath( gamePacksFolder ) );
        if ( ( selectedFolder.empty() || bInHiddenGamePack ) && _listRoot.empty() == false )
            openFolder( _listRoot.front()._absolutePath );

        _bRootsDirty = SW_FALSE;
    }

    void ContentBrowserViewModel::setShowAllPacks( bool bShowAllPacks )
    {
        _bShowAllPacks            = bShowAllPacks;
        _bShowAllPacksInitialized = SW_TRUE;
        _bRootsDirty              = SW_TRUE;
    }

    uint32 ContentBrowserViewModel::getGameRootCount() const
    {
        uint32 gameRootCount{ 0 };
        for ( const ContentBrowserRoot& root : _listRoot )
        {
            if ( StringUtil::startsWith( root._displayName, ContentBrowserViewModelInternal::kGameDomainPrefix ) )
                ++gameRootCount;
        }
        return gameRootCount;
    }

    bool ContentBrowserViewModel::hasRoot( string_view displayName ) const
    {
        return VectorUtil::containsIf( _listRoot, [&]( const ContentBrowserRoot& root )
        { return FileUtil::normalizePath( root._displayName ) == FileUtil::normalizePath( displayName ); } );
    }

    void ContentBrowserViewModel::refreshCurrentFolder()
    {
        _folderJob.request( _selectedFolderAbs, _bListingRecursive );
        _bFolderDirty = SW_FALSE;
    }

    void ContentBrowserViewModel::applyFolderListing( vector<EditorFolderListingEntry>& listEntry )
    {
        _listEntry = std::move( listEntry );
        VectorUtil::sort( _listEntry, []( const AssetEntry& entryA, const AssetEntry& entryB )
        {
            if ( entryA._bIsDirectory != entryB._bIsDirectory )
                return entryA._bIsDirectory > entryB._bIsDirectory;
            return entryA._name < entryB._name;
        } );
    }

    void ContentBrowserViewModel::syncWithContentChanges()
    {
        const uint64 serial = ContentBrowserViewModelInternal::getContentChangeSerial();
        if ( serial == _seenContentChangeSerial )
            return;
        _seenContentChangeSerial = serial;
        _bFolderDirty            = SW_TRUE;
        _folderCache.clear();
    }

    void ContentBrowserViewModel::openFolder( const AbsolutePath& absolutePath )
    {
        vector<ContentBrowserBreadcrumb> listBreadcrumb;
        makeTrailForFolder( absolutePath, listBreadcrumb );
        selectFolder( absolutePath, listBreadcrumb );
    }

    void ContentBrowserViewModel::selectFolder( const AbsolutePath& absolutePath, const vector<ContentBrowserBreadcrumb>& listBreadcrumb, bool bRecordHistory )
    {
        const AbsolutePath normalizedPath = absolutePath;
        if ( bRecordHistory )
            _history.record( normalizedPath, listBreadcrumb );

        // 탐색과 I/O 는 실제 파일 시스템의 대소문자를 쓰고, 새로 만드는 파일 이름은 소문자다(리소스 id 규칙).
        _selectedFolderAbs = normalizedPath;
        _listBreadcrumb    = listBreadcrumb;
        _selectedAssetAbs  = {};
        _bFolderDirty      = SW_TRUE;
    }

    void ContentBrowserViewModel::selectBreadcrumb( size_t breadcrumbIndex )
    {
        if ( breadcrumbIndex >= _listBreadcrumb.size() )
            return;
        // 조각까지 잘라 둔 사본을 넘긴다 — selectFolder 가 `_listBreadcrumb` 를 덮어쓴다.
        const vector<ContentBrowserBreadcrumb> listBreadcrumb( _listBreadcrumb.begin(), _listBreadcrumb.begin() + static_cast<std::ptrdiff_t>( breadcrumbIndex + 1 ) );
        selectFolder( listBreadcrumb.back()._absolutePath, listBreadcrumb );
    }

    void ContentBrowserViewModel::makeTrailForFolder( const AbsolutePath& folderAbs, vector<ContentBrowserBreadcrumb>& outListBreadcrumb ) const
    {
        for ( const ContentBrowserRoot& root : _listRoot )
        {
            if ( FileUtil::isPathAtOrUnderRoot( root._absolutePath.str(), folderAbs.str() ) )
            {
                ContentBrowserBreadcrumb::makeFolderTrail( root._displayName, root._absolutePath, folderAbs, outListBreadcrumb );
                return;
            }
        }
        ContentBrowserBreadcrumb::makeFolderTrail( {}, {}, folderAbs, outListBreadcrumb );
    }

    void ContentBrowserViewModel::navigateBack()
    {
        // 이력을 적지 않고 고르므로 항목 포인터는 selectFolder 안에서도 유효하다.
        const ContentBrowserHistoryEntry* pEntry = _history.stepBack();
        if ( pEntry != nullptr )
            selectFolder( pEntry->_folderPathAbs, pEntry->_listBreadcrumb, false );
    }

    void ContentBrowserViewModel::navigateForward()
    {
        const ContentBrowserHistoryEntry* pEntry = _history.stepForward();
        if ( pEntry != nullptr )
            selectFolder( pEntry->_folderPathAbs, pEntry->_listBreadcrumb, false );
    }

    void ContentBrowserViewModel::collectVisibleEntries( string_view searchText, vector<const AssetEntry*>& outListVisible ) const
    {
        const EditorListFilter filter{ searchText };
        outListVisible.clear();
        outListVisible.reserve( _listEntry.size() );
        for ( const AssetEntry& entry : _listEntry )
        {
            const bool bPassesType = entry._bIsDirectory || _typeFilter.matches( entry._name );
            if ( bPassesType && filter.matches( entry._name ) )
                outListVisible.push_back( &entry );
        }
    }

    void ContentBrowserViewModel::selectAsset( const AssetEntry& entry )
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

    AbsolutePath ContentBrowserViewModel::findAssetFile( string_view resourceID )
    {
        return EditorAssetFileCommands::findOperationPath( resourceID ).value_or( AbsolutePath{} );
    }

    bool ContentBrowserViewModel::revealAsset( string_view assetID )
    {
        const optional<AbsolutePath> filePath = EditorAssetFileCommands::findOperationPath( FileUtil::normalizePath( assetID ).str() ); // 디스크 파일(없으면 비어 있다)
        if ( filePath.has_value() == false )
            return false; // 쿠킹 묶음에만 있는 애셋 — 보일 폴더가 없다
        openFolder( filePath->getParent() );
        // 폴더를 고르면 선택이 비므로 그 뒤에 고른다 — 목록이 새로 와도 경로로 고르므로 그 타일이 고른 채 선다.
        _selectedAssetAbs = *filePath;
        editor::getSubsystem<EditorContext>().getSubsystem<EditorWorkspace>().setFocusedAssetPath( string{ assetID }.c_str() );
        return true;
    }

    void ContentBrowserViewModel::selectAssetPath( const AbsolutePath& absolutePath )
    {
        for ( const AssetEntry& entry : _listEntry )
        {
            if ( FileUtil::isSameDiskPath( entry._absolutePath.str(), absolutePath.str() ) )
            {
                selectAsset( entry );
                return;
            }
        }
    }

    void ContentBrowserViewModel::openAsset( const AssetEntry& entry )
    {
        if ( entry._bIsDirectory )
        {
            vector<ContentBrowserBreadcrumb> listBreadcrumb = _listBreadcrumb;
            ContentBrowserBreadcrumb::appendChildBreadcrumb( entry._absolutePath, listBreadcrumb );
            selectFolder( entry._absolutePath, listBreadcrumb );
            return;
        }

        selectAsset( entry );
        SW_LOG_TRACE_ID( "ContentBrowserViewModel.Open", "Open: %#", entry._relativePath.c_str() );
        (void)EditorAssetCommands::openPath( entry._relativePath ); // 실패는 openPath 가 알린다
    }

    bool ContentBrowserViewModel::isSelected( const AssetEntry& entry, bool bFolderToo ) const
    {
        if ( FileUtil::isSameDiskPath( entry._absolutePath.str(), _selectedAssetAbs.str() ) )
            return true;
        return bFolderToo && entry._bIsDirectory && FileUtil::isSameDiskPath( entry._absolutePath.str(), _selectedFolderAbs.str() );
    }

    void ContentBrowserViewModel::selectAfterFileChange( const AbsolutePath& absolutePath )
    {
        // 감시도 같은 변경을 알리지만 한두 프레임 늦다. 결과를 아는 위치에서 바로 다시 읽는다.
        _selectedAssetAbs = absolutePath;
        _bFolderDirty     = SW_TRUE;
        _folderCache.clear();
        // 이 변경 전에 시작한 스캔은 이동이 고친 색인을 오래된 결과로 덮어쓴다. 그 결과를 버리고 새로 스캔한다.
        ContentBrowserViewModelInternal::getHotReload().restartPendingReferenceScan();
    }

    bool ContentBrowserViewModel::createNewFolder()
    {
        AbsolutePath folderAbs;
        if ( ContentBrowserFileActions::createFolder( _selectedFolderAbs, folderAbs ) == false )
            return false;
        selectAfterFileChange( folderAbs );
        return true;
    }

    bool ContentBrowserViewModel::createNewAsset( const EditorNewAssetTemplate& assetTemplate )
    {
        AbsolutePath assetAbs;
        if ( ContentBrowserFileActions::createAsset( _selectedFolderAbs, assetTemplate, assetAbs ) == false )
            return false;
        selectAfterFileChange( assetAbs );
        return true;
    }

    bool ContentBrowserViewModel::duplicateAsset( const AbsolutePath& absolutePath )
    {
        AbsolutePath assetAbs;
        if ( ContentBrowserFileActions::duplicateAsset( absolutePath, assetAbs ) == false )
            return false;
        selectAfterFileChange( assetAbs );
        return true;
    }

    bool ContentBrowserViewModel::beginRename( const AbsolutePath& absolutePath, string& outStem )
    {
        const bool bStarted = _fileActions.beginRename( absolutePath, outStem );
        if ( bStarted )
            _pendingMoves.discardRename();
        return bStarted;
    }

    void ContentBrowserViewModel::cancelRename()
    {
        _fileActions.cancelRename();
        _pendingMoves.discardRename();
    }

    EditorAssetIndex* ContentBrowserViewModel::findCurrentReferenceIndex()
    {
        return ContentBrowserViewModelInternal::getHotReload().findCurrentReferenceIndex();
    }

    void ContentBrowserViewModel::applyPendingMove()
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

    bool ContentBrowserViewModel::commitRename( string_view newName )
    {
        EditorAssetIndex* pIndex = findCurrentReferenceIndex();
        if ( pIndex == nullptr )
        {
            _pendingMoves.addRename( newName );
            SW_LOG_INFO_ID( "ContentBrowserViewModel.MoveWaitsForReferenceIndex", "Rename waits for the reference index to catch up with the last file change" );
            return true;
        }
        AbsolutePath destinationAbs;
        if ( _fileActions.commitRename( newName, *pIndex, destinationAbs ) == false )
            return false;
        selectAfterFileChange( destinationAbs );
        return true;
    }

    bool ContentBrowserViewModel::isRenaming( const AssetEntry& entry ) const
    {
        const AbsolutePath& renamingAbs = _fileActions.getRenamingPath();
        return renamingAbs.empty() == false && FileUtil::isSameDiskPath( entry._absolutePath.str(), renamingAbs.str() );
    }

    bool ContentBrowserViewModel::moveAssetToFolder( const AbsolutePath& sourceAbs, const AbsolutePath& folderAbs )
    {
        EditorAssetIndex* pIndex = findCurrentReferenceIndex();
        if ( pIndex == nullptr )
        {
            _pendingMoves.addMove( sourceAbs, folderAbs );
            SW_LOG_INFO_ID( "ContentBrowserViewModel.MoveWaitsForReferenceIndex", "Move waits for the reference index to catch up with the last file change" );
            return true;
        }
        AbsolutePath destinationAbs;
        if ( ContentBrowserFileActions::moveAssetToFolder( sourceAbs, folderAbs, *pIndex, destinationAbs ) == false )
            return false;
        selectAfterFileChange( destinationAbs );
        return true;
    }

    void ContentBrowserViewModel::requestMoveToFolder( const AbsolutePath& sourceAbs, const AbsolutePath& folderAbs )
    {
        _moveRequestSourceAbs = sourceAbs;
        _moveRequestFolderAbs = folderAbs;
        _moveRequestPlan      = EditorAssetMovePlan{};
        _bMoveRequestPlanned  = SW_FALSE;
    }

    bool ContentBrowserViewModel::updateMoveRequest()
    {
        if ( hasMoveRequest() == false )
            return false;
        if ( _bMoveRequestPlanned == SW_TRUE )
            return true;
        const EditorAssetIndex* pIndex = findCurrentReferenceIndex();
        if ( pIndex == nullptr )
            return false;                                                                                                             // 역색인이 마지막 변경을 따라오면 계획한다 — 낡은 색인으로 세운 계획은 고칠 참조를 빠뜨린다
        (void)ContentBrowserFileActions::planMoveToFolder( _moveRequestSourceAbs, _moveRequestFolderAbs, *pIndex, _moveRequestPlan ); // 옮길 수 없으면 `_failure` 를 창이 보인다
        _bMoveRequestPlanned = SW_TRUE;
        return true;
    }

    bool ContentBrowserViewModel::confirmMoveRequest()
    {
        const AbsolutePath sourceAbs = _moveRequestSourceAbs;
        const AbsolutePath folderAbs = _moveRequestFolderAbs;
        cancelMoveRequest();
        if ( sourceAbs.empty() )
            return false;
        return moveAssetToFolder( sourceAbs, folderAbs ); // 적용 때 계획을 다시 세운다(확인을 기다리는 사이 파일이 바뀌었을 수 있다)
    }

    void ContentBrowserViewModel::cancelMoveRequest()
    {
        _moveRequestSourceAbs = {};
        _moveRequestFolderAbs = {};
        _moveRequestPlan      = EditorAssetMovePlan{};
        _bMoveRequestPlanned  = SW_FALSE;
    }

    bool ContentBrowserViewModel::confirmDeleteAsset()
    {
        if ( _fileActions.confirmDelete() == false )
            return false;
        // 지운 파일이 목록과 선택에 남지 않도록 지금 폴더를 다시 읽는다. 파일 감시도 같은 변경을 알리지만 한두 프레임 늦다.
        _selectedAssetAbs = {};
        _bFolderDirty     = SW_TRUE;
        _folderCache.clear();
        return true;
    }

    void ContentBrowserViewModel::importFilesFromDialog()
    {
        if ( _selectedFolderAbs.empty() )
        {
            SW_LOG_WARNING_ID( "ContentBrowserViewModel.SelectDestinationFolderBefore", "Select a destination folder before importing." );
            return;
        }
        _importFolderAbs = _selectedFolderAbs;
        _importQueue.openDialog( _importFolderAbs.str() );
    }

    uint32 ContentBrowserViewModel::importDroppedFiles( const vector<string>& listPath, const AbsolutePath& destFolderAbs )
    {
        if ( destFolderAbs.empty() )
        {
            SW_LOG_WARNING_ID( "ContentBrowserViewModel.ImportCancelledNoDestination", "Import cancelled — no destination folder." );
            return 0;
        }
        const EditorToolDefaults&   defaults = editor::getEditorToolDefaults();
        ContentBrowserFileDropRules rules{};
        EditorAssetTypeRegistry::appendImportExtensions( rules._listExtension );
        rules._maxBytes = static_cast<uint64>( defaults._fileDropMaxMegabytes ) * constant::memory::kBytesPerMegabyte;
        rules._maxCount = defaults._fileDropMaxCount;

        vector<ContentBrowserDroppedFile> listFile;
        ContentBrowserFileDropPlan::collectDroppedFiles( listPath, listFile );
        ContentBrowserFileDropPlan plan;
        ContentBrowserFileDropPlan::make( listFile, rules, plan );
        for ( const ContentBrowserRejectedFile& rejected : plan._listRejected )
        {
            SW_LOG_WARNING_ID( "ContentBrowser.FileDropRejected", "Dropped file not imported (%#): %#", ContentBrowserFileDropPlan::describeRejection( rejected._reason ), rejected._path.c_str() );
        }
        const uint32 imported = plan._listImport.empty() ? 0u : EditorAssetFileCommands::importFiles( destFolderAbs, plan._listImport );
        if ( imported > 0 )
            _bFolderDirty = SW_TRUE;
        SW_LOG_INFO_ID( "ContentBrowser.FileDropImported", "Imported %# of %# dropped file(s) into %#", imported, static_cast<uint64>( listPath.size() ), destFolderAbs.str() );
        return imported;
    }

    void ContentBrowserViewModel::processPendingImports()
    {
        vector<string> listPath;
        if ( _importQueue.takePending( listPath ) == false )
            return;
        if ( _importFolderAbs.empty() )
        {
            SW_LOG_WARNING_ID( "ContentBrowserViewModel.ImportCancelledNoDestination", "Import cancelled — no destination folder." );
            return;
        }
        vector<AbsolutePath> listSourcePath;
        for ( const string& pickedPath : listPath )
        {
            listSourcePath.push_back( FileUtil::toDiskPath( pickedPath ) ); // 파일 대화상자가 고른 글이다
        }
        if ( EditorAssetFileCommands::importFiles( _importFolderAbs, listSourcePath ) > 0 )
            _bFolderDirty = SW_TRUE;
    }

    bool ContentBrowserViewModel::isReferenceIndexCurrent() const
    {
        return ContentBrowserViewModelInternal::getHotReload().isReferenceIndexCurrent();
    }

    void ContentBrowserViewModel::showReferences( const AbsolutePath& absolutePath, bool bDependencies )
    {
        _referenceSearch.query( getReferenceIndex(), absolutePath, bDependencies );
    }

    const EditorAssetIndex& ContentBrowserViewModel::getReferenceIndex()
    {
        return ContentBrowserViewModelInternal::getHotReload().getReferenceIndex();
    }

    bool ContentBrowserViewModel::isReferenceScanPending()
    {
        return ContentBrowserViewModelInternal::getHotReload().isReferenceScanPending();
    }

    string ContentBrowserViewModel::describeSourceControlStatus( const AssetEntry& entry )
    {
        if ( entry._bIsDirectory )
            return {};
        return ContentBrowserViewModelInternal::getSourceControl().describeStatus( entry._absolutePath, entry._bReadOnly );
    }

    bool ContentBrowserViewModel::isLocked( const AssetEntry& entry )
    {
        return ContentBrowserViewModelInternal::getSourceControl().findLock( entry._absolutePath ) != nullptr;
    }

    bool ContentBrowserViewModel::openAsset( string_view assetPath )
    {
        return EditorAssetCommands::openPath( assetPath ); // 실패는 openPath 가 알린다
    }

    void ContentBrowserViewModel::showInFileExplorer( string_view absolutePath )
    {
        (void)EditorAssetFileCommands::showInFileExplorer( absolutePath ); // 실패는 그 함수가 로그에 남긴다
    }

    EditorSourceControl& ContentBrowserViewModel::getSourceControl()
    {
        return ContentBrowserViewModelInternal::getSourceControl();
    }

    string ContentBrowserViewModel::findMainSelectionAssetID()
    {
        // 선택 동기화: 메인 씬 칸만 따른다 — 미리보기 창(프리팹 창)에서 고른 부품은 콘텐츠 브라우저를 움직이지 않는다(`EditorSelectionSync`).
        const EditorSelectionSync*      pSync = editor::findSubsystem<EditorSelectionSync>();
        const EditorSelectionSyncEntry* pMain = pSync != nullptr ? pSync->findLatest( makeSelectionContextBit( EditorSelectionContext::MainScene ) ) : nullptr;
        return pMain != nullptr ? pMain->_assetPath : string{};
    }

    string ContentBrowserViewModel::getGamePackRoot()
    {
        return editor::getSubsystem<const GameConfig>()._packRoot.get();
    }

    bool ContentBrowserViewModel::readReferenceIndexCurrent( bool& outCurrent )
    {
        const EditorContext* pContext = editor::findSubsystem<EditorContext>();
        if ( pContext == nullptr )
            return false;
        outCurrent = pContext->getSubsystem<AssetHotReload>().isReferenceIndexCurrent();
        return true;
    }
} // namespace sw::editor

namespace sw::editor
{
    ContentBrowserReferenceSearch::ContentBrowserReferenceSearch()
        : _queryID{}
        , _listResult{}
        , _bDependencyQuery{ SW_FALSE }
    {
    }

    void ContentBrowserReferenceSearch::query( const EditorAssetIndex& index, const AbsolutePath& absolutePath, bool bDependencies )
    {
        _queryID          = editor::getSubsystem<AssetPaths>().toAssetID( absolutePath );
        _bDependencyQuery = bDependencies ? SW_TRUE : SW_FALSE;
        if ( bDependencies )
            index.findDependencies( _queryID, _listResult );
        else
            index.findReferrers( _queryID, _listResult );
    }
} // namespace sw::editor
