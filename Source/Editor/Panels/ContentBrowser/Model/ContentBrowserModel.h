/**
 * @file ContentBrowserModel.h
 * @brief 콘텐츠 브라우저의 상태와 동작입니다 — 루트, 지금 폴더와 목록, 이력, 선택, 이름 바꾸기 · 삭제 대기, 임포트, 참조 역색인, 종류 필터(ImGui 없음).
 */
#pragma once
#include "Core/Concurrency/Queue/LockedSwapQueue.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Asset/Reference/EditorReferenceIndex.h"
#include "Editor/Common/Commands/Asset/EditorFolderListingEntry.h"
#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserBreadcrumb.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFileActions.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserReferenceSearch.h"

namespace sw::editor
{
    /**
     * @class ContentBrowserTypeFilter
     * @brief 종류 필터 목록을 종류 등록에서 만들고, 고른 필터로 항목을 거릅니다.
     * @details 종류는 플러그인 모듈이 올라오고 내려갈 때 바뀐다. 목록을 세대(`EditorAssetTypeRegistry::getGeneration`)가 바뀔 때 다시 만들고,
     *          고른 필터는 순번이 아니라 종류로 기억한다 — 앞에 종류가 끼어도 고른 필터가 다른 종류로 바뀌지 않는다. 고른 종류가 사라지면 All 로 돌아간다.
     */
    class ContentBrowserTypeFilter
    {
    public:
        ContentBrowserTypeFilter();

        /** @brief 종류 등록이 바뀌었으면 목록을 다시 만듭니다. 그리기 전에 부릅니다. */
        void sync();

        /** @brief 필터 목록입니다(`sync` 뒤). 맨 앞이 All, 맨 뒤가 Other 입니다. */
        const vector<EditorAssetBrowserFilter>& getFilters() const { return _listFilter; }
        /** @brief 고른 필터의 순번입니다. */
        uint32 getSelectedIndex() const { return _selectedIndex; }
        /** @brief 고른 필터의 라벨입니다. 목록이 아직 없으면 "All" 입니다. */
        const utf8* getSelectedLabel() const;
        /** @brief 순번 @p index 의 필터를 고릅니다. 범위 밖이면 무시합니다. */
        void select( uint32 index );

        /** @brief 파일 @p path 가 고른 필터를 지나면 true 입니다. 폴더는 부르는 쪽이 거르지 않습니다. */
        bool matches( string_view path ) const;

    private:
        vector<EditorAssetBrowserFilter> _listFilter;
        EditorAssetType                  _selectedKind;       ///< 고른 종류. All · Other 는 빈 값이다
        uint32                           _selectedIndex;      ///< `_listFilter` 안의 순번
        uint32                           _generation;         ///< 목록을 만든 종류 등록의 세대
        uint8                            _bSelectedOther : 1; ///< Other 를 골랐다
        uint8                            _bBuilt         : 1; ///< 목록을 한 번이라도 만들었다
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @class ContentBrowserImportQueue
     * @brief 임포트할 원본 경로를 스레드 사이로 넘기는 대기열입니다.
     * @details 파일 대화상자는 백그라운드에서 열리고 결과도 그 스레드에서 온다(`onDialogResult`). 임포트는 에셋 파일을 쓰고 목록을 다시 읽게 하므로
     *          메인 스레드에서 `takePending` 으로 꺼내 실행한다. 받는 쪽과 꺼내는 쪽이 다른 스레드라 뮤텍스 하나로 지킨다.
     */
    class ContentBrowserImportQueue
    {
    public:
        /** @brief 빈 대기열을 만듭니다. */
        ContentBrowserImportQueue();

        /**
         * @brief 임포트할 파일을 고르는 대화상자를 엽니다. 고른 경로는 `onDialogResult` 로 들어옵니다.
         * @param initialFolderAbs 대화상자가 처음 보일 폴더입니다. 임포트 대상 폴더를 넘깁니다.
         * @warning 대화상자가 닫힐 때 이 객체의 주소로 결과를 넘깁니다. 대화상자가 열려 있는 동안 객체를 옮기거나 지우지 않습니다.
         */
        void openDialog( string_view initialFolderAbs );
        /** @brief 파일 대화상자에서 고른 경로가 들어왔습니다. 어느 스레드에서 불러도 됩니다. */
        void onDialogResult( const vector<string>& listPath );
        /** @brief 쌓인 경로를 모두 꺼내 @p outListPath 에 둡니다. 꺼낼 것이 없으면 false 이고 @p outListPath 는 비웁니다. */
        bool takePending( vector<string>& outListPath );

