/**
 * @file MeshModifiersPanel.h
 * @brief Mesh Modifiers 패널 — 대상 메시 고르기, 모디파이어 스택(추가 · 켜짐 · 위/아래 · 삭제), 매개변수 편집, 미리보기, 사이드카 저장, 굽기.
 * @details 매개변수 입력칸은 모디파이어의 선언(`MeshModifierParamDef`)만 보고 그린다 — 새 모디파이어는 이 패널을 고치지 않는다.
 *          선언의 선택 칸이 모양을 정한다 — 구역(`_pGroup`) · 툴팁 · 단위, NumberList 열 선언(행 × 열 표), 참조의 컴포넌트 걸러 보기, 경로의 확장자.
 *          곡선은 엔진 공용 곡선 편집기(`EditorCurveEditor`)를 `MeshModifierParamEdit` 의 선형 키 어댑터로 쓴다.
 *          격자(Lattice) 모디파이어만 제어점 목록 입력칸을 더 그린다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Widgets/EditorCurveEditor.h"

namespace sw::editor
{
    class LatticeModifier;
    class MeshModifier;
    class MeshModifierSession;
    struct MeshModifierParamDef;
    struct MeshModifierRegistration;

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
        /** @brief 매개변수를 구역(`_pGroup`)별 접히는 머리로 묶어 그립니다. 구역 없는 매개변수는 맨 위입니다. */
        void drawParams( MeshModifierSession& session, MeshModifier& modifier );
        void drawParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex );
        /** @brief 곡선 미리보기를 그리고, 누르면 공용 곡선 편집기 팝업을 엽니다. 편집을 마칠 때 되돌리기 기록 하나로 값에 넣습니다. */
        void drawCurveParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex );
        /** @brief NumberList 를 행 × 열 표로 그립니다(열 선언이 맞으면 이름 · 종류별 입력칸, 아니면 일반 숫자 열). */
        void drawNumberListParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex );
        /** @brief 씬 오브젝트 고르기 콤보와 "선택 사용" 단추를 그립니다. */
        void drawReferenceParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex );
        /** @brief 추가 메뉴 항목에 마우스가 있으면 모디파이어 설명을 툴팁으로 보입니다(설명은 항목마다 한 번 만들어 둡니다). */
        void drawRegistrationTooltip( const MeshModifierRegistration& registration );
        void drawLatticePoints( MeshModifierSession& session, LatticeModifier& lattice );
        void drawFooter( MeshModifierSession& session );
        /** @brief 입력칸이 값을 바꾼 직후 부릅니다. 끌기 한 번에 되돌리기 기록을 한 번만 남기고 미리보기를 다시 걸게 합니다. */
        void noteEdit( MeshModifierSession& session );
        /** @brief 입력칸을 그린 뒤 부릅니다. 끌기가 끝났으면 다음 편집이 새 기록을 남기게 합니다. */
        void finishItem();

        EditorCurveEditorState _curveState;        ///< 열린 곡선 편집기 팝업 하나의 상태
        const MeshModifier*    _pCurveModifier;    ///< 곡선 팝업을 연 모디파이어(그 프레임 안에서 비교만 한다 — 역참조하지 않는다)
        uint32                 _curveParamIndex;   ///< 곡선 팝업을 연 매개변수 번호
        string                 _descriptionID;     ///< 설명을 만들어 둔 등록 id
        string                 _description;       ///< `_descriptionID` 모디파이어의 설명
        bool                   _bEditing;
    };
} // namespace sw::editor
