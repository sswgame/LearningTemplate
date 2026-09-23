/**
 * @file ContentBrowserPanel.h
 * @brief Engine / Common / Game / Editor 애셋 트리를 탐색하는 콘텐츠 브라우저 창입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorBackgroundIo.h"
#include "Editor/Common/Gui/IEditorPanel.h"

struct ImDrawList;

namespace sw::editor
{
    /** @brief 콘텐츠 루트를 탐색하고 애셋을 선택하거나 엽니다. */
    class ContentBrowserPanel : public IEditorPanel
    {
    public:
        /** @brief 콘텐츠 브라우저 창을 만듭니다. */
        ContentBrowserPanel() noexcept;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Content Browser"; }
        /** @brief 소스 트리, 브레드크럼, 애셋 타일/리스트를 그립니다. */
        void drawContent() override;

    private:
        // ------------------------------------------------------------------------------
        // 2) 필터 · 뷰 모드 · 항목
        // ------------------------------------------------------------------------------
        /** @brief 애셋 뷰 레이아웃 */
        enum class ViewMode : int32
        {
            Tiles = 0,
            List
        };

        /** @brief 이름이 있는 콘텐츠 루트 (표시 라벨 + 절대 경로) */
        struct ContentRoot
        {
            string _displayName;
            string _absolutePath;
        };

        using AssetEntry = EditorFolderListingEntry;

        // ------------------------------------------------------------------------------
        // 3) 스캔 · 필터
        // ------------------------------------------------------------------------------
        /** @brief Engine / Common / Game / Editor 루트 목록을 다시 구성합니다. */
        void refreshRoots();
        /** @brief 선택된 폴더의 항목을 다시 스캔합니다. */
        void refreshCurrentFolder();
        /** @brief 워커에서 받은 폴더 목록을 적용합니다. */
        void applyFolderListing( vector<EditorFolderListingEntry>& listEntry );
        /** @brief 항목이 타입 필터를 통과하는지 여부를 반환합니다. */
        bool passesTypeFilter( const AssetEntry& entry ) const;
        /** @brief 항목이 검색 필터를 통과하는지 여부를 반환합니다. */
        bool passesSearchFilter( const AssetEntry& entry ) const;

        // ------------------------------------------------------------------------------
        // 4) 패널 그리기 — 툴바 / 소스 트리 / 타일·리스트
        // ------------------------------------------------------------------------------
        /** @brief 검색 / 필터 / 뷰 모드 툴바를 그립니다. */
        void drawToolbar();
        /** @brief 왼쪽 루트와 폴더 트리를 그립니다. */
        void drawSourcesSection();
        /** @brief 재귀 폴더 트리 노드 하나를 그립니다. */
        void drawFolderTreeNode( string_view folderPath, string_view label, int32 depth );
        /** @brief 오른쪽 애셋 타일 또는 리스트를 그립니다. */
        void drawAssetView();
        /** @brief 현재 폴더의 브레드크럼 경로를 그립니다. */
        void drawBreadcrumbs();
        /** @brief 보이는 항목의 타일 그리드를 그립니다. */
        void drawTilesView( const vector<const AssetEntry*>& listVisible );
        /** @brief 보이는 항목의 리스트 행을 그립니다. */
        void drawListView( const vector<const AssetEntry*>& listVisible );
        /** @brief 애셋 항목 우클릭 컨텍스트 메뉴를 그립니다. */
        void drawAssetContextMenu( const AssetEntry& entry );
        /** @brief 애셋 항목 썸네일/아이콘을 그립니다. */
        void drawAssetThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos,
                                 const AssetEntry& entry );

        /** @brief 폴더 카드 아이콘을 그립니다. */
        void drawFolderThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos );

        /** @brief 폴더 이동 히스토리 항목 */
        struct HistoryEntry
        {
            string _folderPathAbs;
            string _breadcrumb;
        };

        /**
         * @brief 뒤로/앞으로 이동이 기억하는 최대 폴더 수입니다.
         * @details 상한이 없었습니다. 폴더를 옮길 때마다 문자열 둘이 붙고 **지워지지 않아서**, 에디터를 오래 켜 두고 폴더를
         *          계속 오갈수록 목록이 계속 늘었습니다. 브라우저의 뒤로 가기와 같은 규칙으로 가장 오래된 것부터 버립니다.
         */
        static constexpr size_t kMaxHistoryCount = 64;

        // ------------------------------------------------------------------------------
        // 5) 선택 · 히스토리 · 임포트
        //    파일 대화상자는 백그라운드, processPendingImports는 메인 스레드
        // ------------------------------------------------------------------------------
        /** @brief 이전 폴더로 돌아갈 수 있는지 반환합니다. */
        bool canNavigateBack() const { return _historyIndex > 0; }
        /** @brief 다음 폴더로 나아갈 수 있는지 반환합니다. */
        bool canNavigateForward() const { return _historyIndex >= 0 && _historyIndex + 1 < static_cast<int32>( _listHistory.size() ); }
        /** @brief 이전 히스토리 폴더로 이동합니다. */
        void navigateBack();
        /** @brief 다음 히스토리 폴더로 이동합니다. */
        void navigateForward();
        /** @brief 폴더를 선택하고 내용을 새로고침합니다. */
        void selectFolder( string_view absolutePath, string_view breadcrumb, bool bRecordHistory = true );
        /** @brief 애셋을 선택된 상태로 표시합니다. */
        void selectAsset( const AssetEntry& entry );
        /** @brief 폴더를 열거나 파일 애셋에 포커스/오픈합니다. */
        void openAsset( const AssetEntry& entry );
        /** @brief 대화상자로 현재 폴더에 파일을 임포트합니다. */
        void importFilesFromDialog();
        /** @brief 파일 대화상자가 고른 경로를 임포트 큐에 넣습니다. */
        void onImportDialogResult( const vector<string>& listPath );
        /** @brief 백그라운드 임포트 경로를 메인 스레드에서 처리합니다. */
        void processPendingImports();

    private:
        vector<ContentRoot> _listRoot;
        vector<AssetEntry>  _listEntry;
        /**
         * @brief 이번 프레임에 보일 엔트리입니다. **`_listEntry` 를 가리키기만** 합니다.
         * @details 그리는 함수 안의 지역 `vector` 였습니다. 프레임마다 할당하고 해제했고, 게다가 엔트리를 통째로 복사해서
         *          `string` 넷씩을 애셋 수만큼 베꼈습니다. 멤버로 두면 용량이 남아 첫 프레임 뒤로는 할당이 없습니다.
         * @warning `_listEntry` 가 바뀌면 이 포인터들은 무효가 됩니다. 그래서 프레임마다 다시 채웁니다.
         */
        vector<const AssetEntry*>             _listVisibleEntry;
        vector<HistoryEntry>                  _listHistory;
        string                                _selectedFolderAbs;
        string                                _breadcrumb; /**< 예: "Game / Shaders" */
        string                                _selectedAssetAbs;
        fixed_string<constant::kMaxBuffer128> _searchBuffer;
        float32                               _tileSize;
        uint32                                _filterIndex;
        int32                                 _historyIndex;
        ViewMode                              _viewMode;
        mutex                                 _pendingImportMutex;
        vector<string>                        _listPendingImportPath;
        EditorFolderListingJob                _folderJob;
        uint8                                 _bRootsDirty   : 1;
        uint8                                 _bFolderDirty  : 1;
        [[maybe_unused]] uint8                _reservedFlags : 6;
    };
} // namespace sw::editor
