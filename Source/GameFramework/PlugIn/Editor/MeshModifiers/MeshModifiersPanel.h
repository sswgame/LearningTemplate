/**
 * @file MeshModifiersPanel.h
 * @brief Mesh Modifiers 패널 — 대상 메시 고르기, 모디파이어 스택(추가 · 켜짐 · 위/아래 · 삭제), 매개변수 편집, 미리보기, 사이드카 저장, 굽기.
 * @details 매개변수 입력칸은 모디파이어의 선언(`MeshModifierParamDef`)만 보고 그린다 — 새 모디파이어는 이 패널을 고치지 않는다.
 *          격자(Lattice) 모디파이어만 제어점 목록 입력칸을 더 그린다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Editor/Common/GUI/IEditorPanel.h"

namespace sw::editor
{
    class LatticeModifier;
    class MeshModifier;
    class MeshModifierSession;

    /**
     * @class MeshModifiersPanel
     * @brief 메시 모디파이어 확장의 도구 패널입니다.
     */
    class MeshModifiersPanel final : public IEditorPanel
    {
    public:
        MeshModifiersPanel();

        const utf8* getPanelTitle() const override { return "Mesh Modifiers"; }
        bool        isToolPanel() const override { return true; }
        void        drawContent() override;

    private:
        void drawTargetRow( MeshModifierSession& session );
        void drawStackList( MeshModifierSession& session );
        void drawAddMenu( MeshModifierSession& session );
        void drawSelectedModifier( MeshModifierSession& session );
        void drawGizmo( MeshModifierSession& session, MeshModifier& modifier );
        void drawParams( MeshModifierSession& session, MeshModifier& modifier );
        void drawLatticePoints( MeshModifierSession& session, LatticeModifier& lattice );
        void drawFooter( MeshModifierSession& session );
        /** @brief 입력칸이 값을 바꾼 직후 부릅니다. 끌기 한 번에 되돌리기 기록을 한 번만 남기고 미리보기를 다시 걸게 합니다. */
        void noteEdit( MeshModifierSession& session );
        /** @brief 입력칸을 그린 뒤 부릅니다. 끌기가 끝났으면 다음 편집이 새 기록을 남기게 합니다. */
        void finishItem();

        int32 _selectedControlPoint;
        bool  _bEditing;
        bool  _bMirrorEdit;
        uint8 _mirrorAxis;
    };
} // namespace sw::editor