    private:
        LockedSwapQueue<string> _pendingPath;
    };
} // namespace sw::editor

namespace sw::editor
{
    class EditorSourceControl;

    struct EditorNewAssetTemplate;

    /** @brief 이름이 있는 콘텐츠 루트입니다. 표시 이름과 절대 경로를 가집니다. */
    struct ContentBrowserRoot
    {
        string _displayName;
        string _absolutePath;
    };

    /**
     * @class ContentBrowserModel
     * @brief 콘텐츠 브라우저 창(`ContentBrowserPanel`)의 모델입니다. 창은 이것을 들고 그리기만 하고, 디스크 · 선택 · 색인을 바꾸는 일은 모두 여기를 지납니다.
     * @details 언리얼 Content Browser 의 `SContentBrowser`(뷰)와 에셋 레지스트리 · 경로 상태(모델)를 나눈 것과 같은 선이다. 인스펙터의 `Model/` 처럼
     *          ImGui 를 include 하지 않아 화면 없이 부를 수 있다. 창 상태(입력란 글, 타일 크기, 팝업을 열 표시)는 창이 가진다.
     */
    class ContentBrowserModel
    {
    public:
        using AssetEntry = EditorFolderListingEntry;

        ContentBrowserModel() noexcept;

        // ------------------------------------------------------------------------------
        // 0) 에디터 창구 — MVVM View(`ContentBrowserPanel`)가 작업 공간 · 명령 헤더를 보지 않게 서비스 접근을 여기 모은다(`docs/agents/EditorUI.md`)
        // ------------------------------------------------------------------------------
        /** @brief 에셋 @p assetPath(리소스 id 또는 디스크 경로)를 그 종류의 도구로 엽니다. 열지 못하면 false 이고 이유는 알림 · 로그에 남는다. */
        static bool openAsset( string_view assetPath );
        /** @brief OS 파일 탐색기에서 @p absolutePath 를 보입니다. */
        static void showInFileExplorer( string_view absolutePath );
        /** @brief 버전 관리 창구입니다(잠금 배지 · 체크아웃 메뉴). */
        [[nodiscard]] static EditorSourceControl& getSourceControl();
        /** @brief 메인 씬 선택이 가리키는 에셋의 리소스 id 입니다(선택 동기화 — 미리보기 창의 선택은 따르지 않는다). 없으면 빈 글입니다. */
        [[nodiscard]] static string findMainSelectionAssetID();
        /** @brief 게임 팩 루트(`GameConfig::_packRoot`)입니다(즐겨찾기 폴더). */
        [[nodiscard]] static string getGamePackRoot();
        /** @brief 참조 역색인이 마지막 `Content/` 변경을 반영했으면 @p outCurrent 가 true 입니다. 에디터 컨텍스트가 없으면 false 를 돌려줍니다(감시 값). */
        [[nodiscard]] static bool readReferenceIndexCurrent( bool& outCurrent );

        // ------------------------------------------------------------------------------
        // 1) 프레임 — 창이 내용을 그리기 전에 한 번 부른다
        // ------------------------------------------------------------------------------
        /**
         * @brief 루트 · 임포트 · `Content/` 변경 · 참조 역색인 · 폴더 목록을 맞춥니다.
         * @param bSearchActive 검색어가 있으면 하위 폴더의 파일까지 본다(언리얼과 유니티의 프로젝트 검색과 같다).
         */
        void tick( bool bSearchActive );

        // ------------------------------------------------------------------------------
        // 2) 루트와 폴더
        // ------------------------------------------------------------------------------
        const vector<ContentBrowserRoot>&       getRoots() const { return _listRoot; }
        const vector<AssetEntry>&               getEntries() const { return _listEntry; }
        const string&                           getSelectedFolderPath() const { return _selectedFolderAbs; }
        const vector<ContentBrowserBreadcrumb>& getBreadcrumbs() const { return _listCrumb; }
        /** @brief 폴더 트리의 하위 폴더입니다. 처음 볼 때 한 번 디스크에서 읽는다. 참조는 재귀 중에도 유효하다. */
        const vector<string>& getOrScanChildFolders( string_view folderAbs ) { return _folderCache.getOrScanChildFolders( folderAbs ); }
        /** @brief 폴더 트리 캐시를 비웁니다. 다음 그리기에서 디스크를 다시 읽는다. */
        void clearFolderTreeCache() { _folderCache.clear(); }
        /** @brief 폴더 목록을 다시 읽도록 예약했거나 읽는 중이면 true 입니다. */
        bool isFolderRefreshPending() const { return _bFolderDirty || _folderJob.isPending(); }
        /** @brief 루트 · 폴더 트리 · 목록 · 선택을 비우고 다시 읽습니다(툴바 Refresh). */
        void refreshAll();

