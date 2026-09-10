/**
 * @file HierarchyPanel.h
 * @brief 씬 GameObject / Component 계층 윈도우
 */
#pragma once
#include "Core/Common/Defines.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/IEditorPanel.h"

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
        /** @brief 윈도우 제목을 반환합니다. */
        const utf8* getPanelTitle() const override { return "Hierarchy"; }
        /** @brief Hierarchy UI를 그립니다. */
        void drawContent() override;

        /** @brief 복제·이름 변경·삭제 단축키를 처리합니다. */
        void handleHierarchyShortcuts( GameObjectManager* pManager );

    private:
        /**
         * @brief 프레임마다 다시 채우는 오브젝트 스냅샷 (용량 재사용).
         * @details 트리를 그리는 도중 오브젝트가 지워지거나 재부모화될 수 있으므로 스냅샷이
         *          필요하다 — 매니저를 잠근 채로 그리면 그 변경이 같은 스레드에서 배타 락을
         *          다시 잡아 교착한다. 다만 값 반환 getAllGameObjects() 는 호출마다 새로
         *          할당하므로, 버퍼를 들고 out 파라미터 오버로드를 쓴다.
         */
        vector<GameObject*>                   _listSceneObject;
        uint64                                _renamingObjectId;
        fixed_string<constant::kMaxBuffer128> _filterBuffer;
        fixed_string<constant::kMaxBuffer256> _renameBuffer;
        bool                                  _bFocusRenameInput;
    };
} // namespace sw::editor
