/**
 * @file HierarchyPanel.h
 * @brief 씬 GameObject / Component 계층 창입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/IEditorPanel.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
    class Scene;
} // namespace sw

namespace sw::editor
{
    /** @brief 활성 씬의 오브젝트 아웃라이너 */
    class HierarchyPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "hierarchy";
        /** @brief 창 제목입니다(ImGui 창 이름 — 자체 테스트가 창을 찾을 때 이 상수를 쓴다). */
        static constexpr const utf8* kPanelTitle = "Hierarchy";

        HierarchyPanel();
        ~HierarchyPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8*    getPanelTitle() const override { return kPanelTitle; }
        EditorDockSlot getDefaultDockSlot() const override { return EditorDockSlot::Left; }
        /** @brief Hierarchy UI를 그립니다. */
        void drawContent() override;

        /** @brief 복제·이름 변경·삭제 단축키를 처리합니다. */
        void handleHierarchyShortcuts( GameObjectManager* pManager );

        /** @brief 검색 필드의 글을 바꿉니다. 다음 프레임의 트리가 이 필터로 그려집니다(에디터 자체 테스트가 사람 대신 칩니다). */
        void setFilterText( string_view filterText ) { _filterBuffer = filterText; }
        /** @brief 마지막으로 그린 프레임에 필터를 지난 루트 오브젝트 수입니다(서브트리 중 하나라도 맞으면 그 루트가 보인다). */
        uint32 getVisibleRootCount() const { return _visibleRootCount; }
        /** @brief 검색 필드의 지금 글입니다(에디터 자체 테스트가 타이핑이 닿았는지 본다). */
        string_view getFilterText() const { return _filterBuffer.view(); }
        /** @brief 마지막 프레임에 실제로 그린 루트 수입니다(화면 밖의 접힌 루트는 빈 공간만 두고 세지 않는다). */
        uint32 getDrawnRootCount() const { return _drawnRootCount; }
        /** @brief 마지막 프레임에 그린 마지막 루트의 오브젝트 id 입니다(없으면 0). */
        uint64 getLastDrawnRootID() const { return _lastDrawnRootID; }
        /** @brief 화면 밖의 접힌 루트를 빈 공간으로 둘지 정합니다(기본 켬). 에디터 자체 테스트가 껐다 켜며 위치가 같은지 본다. */
        void setOffscreenRowSkipEnabled( bool bEnabled ) { _bSkipOffscreenRows = bEnabled; }

    private:
        /** @brief 생성 버튼과 검색창이 있는 위쪽 막대를 그립니다. */
        void drawToolbar( GameObjectManager* pManager );
        /** @brief 루트 오브젝트 행을 그립니다. 화면 밖의 접힌 루트는 같은 높이의 빈 공간으로 합칩니다. */
        void drawRootRows( Scene* pScene, GameObjectManager* pManager );
        /** @brief 트리 아래 빈 영역의 드래그 앤 드롭 대상, 단축키, 우클릭 메뉴를 처리합니다. */
        void drawTreeEmptyArea( GameObjectManager* pManager );

        /**
         * @brief 프레임마다 다시 채우는 오브젝트 스냅샷입니다(용량 재사용).
         * @details 트리를 그리는 도중 오브젝트가 지워지거나 부모가 바뀔 수 있으므로 스냅샷이 필요합니다. 매니저를 잠근 채로
         *          그리면 그 변경이 같은 스레드에서 배타 락을 다시 잡아 교착합니다. 다만 값으로 반환하는 getAllGameObjects() 는
         *          호출마다 새로 할당하므로, 버퍼를 들고 출력 매개변수 오버로드를 씁니다.
         */
        vector<GameObject*>                   _listSceneObject;
        uint64                                _renamingObjectID;
        uint64                                _lastDrawnRootID; ///< 마지막 프레임에 그린 마지막 루트(`getLastDrawnRootID`)
        fixed_string<constant::kMaxBuffer128> _filterBuffer;
        fixed_string<constant::kMaxBuffer256> _renameBuffer;
        uint32                                _visibleRootCount; ///< 마지막 프레임에 보인 루트 수(`getVisibleRootCount`)
        uint32                                _drawnRootCount;   ///< 마지막 프레임에 실제로 그린 루트 수(`getDrawnRootCount`)
        bool                                  _bFocusRenameInput;
        bool                                  _bSkipOffscreenRows; ///< 화면 밖의 접힌 루트를 빈 공간으로 둔다(`setOffscreenRowSkipEnabled`)
    };
} // namespace sw::editor