        /** @brief 폴더로 이동합니다. 그 폴더가 속한 콘텐츠 루트에서 브레드크럼 경로도 함께 만듭니다. */
        void openFolder( string_view absolutePath );
        /** @brief 폴더를 고르고 목록을 다시 읽게 합니다. @p listCrumb 는 그 폴더의 브레드크럼 경로입니다. */
        void selectFolder( string_view absolutePath, const vector<ContentBrowserBreadcrumb>& listCrumb, bool bRecordHistory = true );
        /** @brief 지금 브레드크럼 경로의 @p breadcrumbIndex 번째 조각까지로 이동합니다. */
        void selectBreadcrumb( size_t breadcrumbIndex );
        /** @brief 폴더가 속한 콘텐츠 루트에서 그 폴더까지의 브레드크럼 경로를 만듭니다. */
        void makeTrailForFolder( string_view folderAbs, vector<ContentBrowserBreadcrumb>& outListCrumb ) const;
        bool canNavigateBack() const { return _history.canNavigateBack(); }
        bool canNavigateForward() const { return _history.canNavigateForward(); }
        void navigateBack();
        void navigateForward();

        /** @brief 게임 팩을 모두 보일지 정합니다. 끄면 활성 게임 팩(`GameConfig::_packRoot`) 하나만 루트로 둡니다. */
        void setShowAllPacks( bool bShowAllPacks );
        bool isShowingAllPacks() const { return _bShowAllPacks; }
        /** @brief `game/` 아래 루트의 수입니다. 활성 팩만 보이면 1 입니다. */
        uint32 getGameRootCount() const;
        /** @brief 그 표시 이름(`game/empty`, `engine`)을 가진 루트가 있으면 true 입니다. */
        bool hasRoot( string_view displayName ) const;

        // ------------------------------------------------------------------------------
        // 3) 필터와 선택
        // ------------------------------------------------------------------------------
        ContentBrowserTypeFilter&       getTypeFilter() { return _typeFilter; }
        const ContentBrowserTypeFilter& getTypeFilter() const { return _typeFilter; }
        /** @brief 종류 필터와 검색어 @p searchText 를 지나는 항목을 @p outListVisible 에 씁니다. 항목을 복사하지 않고 가리키기만 합니다. */
        void collectVisibleEntries( string_view searchText, vector<const AssetEntry*>& outListVisible ) const;

        const string& getSelectedAssetPath() const { return _selectedAssetAbs; }
        /** @brief 항목을 고릅니다. 파일이면 작업 공간 포커스도 그 에셋으로 옮긴다. */
        void selectAsset( const AssetEntry& entry );
        /** @brief 지금 목록에 있는 @p absolutePath 항목을 고릅니다. 목록에 없으면 아무 일도 없습니다. */
        void selectAssetPath( string_view absolutePath );
        /**
         * @brief 리소스 id @p assetID 의 폴더로 가서 그 애셋을 고릅니다(언리얼 "Browse to Asset" · Ctrl+B). 메인 씬에서 고른 오브젝트의 애셋(`EditorSelectionSync`)을 찾아갈 때 쓴다.
         * @return 디스크에 없는 애셋(쿠킹 묶음에만 있다)이면 false 이고 아무것도 바꾸지 않는다.
         */
        [[nodiscard]] bool revealAsset( string_view assetID );
        /** @brief 폴더면 들어가고, 파일이면 고르고 엽니다(`EditorAssetCommands::openPath`). */
        void openAsset( const AssetEntry& entry );
        /** @brief 항목이 고른 것이면 true 입니다. @p bFolderToo 면 지금 폴더인 폴더 항목도 고른 것으로 칩니다(타일 보기). */
        bool isSelected( const AssetEntry& entry, bool bFolderToo ) const;

