/**
 * @file ContentBrowserModel.h
 * @brief 콘텐츠 브라우저의 상태와 동작입니다 — 루트, 지금 폴더와 목록, 이력, 선택, 이름 바꾸기 · 삭제 대기, 임포트, 참조 역색인, 종류 필터(ImGui 없음).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/Commands/Asset/EditorFolderListing.h"
#include "Editor/Common/Commands/Asset/EditorReferenceIndex.h"
#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Panels/ContentBrowser/ContentBrowserReferenceSearch.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserFileActions.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserImportQueue.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserLogic.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserTypeFilter.h"

namespace sw::editor
{
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
        const vector<ContentBrowserRoot>&  getRoots() const { return _listRoot; }
        const vector<AssetEntry>&          getEntries() const { return _listEntry; }
        const string&                      getSelectedFolderPath() const { return _selectedFolderAbs; }
        const vector<ContentBrowserCrumb>& getCrumbs() const { return _listCrumb; }
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
        void selectFolder( string_view absolutePath, const vector<ContentBrowserCrumb>& listCrumb, bool bRecordHistory = true );
        /** @brief 지금 브레드크럼 경로의 @p crumbIndex 번째 조각까지로 이동합니다. */
        void selectCrumb( size_t crumbIndex );
        /** @brief 폴더가 속한 콘텐츠 루트에서 그 폴더까지의 브레드크럼 경로를 만듭니다. */
        void makeTrailForFolder( string_view folderAbs, vector<ContentBrowserCrumb>& outListCrumb ) const;
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
        /** @brief 이름 바꾸기를 @p newName 으로 끝냅니다. 참조 역색인이 따라오기 전이면 경고하고 false 입니다(입력은 열어 둔다). */
        bool          commitRename( string_view newName );
        void          cancelRename() { _fileActions.cancelRename(); }
        const string& getRenamingPath() const { return _fileActions.getRenamingPath(); }
        bool          isRenaming( const AssetEntry& entry ) const;
        /** @brief 에셋을 @p folderAbs 로 옮기고 참조를 고칩니다. */
        [[nodiscard]] bool moveAssetToFolder( string_view sourceAbs, string_view folderAbs );
        /** @brief 지울 에셋을 정합니다. 확인은 창이 받는다. */
        void          requestDeleteAsset( string_view absolutePath ) { _fileActions.requestDelete( absolutePath ); }
        const string& getPendingDeletePath() const { return _fileActions.getPendingDeletePath(); }
        /** @brief 기다리던 에셋을 휴지통으로 보냅니다. 성공하면 선택을 비우고 목록을 다시 읽게 합니다. */
        bool confirmDeleteAsset();
        void cancelDeleteAsset() { _fileActions.cancelDelete(); }
        /** @brief 대화상자로 지금 폴더에 파일을 임포트합니다. 고른 경로는 다음 `tick` 이 처리한다. */
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
        void showReferences( string_view absolutePath, bool bDependencies ) { _referenceSearch.query( absolutePath, bDependencies ); }

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
        /** @brief 임포트 대화상자가 고른 경로를 지금 폴더로 임포트합니다. */
        void processPendingImports();
        /** @brief 목록과 폴더 트리를 다시 읽게 하고 @p absolutePath 를 고릅니다. 이 변경 전에 시작한 색인 스캔은 버린다. */
        void selectAfterFileChange( string_view absolutePath );
        /** @brief 옮기기가 고칠 역색인입니다. 마지막 변경을 아직 반영하지 않았으면 경고하고 nullptr 입니다. */
        EditorReferenceIndex* findCurrentReferenceIndex();

    private:
        vector<ContentBrowserRoot>    _listRoot;
        vector<AssetEntry>            _listEntry;
        string                        _selectedFolderAbs;
        vector<ContentBrowserCrumb>   _listCrumb; /**< 지금 폴더의 브레드크럼 경로. 예: "Favorites / Shaders / bin" */
        string                        _selectedAssetAbs;
        ContentBrowserHistory         _history;         /**< 뒤로 가기 · 앞으로 가기 이력 */
        ContentBrowserFileActions     _fileActions;     /**< 이름 바꾸기와 삭제 확인을 기다리는 상태, 디스크를 바꾸는 작업 */
        ContentBrowserReferenceSearch _referenceSearch; /**< 참조 역색인과 마지막 참조 찾기의 결과 */
        ContentBrowserImportQueue     _importQueue;     /**< 파일 대화상자가 고른 임포트 경로. 메인 스레드가 꺼내 임포트한다 */
        ContentBrowserTypeFilter      _typeFilter;      /**< 종류 필터 목록과 고른 필터. 종류 등록이 바뀌면 다시 만든다 */
        EditorFolderListingJob        _folderJob;
        ContentBrowserFolderCache     _folderCache;             /**< 폴더 트리의 하위 폴더. 그릴 때마다 디스크를 읽지 않도록 캐시한다 */
        uint64                        _seenContentChangeSerial; /**< 마지막으로 반영한 `AssetHotReload::getContentChangeSerial` */
        bool                          _bRootsDirty;
        bool                          _bFolderDirty;
        bool                          _bShowAllPacks;            /**< 게임 팩을 모두 보인다. 끄면 활성 팩 하나만 보인다 */
        bool                          _bShowAllPacksInitialized; /**< 환경설정의 기본값을 한 번 읽었다. 그 뒤에는 툴바가 정한다 */
        bool                          _bListingRecursive;        /**< 지금 목록에 하위 폴더의 파일까지 포함한다(검색 중) */
    };
} // namespace sw::editor
