/**
 * @file RigDebugPanel.h
 * @brief 리그 디버그 창 — 고른 오브젝트의 `PoseModifierComponent` 노드 목록(종류 · 대상 본 · 가중치 · 켜짐)을 보이고 가중치와 켜짐을 고칩니다.
 * @details 언리얼 Control Rig 의 디버그 보기와 Animation Blueprint 디버그, 유니티 Animation Rigging 의 Rig 가중치 · 제약 켜짐 자리입니다.
 *          고친 값은 실행 중인 인스턴스의 노드 복제본에 들어가고, "Save to rig" 를 누르면 리그 에셋(`*.rig.json`)의 노드 `weight` · `enabled` 에
 *          되써 넣는다(`RigAsset::rewriteNodeSettings` — 다른 키는 그대로, 결과를 다시 읽어 규칙을 본다). 되돌리기는 문서 명령(`EditorTransaction::push`)으로 기록한다.
 *          고른 노드가 쓰는 본은 디버그 드로우 큐에 구와 이름으로 표시한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/GUI/IEditorPanel.h"

namespace sw::editor
{
    /** @brief 리그 디버그 도구 창입니다. */
    class RigDebugPanel : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 조회(`EditorPanelManager::findPanel<…>` · `setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "rig_debug";

        RigDebugPanel();
        virtual ~RigDebugPanel() override = default;

        const utf8* getPanelTitle() const override { return "Rig Debug"; }
        void        drawContent() override;
        bool        isToolPanel() const override { return true; }

    private:
        string                 _status;           ///< 저장 결과 줄
        uint64                 _selectedObjectID; ///< 고른 노드가 속한 오브젝트다(오브젝트가 바뀌면 노드 선택을 푼다)
        int32                  _selectedNode;     ///< 고른 노드 번호다(-1 이면 없음)
        float32                _weightBeforeEdit; ///< 가중치를 끌기 시작할 때의 값이다(되돌리기 기록용)
        uint8                  _bDrawMarkers : 1;
        [[maybe_unused]] uint8 _reserved     : 7;
    };
} // namespace sw::editor