        // ------------------------------------------------------------------------------
        // 4) 파일 관리 — 성공하면 결과를 고르고 목록을 다시 읽게 한다
        // ------------------------------------------------------------------------------
        [[nodiscard]] bool createNewFolder();
        [[nodiscard]] bool createNewAsset( const EditorNewAssetTemplate& assetTemplate );
        bool               duplicateAsset( string_view absolutePath );
        /** @brief 이름 바꾸기를 시작합니다. 시작했으면 true 이고 @p outStem 에 확장자를 뺀 이름을 씁니다. 폴더는 받지 않습니다. */
        [[nodiscard]] bool beginRename( string_view absolutePath, string& outStem );
        /**
         * @brief 이름 바꾸기를 @p newName 으로 끝냅니다. 참조 역색인이 마지막 파일 변경을 따라오기 전이면 그때까지 미뤘다가 `tick` 이 실행한다(입력은 그동안 열려 있다).
         * @details 언리얼이 에셋 레지스트리 스캔이 끝날 때까지 이름 바꾸기를 미루는 것과 같다 — 거절하면 늦게 닿은 파일 변경 알림 하나가 사용자의 Enter 를 버린다.
         */
        bool commitRename( string_view newName );
        /** @brief 이름 바꾸기를 끝냅니다. 역색인을 기다리던 이름도 버립니다. */
        void          cancelRename();
        const string& getRenamingPath() const { return _fileActions.getRenamingPath(); }
        bool          isRenaming( const AssetEntry& entry ) const;
        /** @brief 에셋을 @p folderAbs 로 옮기고 참조를 고칩니다. 역색인이 따라오기 전이면 `commitRename` 처럼 미룬다. */
        [[nodiscard]] bool moveAssetToFolder( string_view sourceAbs, string_view folderAbs );
        /**
         * @brief 끌어 놓은 에셋 · 폴더의 옮기기를 확인 대기로 둡니다. 창이 미리보기(옮길 파일 수 · 고칠 참조 · 바깥 도메인 · 고칠 수 없는 참조)를 보이고 확인을 받는다.
         * @details 언리얼 콘텐츠 브라우저가 놓을 때 Move Here 를 묻는 자리다. 계획은 역색인이 따라온 뒤에 세운다(`updateMoveRequest`).
         */
        void requestMoveToFolder( string_view sourceAbs, string_view folderAbs );
        /** @brief 확인을 기다리는 옮기기가 있으면 true 입니다. */
        bool hasMoveRequest() const { return _moveRequestSourceAbs.empty() == false; }
        /** @brief 역색인이 따라왔고 아직 계획하지 않았으면 계획합니다. 확인 대화상자가 그릴 때마다 부른다. 계획했으면 true 입니다. */
        bool updateMoveRequest();
        /** @brief 계획한 옮기기의 미리보기입니다(`updateMoveRequest` 가 true 일 때만 뜻이 있다). */
        const EditorAssetMovePlan& getMoveRequestPlan() const { return _moveRequestPlan; }
        /** @brief 확인을 기다리는 옮기기의 대상 폴더(디스크 경로)입니다. */
        const string& getMoveRequestFolderPath() const { return _moveRequestFolderAbs; }
        /** @brief 계획한 옮기기를 적용합니다(트랜잭션 — 지면 되돌린다). */
        bool confirmMoveRequest();
        /** @brief 기다리던 옮기기를 버립니다. */
        void cancelMoveRequest();
        /** @brief 지울 에셋을 정합니다. 확인은 창이 받는다. */
        void          requestDeleteAsset( string_view absolutePath ) { _fileActions.requestDelete( absolutePath ); }
        const string& getPendingDeletePath() const { return _fileActions.getPendingDeletePath(); }
        /** @brief 기다리던 에셋을 휴지통으로 보냅니다. 성공하면 선택을 비우고 목록을 다시 읽게 합니다. */
        bool confirmDeleteAsset();
        void cancelDeleteAsset() { _fileActions.cancelDelete(); }
        /** @brief 대화상자로 지금 폴더에 파일을 임포트합니다. 고른 경로는 뒤따르는 `tick` 이 처리하고, 대화상자를 연 동안 폴더를 옮겨도 연 때의 폴더로 들어간다. */
        void importFilesFromDialog();
        /** @brief 임포트할 폴더가 있으면 true 입니다. */
        bool canImport() const { return _selectedFolderAbs.empty() == false; }

        // ------------------------------------------------------------------------------
        // 5) 참조 찾기
        // ------------------------------------------------------------------------------
        /** @brief 참조 역색인이 에디터가 본 마지막 `Content/` 변경까지 반영했으면 true 입니다. */
        bool                                 isReferenceIndexCurrent() const;
        const ContentBrowserReferenceSearch& getReferenceSearch() const { return _referenceSearch; }
        /** @brief 에셋의 참조(@p bDependencies 면 의존)를 찾습니다. 결과는 `getReferenceSearch` 입니다. */
        void showReferences( string_view absolutePath, bool bDependencies );
        /** @brief 에디터 전역의 참조 역색인입니다(`AssetHotReload::getReferenceIndex`). */
        static const EditorReferenceIndex& getReferenceIndex();
        /** @brief 역색인 스캔이 워커에서 진행 중이면 true 입니다. */
        static bool isReferenceScanPending();

