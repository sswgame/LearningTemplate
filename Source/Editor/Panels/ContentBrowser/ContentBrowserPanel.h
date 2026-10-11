/**
 * @file ContentBrowserPanel.h
 * @brief 활성 게임 팩과 Engine / Common / Editor 애셋 트리를 탐색하고 참조를 찾는 콘텐츠 브라우저 창입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorAssetFileCommands.h"
#include "Editor/Common/Commands/EditorBackgroundIO.h"
#include "Editor/Common/Commands/EditorReferenceIndex.h"
#include "Editor/Common/GUI/EditorThumbnailCache.h"
#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Panels/ContentBrowser/ContentBrowserLogic.h"

struct ImDrawList;

namespace sw::editor
{
    /** @brief 콘텐츠 루트를 탐색하고 애셋을 선택하거나 여는 창입니다. */
    class ContentBrowserPanel : public IEditorPanel
    {
    public:
        /** @brief 콘텐츠 브라우저 창을 만듭니다. */
        ContentBrowserPanel() noexcept;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel: 제목과 그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Content Browser"; }
        /** @brief 소스 트리, 브레드크럼, 애셋 타일/리스트를 그립니다. */
        void drawContent() override;
        /** @brief 썸네일 캐시가 가진 텍스처와 ImGui 등록을 해제합니다. */
        void shutdown( IRHIDevice* pDevice ) override;

        // ------------------------------------------------------------------------------
        // 1-1) 삭제와 테스트용 함수. 우클릭 Delete 는 requestDeleteAsset, 확인 모달, confirmDeleteAsset 순서로 진행한다. 자체 테스트도 같은 경로를 부른다
        // ------------------------------------------------------------------------------
        /** @brief 폴더로 이동합니다. 그 폴더가 속한 콘텐츠 루트에서 브레드크럼 경로도 함께 만듭니다. */
        void openFolder( string_view absolutePath );
        /** @brief 현재 폴더 목록의 항목 수를 반환합니다. 필터를 적용하기 전의 수입니다. */
        uint32 getEntryCount() const { return static_cast<uint32>( _listEntry.size() ); }
        /** @brief 폴더 목록을 다시 읽도록 예약했거나 읽는 중이면 true 입니다. */
        bool isFolderRefreshPending() const { return _bFolderDirty == SW_TRUE || _folderJob.isPending(); }
        /** @brief 에셋 삭제 확인 모달을 엽니다. 실제 삭제는 사용자가 확인한 뒤 `confirmDeleteAsset` 이 합니다. */
        void requestDeleteAsset( string_view absolutePath );
        /**
         * @brief 확인을 기다리던 에셋을 OS 휴지통으로 보냅니다. 짝이 되는 `.meta` 파일도 함께 보냅니다. 성공하면 선택을 비우고 폴더 목록을 다시 읽게 합니다.
         * @return 휴지통으로 보냈으면 true 입니다. 기다리던 에셋이 없거나 보내지 못했으면 false 이며, 실패는 `EditorAssetFileCommands::moveToTrash` 가 알립니다.
         */
        bool confirmDeleteAsset();
        /** @brief 폴더 트리가 캐시한 하위 폴더를 비웁니다. 다음 그리기에서 디스크를 다시 읽으며, Refresh 와 같은 효과입니다. */
        void clearFolderTreeCache() { _folderCache.clear(); }

        // ------------------------------------------------------------------------------
        // 1-2) 게임 팩 루트와 참조 찾기. 툴바 "All packs" 와 우클릭 Find References, Show Dependencies 가 부르는 함수와 같다
        // ------------------------------------------------------------------------------
        /** @brief 게임 팩을 모두 보일지 정합니다. 끄면 활성 게임 팩(`GameConfig::_packRoot`) 하나만 루트로 둡니다. */
        void setShowAllPacks( bool bShowAllPacks );
        /** @brief 모든 게임 팩을 보이는 중이면 true 입니다. */
        bool isShowingAllPacks() const { return _bShowAllPacks == SW_TRUE; }
        /** @brief `game/` 아래 루트의 수를 반환합니다. 활성 팩만 보이면 1 입니다. */
        uint32 getGameRootCount() const;
        /** @brief 루트 목록에 그 표시 이름(`game/empty`, `engine`)을 가진 루트가 있으면 true 입니다. */
        bool hasRoot( string_view displayName ) const;
        /** @brief 참조 역색인이 한 번이라도 완성되었으면 true 입니다. */
        bool isReferenceIndexReady() const { return _referenceIndex.isReady(); }
        /** @brief 참조 역색인을 반환합니다. */
        const EditorReferenceIndex& getReferenceIndex() const { return _referenceIndex; }
        /**
         * @brief 에셋의 참조를 찾아 결과 창을 엽니다.
         * @param bDependencies false 면 그 에셋을 사용하는 쪽(Find References)을, true 면 그 에셋이 사용하는 쪽(Show Dependencies)을 찾습니다.
         */
        void showReferences( string_view absolutePath, bool bDependencies );
        /** @brief 마지막 참조 찾기의 결과 행 수를 반환합니다. */
        uint32 getReferenceResultCount() const { return static_cast<uint32>( _listReferenceResult.size() ); }
        // ------------------------------------------------------------------------------
        // 1-3) 에셋 관리. 툴바 Add, 우클릭 Rename, Duplicate, Delete, 단축키 F2, Ctrl+D, Delete, 폴더로 드래그 앤 드롭이 부르는 함수와 같다
        // ------------------------------------------------------------------------------
        /** @brief 현재 폴더에 새 폴더를 만들고 목록을 다시 읽게 합니다. */
        [[nodiscard]] bool createNewFolder();
        /** @brief 현재 폴더에 새 에셋을 만들고 선택합니다. */
        [[nodiscard]] bool createNewAsset( EditorNewAssetKind kind );
        /** @brief 에셋을 같은 폴더에 복제하고 사본을 선택합니다. */
        bool duplicateAsset( string_view absolutePath );
        /** @brief 에셋 이름을 그 자리에서 바꾸는 입력을 시작합니다. 입력란에는 확장자를 뺀 이름이 들어갑니다. 폴더는 받지 않습니다. */
        void beginRename( string_view absolutePath );
        /** @brief 이름을 바꾸는 중인 에셋의 경로를 반환합니다. 없으면 빈 문자열입니다. */
        const string& getRenamingPath() const { return _renamingAbs; }
        /** @brief 이름 바꾸기를 @p newName 으로 끝냅니다. 확장자는 생략해도 됩니다. 참조를 고치고 새 이름을 선택합니다. */
        bool commitRename( string_view newName );
        /** @brief 에셋을 @p folderAbs 로 옮기고 참조를 고칩니다. 폴더 타일이나 트리로 드래그 앤 드롭할 때 씁니다. */
        [[nodiscard]] bool moveAssetToFolder( string_view sourceAbs, string_view folderAbs );
        /** @brief 썸네일 캐시가 보관한 텍스처 썸네일 수를 반환합니다. */
        uint32 getThumbnailCacheCount() const { return _thumbnailCache.getCachedCount(); }
        /** @brief 지난 그리기에서 종류 아이콘으로 그린 타일 수를 반환합니다. 이미지 썸네일이 없는 종류가 해당합니다. */
        uint32 getFallbackGlyphCount() const { return _lastFallbackGlyphCount; }

    private:
        // ------------------------------------------------------------------------------
        // 2) 필터, 뷰 모드, 항목
        // ------------------------------------------------------------------------------
        /** @brief 애셋 뷰의 레이아웃입니다. */
        enum class ViewMode : int32
        {
            Tiles = 0,
            List
        };

        /** @brief 이름이 있는 콘텐츠 루트입니다. 표시 이름과 절대 경로를 가집니다. */
        struct ContentRoot
        {
            string _displayName;
            string _absolutePath;
        };

        using AssetEntry = EditorFolderListingEntry;

        // ------------------------------------------------------------------------------
        // 3) 스캔과 필터
        // ------------------------------------------------------------------------------
        /** @brief Engine, Common, Game, Editor 루트 목록을 다시 구성합니다. */
        void refreshRoots();
        /** @brief 선택한 폴더의 항목을 다시 스캔합니다. */
        void refreshCurrentFolder();
        /** @brief 워커가 읽어 온 폴더 목록을 적용합니다. */
        void applyFolderListing( vector<EditorFolderListingEntry>& listEntry );
        /** @brief 항목이 타입 필터를 통과하는지 여부를 반환합니다. */
        bool passesTypeFilter( const AssetEntry& entry ) const;
        /** @brief 항목이 검색 필터를 통과하는지 여부를 반환합니다. */
        bool passesSearchFilter( const AssetEntry& entry ) const;

        // ------------------------------------------------------------------------------
        // 4) 패널 그리기: 툴바, 소스 트리, 타일과 리스트
        // ------------------------------------------------------------------------------
        /** @brief 검색, 필터, 뷰 모드 툴바를 그립니다. */
        void drawToolbar();
        /** @brief 왼쪽 루트와 폴더 트리를 그립니다. */
        void drawSourcesSection();
        /** @brief 폴더 트리 노드 하나를 재귀로 그립니다. */
        void drawFolderTreeNode( string_view folderPath, string_view label, int32 depth );
        /** @brief 오른쪽 애셋 타일 또는 리스트를 그립니다. */
        void drawAssetView();
        /** @brief 현재 폴더의 브레드크럼 경로를 그립니다. */
        void drawBreadcrumbs();
        /** @brief 보이는 항목의 타일 그리드를 그립니다. */
        void drawTilesView( const vector<const AssetEntry*>& listVisible );
        /** @brief 보이는 항목의 리스트 행을 그립니다. */
        void drawListView( const vector<const AssetEntry*>& listVisible );
        /** @brief 애셋 항목을 우클릭했을 때의 컨텍스트 메뉴를 그립니다. */
        void drawAssetContextMenu( const AssetEntry& entry );
        /** @brief 삭제 확인 모달을 그립니다. `requestDeleteAsset` 이 이 모달을 엽니다. */
        void drawDeleteConfirmModal();
        /** @brief `Resource/` 가 바뀌었으면 폴더 목록을 다시 읽게 합니다. 에디터 밖에서 바꾼 경우도 포함합니다. */
        void syncWithContentChanges();
        /** @brief 워커가 만든 역색인을 받습니다. 처음이거나 `Resource/` 가 바뀌었으면 다시 스캔하도록 요청합니다. */
        void syncReferenceIndex();
        /** @brief 참조 찾기 결과 창을 그립니다. `showReferences` 가 이 창을 엽니다. */
        void drawReferenceResults();
        /** @brief 툴바 Add 팝업을 그립니다. 새 폴더, 머티리얼, 씬, 프리팹을 만들 수 있습니다. */
        void drawAddMenu();
        /** @brief 이름을 바꾸는 중인 항목의 입력란을 그립니다. Enter 를 누르거나 다른 곳을 클릭하면 확정하고, Escape 를 누르면 취소합니다. */
        void drawRenameField( float32 width );
        /** @brief 창에 포커스가 있을 때 F2, Ctrl+D, Delete 단축키를 처리합니다. */
        void handleAssetShortcuts();
        /** @brief 바로 앞 위젯을 에셋 드롭 대상으로 만듭니다. 에셋을 놓으면 @p folderAbs 로 옮깁니다. */
        void acceptAssetDrop( string_view folderAbs );
        /** @brief 목록과 폴더 트리를 다시 읽게 하고 @p absolutePath 를 선택합니다. */
        void selectAfterFileChange( string_view absolutePath );
        /** @brief 애셋 항목의 썸네일이나 아이콘을 그립니다. */
        void drawAssetThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos,
                                 const AssetEntry& entry );

        /** @brief 폴더 카드 아이콘을 그립니다. */
        void drawFolderThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos );
        /** @brief 잠긴 파일이나 읽기 전용 파일이면 타일 오른쪽 위에 자물쇠를 그리고, 툴팁에 이유를 보여 줍니다. */
        static void drawSourceControlBadge( ImDrawList* pDrawList, const float2& topRight, const AssetEntry& entry );
        /** @brief 항목의 버전 관리 상태 문자열을 만듭니다. 잠금이나 읽기 전용이 아니면 빈 문자열입니다. */
        static string describeSourceControlStatus( const AssetEntry& entry );

        /** @brief 폴더 이동 히스토리의 항목 하나입니다. */
        struct HistoryEntry
        {
            string                      _folderPathAbs;
            vector<ContentBrowserCrumb> _listCrumb;
        };

        /**
         * @brief 뒤로 가기와 앞으로 가기가 기억하는 최대 폴더 수입니다.
         * @details 상한이 없으면 폴더를 옮길 때마다 문자열 둘이 쌓여 에디터를 오래 켜 둘수록 목록이 계속 늘어난다.
         *          브라우저의 뒤로 가기와 같은 규칙으로 가장 오래된 항목부터 버린다.
         */
        static constexpr size_t kMaxHistoryCount = 64;

        // ------------------------------------------------------------------------------
        // 5) 선택, 히스토리, 임포트
        //    파일 대화상자는 백그라운드에서 열고, processPendingImports 는 메인 스레드에서 실행한다
        // ------------------------------------------------------------------------------
        /** @brief 이전 폴더로 돌아갈 수 있으면 true 입니다. */
        bool canNavigateBack() const { return _historyIndex > 0; }
        /** @brief 다음 폴더로 나아갈 수 있으면 true 입니다. */
        bool canNavigateForward() const { return _historyIndex >= 0 && _historyIndex + 1 < static_cast<int32>( _listHistory.size() ); }
        /** @brief 이전 히스토리 폴더로 이동합니다. */
        void navigateBack();
        /** @brief 다음 히스토리 폴더로 이동합니다. */
        void navigateForward();
        /** @brief 폴더를 선택하고 내용을 새로고침합니다. @p listCrumb 는 그 폴더의 브레드크럼 경로이며 조각마다 절대 경로를 가집니다. */
        void selectFolder( string_view absolutePath, const vector<ContentBrowserCrumb>& listCrumb, bool bRecordHistory = true );
        /** @brief 폴더가 속한 콘텐츠 루트에서 그 폴더까지의 브레드크럼 경로를 만듭니다. */
        void makeTrailForFolder( string_view folderAbs, vector<ContentBrowserCrumb>& outListCrumb ) const;
        /** @brief 애셋을 선택 상태로 표시합니다. */
        void selectAsset( const AssetEntry& entry );
        /** @brief 폴더면 열고, 파일 애셋이면 포커스를 주거나 엽니다. */
        void openAsset( const AssetEntry& entry );
        /** @brief 대화상자로 현재 폴더에 파일을 임포트합니다. */
        void importFilesFromDialog();
        /** @brief 파일 대화상자에서 고른 경로를 임포트 큐에 넣습니다. */
        void onImportDialogResult( const vector<string>& listPath );
        /** @brief 백그라운드에서 받은 임포트 경로를 메인 스레드에서 처리합니다. */
        void processPendingImports();

    private:
        vector<ContentRoot> _listRoot;
        vector<AssetEntry>  _listEntry;
        /**
         * @brief 이번 프레임에 보일 엔트리입니다. `_listEntry` 를 가리키기만 합니다.
         * @details 멤버로 두면 용량이 남아 첫 프레임 이후로는 할당이 없다. 포인터이므로 엔트리(`string` 네 개)를 복사하지도 않는다.
         * @warning `_listEntry` 가 바뀌면 이 포인터들은 무효가 된다. 그래서 프레임마다 다시 채운다.
         */
        vector<const AssetEntry*>             _listVisibleEntry;
        vector<HistoryEntry>                  _listHistory;
        string                                _selectedFolderAbs;
        vector<ContentBrowserCrumb>           _listCrumb; /**< 현재 폴더의 브레드크럼 경로. 예: "Favorites / Shaders / bin" */
        string                                _selectedAssetAbs;
        string                                _pendingDeleteAbs; /**< 삭제 확인을 기다리는 에셋. 비어 있으면 없음 */
        string                                _referenceQueryID; /**< 마지막 참조 찾기의 대상 리소스 id */
        string                                _renamingAbs;      /**< 이름을 바꾸는 중인 에셋. 비어 있으면 없음 */
        vector<EditorAssetReference>          _listReferenceResult;
        EditorReferenceIndex                  _referenceIndex;
        EditorReferenceIndexJob               _referenceIndexJob;
        EditorThumbnailCache                  _thumbnailCache;
        uint64                                _referenceIndexSerial; /**< 역색인 스캔을 요청할 때의 `AssetHotReload::getContentChangeSerial` 값 */
        fixed_string<constant::kMaxBuffer128> _searchBuffer;
        fixed_string<constant::kMaxBuffer256> _renameBuffer;
        uint64                                _seenContentChangeSerial; /**< 마지막으로 반영한 `AssetHotReload::getContentChangeSerial` */
        float32                               _tileSize;
        uint32                                _filterIndex;
        uint32                                _fallbackGlyphCount;     /**< 이번 그리기에서 종류 아이콘으로 그린 타일 수 */
        uint32                                _lastFallbackGlyphCount; /**< 지난 그리기의 값. 프로브가 읽는다 */
        int32                                 _historyIndex;
        ViewMode                              _viewMode;
        mutex                                 _pendingImportMutex;
        vector<string>                        _listPendingImportPath;
        EditorFolderListingJob                _folderJob;
        ContentBrowserFolderCache             _folderCache; /**< 폴더 트리의 하위 폴더. 그릴 때마다 디스크를 읽지 않도록 캐시한다 */
        uint8                                 _bRootsDirty                 : 1;
        uint8                                 _bFolderDirty                : 1;
        uint8                                 _bOpenDeleteConfirm          : 1; /**< 다음 그리기에서 삭제 확인 모달을 연다. 우클릭 메뉴 안에서는 창 단위 팝업을 열 수 없기 때문이다 */
        uint8                                 _bShowAllPacks               : 1; /**< 게임 팩을 모두 보인다. 끄면 활성 팩 하나만 보인다 */
        uint8                                 _bShowAllPacksInitialized    : 1; /**< 환경설정의 기본값을 한 번 읽었다. 그 뒤에는 툴바가 정한다 */
        uint8                                 _bReferenceIndexRequested    : 1; /**< 역색인 스캔을 한 번이라도 요청했다 */
        uint8                                 _bOpenReferenceResults       : 1; /**< 다음 그리기에서 참조 결과 창을 연다 */
        uint8                                 _bReferenceQueryDependencies : 1; /**< 마지막 질의가 Show Dependencies 였다 */
        uint8                                 _bFocusRenameInput           : 1; /**< 다음 그리기에서 이름 입력란에 포커스를 준다 */
        uint8                                 _bListingRecursive           : 1; /**< 현재 목록에 하위 폴더의 파일까지 포함한다(검색 중) */
        [[maybe_unused]] uint8                _reservedFlags               : 6;
    };
} // namespace sw::editor
