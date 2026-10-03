/**
 * @file HierarchyPanel.h
 * @brief 씬 GameObject / Component 계층 창입니다.
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
    class GameObject;
    class GameObjectManager;
} // namespace sw

namespace sw::editor
{
    /** @brief 활성 씬의 오브젝트 아웃라이너 */
    class HierarchyPanel : public IEditorPanel
    {
    public:
        HierarchyPanel();
        ~HierarchyPanel() override = default;

        // ------------------------------------------------------------------------------
        // 1) IEditorPanel — 제목/그리기
        // ------------------------------------------------------------------------------
        /** @brief 창 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Hierarchy"; }
        /** @brief Hierarchy UI를 그립니다. */
        void drawContent() override;

        /** @brief 복제·이름 변경·삭제 단축키를 처리합니다. */
        void handleHierarchyShortcuts( GameObjectManager* pManager );

        /** @brief 검색 칸의 글을 바꿉니다. 다음 프레임의 트리가 이 필터로 그려집니다(에디터 자체 시험이 사람 대신 칩니다). */
        void setFilterText( string_view filterText ) { _filterBuffer = filterText; }
        /** @brief 마지막으로 그린 프레임에 필터를 지난 루트 오브젝트 수입니다(서브트리 중 하나라도 맞으면 그 루트가 보인다). */
        uint32 getVisibleRootCount() const { return _visibleRootCount; }

    private:
        /**
         * @brief 프레임마다 다시 채우는 오브젝트 스냅샷입니다(용량 재사용).
         * @details 트리를 그리는 도중 오브젝트가 지워지거나 부모가 바뀔 수 있으므로 스냅샷이 필요합니다. 매니저를 잠근 채로
         *          그리면 그 변경이 같은 스레드에서 배타 락을 다시 잡아 교착합니다. 다만 값으로 반환하는 getAllGameObjects() 는
         *          호출마다 새로 할당하므로, 버퍼를 들고 출력 매개변수 오버로드를 씁니다.
         */
        vector<GameObject*>                   _listSceneObject;
        uint64                                _renamingObjectId;
        fixed_string<constant::kMaxBuffer128> _filterBuffer;
        fixed_string<constant::kMaxBuffer256> _renameBuffer;
        uint32                                _visibleRootCount; ///< 마지막 프레임에 보인 루트 수(`getVisibleRootCount`)
        bool                                  _bFocusRenameInput;
    };
} // namespace sw::editor