        /** @brief 항목의 버전 관리 상태 문자열입니다. 잠금이나 읽기 전용이 아니면(폴더 포함) 빈 문자열입니다. */
        static string describeSourceControlStatus( const AssetEntry& entry );
        /** @brief 항목이 잠겨 있으면 true 입니다(누가 걸었든). */
        static bool isLocked( const AssetEntry& entry );

    private:
        /** @brief 루트 목록을 다시 구성합니다. 지금 폴더가 숨긴 팩 안이면 첫 루트로 옮긴다. */
        void refreshRoots();
        /** @brief 지금 폴더의 목록을 워커에 청합니다. */
        void refreshCurrentFolder();
        /** @brief 워커가 읽어 온 목록을 폴더 먼저, 이름 순으로 적용합니다. */
        void applyFolderListing( vector<EditorFolderListingEntry>& listEntry );
        /** @brief `Content/` 가 바뀌었으면 목록과 폴더 트리를 다시 읽게 합니다. 에디터 밖에서 바꾼 경우도 포함합니다. */
        void syncWithContentChanges();
        /** @brief 임포트 대화상자가 고른 경로를 대화상자를 연 때의 폴더로 임포트합니다. */
        void processPendingImports();
        /** @brief 목록과 폴더 트리를 다시 읽게 하고 @p absolutePath 를 고릅니다. 이 변경 전에 시작한 색인 스캔은 버린다. */
        void selectAfterFileChange( string_view absolutePath );
        /** @brief 옮기기가 고칠 역색인입니다. 마지막 변경을 아직 반영하지 않았으면 nullptr 입니다. */
        EditorReferenceIndex* findCurrentReferenceIndex();
        /** @brief 역색인이 따라왔으면 미뤄 둔 옮기기를 실행합니다(`tick`). */
        void applyPendingMove();

    private:
        vector<ContentBrowserRoot>       _listRoot;
        vector<AssetEntry>               _listEntry;
        string                           _selectedFolderAbs;
        vector<ContentBrowserBreadcrumb> _listCrumb; /**< 지금 폴더의 브레드크럼 경로. 예: "Favorites / Shaders / bin" */
        string                           _selectedAssetAbs;
        string                           _importFolderAbs;      /**< 임포트 대화상자를 연 때의 폴더 */
        ContentBrowserHistory            _history;              /**< 뒤로 가기 · 앞으로 가기 이력 */
        ContentBrowserFileActions        _fileActions;          /**< 이름 바꾸기와 삭제 확인을 기다리는 상태, 디스크를 바꾸는 작업 */
        ContentBrowserMoveQueue          _pendingMoves;         /**< 역색인이 따라오기를 기다리는 옮기기(`commitRename` · `moveAssetToFolder`) */
        string                           _moveRequestSourceAbs; /**< 확인을 기다리는 끌어 놓기의 원본(파일 · 폴더). 비었으면 없다 */
        string                           _moveRequestFolderAbs; /**< 그 대상 폴더 */
        EditorAssetMovePlan              _moveRequestPlan;      /**< 그 계획(미리보기) */
        ContentBrowserReferenceSearch    _referenceSearch;      /**< 마지막 참조 찾기의 결과(색인은 `AssetHotReload` 의 것) */
        ContentBrowserImportQueue        _importQueue;          /**< 파일 대화상자가 고른 임포트 경로. 메인 스레드가 꺼내 임포트한다 */
        ContentBrowserTypeFilter         _typeFilter;           /**< 종류 필터 목록과 고른 필터. 종류 등록이 바뀌면 다시 만든다 */
        EditorFolderListingJob           _folderJob;
        ContentBrowserFolderCache        _folderCache;             /**< 폴더 트리의 하위 폴더. 그릴 때마다 디스크를 읽지 않도록 캐시한다 */
        uint64                           _seenContentChangeSerial; /**< 마지막으로 반영한 `AssetHotReload::getContentChangeSerial` */
        uint8                            _bRootsDirty              : 1;
        uint8                            _bFolderDirty             : 1;
        uint8                            _bShowAllPacks            : 1; /**< 게임 팩을 모두 보인다. 끄면 활성 팩 하나만 보인다 */
        uint8                            _bShowAllPacksInitialized : 1; /**< 환경설정의 기본값을 한 번 읽었다. 그 뒤에는 툴바가 정한다 */
        uint8                            _bListingRecursive        : 1; /**< 지금 목록에 하위 폴더의 파일까지 포함한다(검색 중) */
        uint8                            _bMoveRequestPlanned      : 1; /**< 확인을 기다리는 옮기기의 계획을 세웠다(역색인이 따라온 뒤) */
    };
} // namespace sw::editor
