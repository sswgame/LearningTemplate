/**
 * @file ContentBrowserPanel.h
 * @brief 활성 게임 팩과 Engine / Common / Editor 애셋 트리를 탐색하고 참조를 찾는 콘텐츠 브라우저 창입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorThumbnailCache.h"
#include "Editor/Common/GUI/Panel/IEditorPanel.h"
#include "Editor/Panels/ContentBrowser/Model/ContentBrowserModel.h"

struct ImDrawList;

namespace sw::editor
{
    /**
     * @brief 콘텐츠 루트를 탐색하고 애셋을 선택하거나 여는 창입니다.
     * @details 창은 그리기(뷰)만 한다. 루트 · 목록 · 이력 · 선택 · 파일 관리 · 참조 역색인은 모델(`ContentBrowserModel`)이 가지고, 아래 공개 함수는 모델로 넘긴다.
     *          창이 가진 것은 화면 상태뿐이다(입력란 글, 타일 크기, 보기 모드, 팝업을 열 표시, 썸네일 캐시).
     */
    class ContentBrowserPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "content_browser";
        /** @brief 창 제목입니다(ImGui 창 이름 — 자체 테스트가 창을 찾을 때 이 상수를 쓴다). */
        static constexpr const utf8* kPanelTitle = "Content Browser";

        /** @brief 콘텐츠 브라우저 창을 만듭니다. */
        ContentBrowserPanel() noexcept;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel: 제목과 그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8*    getPanelTitle() const override { return kPanelTitle; }
        EditorDockSlot getDefaultDockSlot() const override { return EditorDockSlot::Bottom; }
        /** @brief 소스 트리, 브레드크럼, 애셋 타일/리스트를 그립니다. */
        void drawContent() override;
        /** @brief 썸네일 캐시가 가진 텍스처와 ImGui 등록을 해제합니다. */
        void shutdown( IRHIDevice* pDevice ) override;

        // ------------------------------------------------------------------------------
        // 1-1) 삭제와 테스트용 함수. 우클릭 Delete 는 requestDeleteAsset, 확인 모달, confirmDeleteAsset 순서로 진행한다. 자체 테스트도 같은 경로를 부른다
        // ------------------------------------------------------------------------------
        /** @brief 폴더로 이동합니다. 그 폴더가 속한 콘텐츠 루트에서 브레드크럼 경로도 함께 만듭니다. */
        void openFolder( string_view absolutePath ) { _model.openFolder( absolutePath ); }
        /** @brief 현재 폴더 목록의 항목 수를 반환합니다. 필터를 적용하기 전의 수입니다. */
        uint32 getEntryCount() const { return static_cast<uint32>( _model.getEntries().size() ); }
        /** @brief 폴더 목록을 다시 읽도록 예약했거나 읽는 중이면 true 입니다. */
        bool isFolderRefreshPending() const { return _model.isFolderRefreshPending(); }
        /** @brief 에셋 삭제 확인 모달을 엽니다. 실제 삭제는 사용자가 확인한 뒤 `confirmDeleteAsset` 이 합니다. */
        void requestDeleteAsset( string_view absolutePath );
        /**
         * @brief 확인을 기다리던 에셋을 OS 휴지통으로 보냅니다. 짝이 되는 `.meta` 파일도 함께 보냅니다. 성공하면 선택을 비우고 폴더 목록을 다시 읽게 합니다.
         * @return 휴지통으로 보냈으면 true 입니다. 기다리던 에셋이 없거나 보내지 못했으면 false 이며, 실패는 `EditorAssetFileCommands::moveToTrash` 가 알립니다.
         */
        bool confirmDeleteAsset() { return _model.confirmDeleteAsset(); }
        /** @brief 폴더 트리가 캐시한 하위 폴더를 비웁니다. 다음 그리기에서 디스크를 다시 읽으며, Refresh 와 같은 효과입니다. */
        void clearFolderTreeCache() { _model.clearFolderTreeCache(); }

        // ------------------------------------------------------------------------------
        // 1-2) 게임 팩 루트와 참조 찾기. 툴바 "All packs" 와 우클릭 Find References, Show Dependencies 가 부르는 함수와 같다
        // ------------------------------------------------------------------------------
        /** @brief 게임 팩을 모두 보일지 정합니다. 끄면 활성 게임 팩(`GameConfig::_packRoot`) 하나만 루트로 둡니다. */
        void setShowAllPacks( bool bShowAllPacks ) { _model.setShowAllPacks( bShowAllPacks ); }
        /** @brief 모든 게임 팩을 보이는 중이면 true 입니다. */
        bool isShowingAllPacks() const { return _model.isShowingAllPacks(); }
        /** @brief `game/` 아래 루트의 수를 반환합니다. 활성 팩만 보이면 1 입니다. */
        uint32 getGameRootCount() const { return _model.getGameRootCount(); }
        /** @brief 루트 목록에 그 표시 이름(`game/empty`, `engine`)을 가진 루트가 있으면 true 입니다. */
        bool hasRoot( string_view displayName ) const { return _model.hasRoot( displayName ); }
        /**
         * @brief 참조 역색인이 에디터가 본 마지막 `Content/` 변경까지 반영했으면 true 입니다(`ContentBrowserReferenceSearch::isCurrent`).
         * @details 자동화 감시 값(`Editor.ReferenceIndexReady` · `Editor.ReferenceCount`)과 이름 바꾸기 · 옮기기가 본다. 한 번 만든 색인이 있어도 스캔이 돌면 false 다.
         */
        bool isReferenceIndexCurrent() const { return _model.isReferenceIndexCurrent(); }
        /**
         * @brief 내용이 화면에 보이는데 참조 역색인이 아직 마지막 변경을 반영하지 않았으면 true 입니다. 자동화 시계 잡기 조건(`ContentBrowserReferenceIndex`)입니다.
         * @details 색인은 내용을 그리는 프레임에만 따라온다(`syncReferenceIndex`). 가려진 탭 · 접힌 창 · 닫힌 패널은 색인을 갱신하지 않으므로 기다리지 않는다 —
         *          그때 감시 값은 값을 내지 않고, 시나리오는 패널을 앞에 두고(`panel.focus content_browser`) 기다린다.
         */
        bool isWaitingForReferenceIndex() const;
        /** @brief 참조 역색인을 반환합니다. */
        const EditorReferenceIndex& getReferenceIndex() const { return _model.getReferenceSearch().getIndex(); }
        /**
         * @brief 에셋의 참조를 찾아 결과 창을 엽니다.
         * @param bDependencies false 면 그 에셋을 사용하는 쪽(Find References)을, true 면 그 에셋이 사용하는 쪽(Show Dependencies)을 찾습니다.
         */
        void showReferences( string_view absolutePath, bool bDependencies );
        /** @brief 마지막 참조 찾기의 결과 행 수를 반환합니다. */
        uint32 getReferenceResultCount() const { return static_cast<uint32>( _model.getReferenceSearch().getResultList().size() ); }
        // ------------------------------------------------------------------------------
        // 1-3) 에셋 관리. 툴바 Add, `asset.*` 명령(맥락 메뉴 · 단축키), 폴더로 드래그 앤 드롭이 부르는 함수다
        // ------------------------------------------------------------------------------
        /** @brief 지금 고른 에셋의 절대 경로입니다. 없으면 빈 문자열입니다. */
        const string& getSelectedAssetPath() const { return _model.getSelectedAssetPath(); }
        /** @brief 지금 목록에 있는 @p absolutePath 항목을 고릅니다. 목록에 없으면 아무 일도 없습니다. */
        void selectAssetPath( string_view absolutePath ) { _model.selectAssetPath( absolutePath ); }
        /** @brief 에셋을 같은 폴더에 복제하고 사본을 선택합니다. */
        bool duplicateAsset( string_view absolutePath ) { return _model.duplicateAsset( absolutePath ); }
        /** @brief 에셋 이름을 제자리에서 바꾸는 입력을 시작합니다. 입력란에는 확장자를 뺀 이름이 들어갑니다. 폴더는 받지 않습니다. */
        void beginRename( string_view absolutePath );
        /** @brief 이름을 바꾸는 중인 에셋의 경로를 반환합니다. 없으면 빈 문자열입니다. */
        const string& getRenamingPath() const { return _model.getRenamingPath(); }
        /** @brief 이름 바꾸기를 @p newName 으로 끝냅니다. 확장자는 생략해도 됩니다. 참조를 고치고 새 이름을 선택합니다. */
        bool commitRename( string_view newName ) { return _model.commitRename( newName ); }
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

        using AssetEntry = ContentBrowserModel::AssetEntry;

        // ------------------------------------------------------------------------------
        // 3) 패널 그리기: 툴바, 소스 트리, 타일과 리스트
        // ------------------------------------------------------------------------------
        /** @brief 검색, 필터, 뷰 모드 툴바를 그립니다. */
        void drawToolbar();
        /** @brief 툴바의 이동 단추(뒤로 · 앞으로), 추가 메뉴, 검색창입니다. */
        void drawNavigationControls();
        /** @brief 툴바의 형식 필터, 타일 · 목록 전환, 타일 크기입니다. */
        void drawViewControls();
        /** @brief 툴바의 가져오기, 새로 고침, 모든 팩 보기입니다. */
        void drawFileControls();
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
        /** @brief 탭이 가려지거나 창이 접혀 내용을 그리지 않는 프레임입니다. 색인을 기다리지 않게 표시합니다. */
        void onPanelCollapsed() override;
        /** @brief 패널을 닫았습니다. 색인을 기다리지 않게 표시합니다. */
        void onPanelClosed() override;
        /** @brief 참조 찾기 결과 창을 그립니다. `showReferences` 가 이 창을 엽니다. */
        void drawReferenceResults();
        /** @brief 툴바 Add 팝업을 그립니다. 새 폴더, 머티리얼, 씬, 프리팹을 만들 수 있습니다. */
        void drawAddMenu();
        /** @brief 이름을 바꾸는 중인 항목의 입력란을 그립니다. Enter 를 누르거나 다른 곳을 클릭하면 확정하고, Escape 를 누르면 취소합니다. */
        void drawRenameField( float32 width );
        /** @brief 바로 앞 위젯을 에셋 드롭 대상으로 만듭니다. 에셋을 놓으면 @p folderAbs 로 옮깁니다. */
        void acceptAssetDrop( string_view folderAbs );
        /** @brief 애셋 항목의 썸네일이나 아이콘을 그립니다. */
        void drawAssetThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos,
                                 const AssetEntry& entry );

        /** @brief 폴더 카드 아이콘을 그립니다. */
        void drawFolderThumbnail( ImDrawList* pDrawList, const float2& minPos, const float2& maxPos );
        /** @brief 잠긴 파일이나 읽기 전용 파일이면 타일 오른쪽 위에 자물쇠를 그리고, 툴팁에 이유를 보여 줍니다. */
        static void drawSourceControlBadge( ImDrawList* pDrawList, const float2& topRight, const AssetEntry& entry );

    private:
        ContentBrowserModel _model; /**< 루트 · 목록 · 이력 · 선택 · 파일 관리 · 참조 역색인 · 종류 필터 */
        /**
         * @brief 이번 프레임에 보일 엔트리입니다. 모델의 목록을 가리키기만 합니다.
         * @details 멤버로 두면 용량이 남아 첫 프레임 이후로는 할당이 없다. 포인터이므로 엔트리(`string` 네 개)를 복사하지도 않는다.
         * @warning 모델의 목록이 바뀌면 이 포인터들은 무효가 된다. 그래서 프레임마다 다시 채운다.
         */
        vector<const AssetEntry*>             _listVisibleEntry;
        EditorThumbnailCache                  _thumbnailCache;
        fixed_string<constant::kMaxBuffer128> _searchBuffer;
        fixed_string<constant::kMaxBuffer256> _renameBuffer;
        float32                               _tileSize;
        uint32                                _fallbackGlyphCount;     /**< 이번 그리기에서 종류 아이콘으로 그린 타일 수 */
        uint32                                _lastFallbackGlyphCount; /**< 지난 그리기의 값. 프로브가 읽는다 */
        ViewMode                              _viewMode;
        uint8                                 _bOpenDeleteConfirm    : 1; /**< 다음 그리기에서 삭제 확인 모달을 연다. 우클릭 메뉴 안에서는 창 단위 팝업을 열 수 없기 때문이다 */
        uint8                                 _bOpenReferenceResults : 1; /**< 다음 그리기에서 참조 결과 창을 연다 */
        uint8                                 _bFocusRenameInput     : 1; /**< 다음 그리기에서 이름 입력란에 포커스를 준다 */
        uint8                                 _bContentVisible       : 1; /**< 마지막 그리기에서 내용을 그렸다(가려진 탭 · 접힌 창 · 닫힌 패널이 아니다) */
        [[maybe_unused]] uint8                _reservedFlags         : 4;
    };
} // namespace sw::editor
