/**
 * @file MeshModifiersPanel.h
 * @brief Mesh Modifiers 패널 — 대상 메시 고르기, 모디파이어 스택(추가 · 켜짐 · 위/아래 · 삭제), 매개변수 편집, 미리보기, 사이드카 저장, 베이크.
 * @details 매개변수 입력칸은 모디파이어의 선언(`MeshModifierParamDef`)만 보고 그린다 — 새 모디파이어는 이 패널을 고치지 않는다.
 *          선언의 선택 칸이 모양을 정한다 — 구역(`_pGroup`) · 툴팁 · 단위, NumberList 열 선언(행 × 열 표), 참조의 컴포넌트 걸러 보기, 경로의 확장자.
 *          곡선은 엔진 공용 곡선 편집기(`EditorCurveEditor`)를 `MeshModifierParamEdit` 의 선형 키 어댑터로 쓴다.
 *          격자(Lattice) 모디파이어만 제어점 목록 입력칸을 더 그린다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/GUI/IEditorPanel.h"
#include "Editor/Common/Widgets/EditorCurveEditor.h"

namespace sw::editor
{
    struct MeshModifierParamDef;
    struct MeshModifierRegistration;

    class DeformableModifier;
    class LatticeModifier;
    class MeshModifier;
    class MeshModifierSession;
    class MeshModifierStack;

    /**
     * @class MeshModifiersPanel
     * @brief 메시 모디파이어 확장의 도구 패널입니다.
     */
    class MeshModifiersPanel final : public IEditorPanel
    {
    public:
        /** @brief 패널 id 입니다. 등록(`SW_EDITOR_PANEL`)과 열기(`EditorPanelManager::setPanelOpen<…>`)가 모두 이 상수에서 온다. */
        static constexpr const utf8* kPanelID = "meshmodifiers.stack";

        MeshModifiersPanel();

        const utf8* getPanelTitle() const override { return "Mesh Modifiers"; }
        bool        isToolPanel() const override { return true; }
        /** @brief 스택 목록 · 기즈모 · 매개변수 · 하위 스택이 세로로 길다 - 기본(640 x 420)이면 매개변수가 첫 화면 밖으로 밀린다. */
        EditorTextExtent getInitialPanelSize() const override { return EditorTextExtent::ofText( 75.0f, 47.0f ); }
        void             drawContent() override;
        /** @brief 스택을 사이드카 문서에 씁니다(닫기 · 종료 확인의 Save). */
        [[nodiscard]] bool saveDocument() override;
        bool               canSaveDocument() const override;
        /** @brief 스택은 패널 밖의 세션(`MeshModifierSession`)에 살아 있어 창을 닫아도 잃지 않는다 — 닫기는 묻지 않고, 종료 확인만 미저장 스택을 묻는다. */
        bool asksBeforeClose() const override { return false; }

    private:
        /** @brief 세션 스택이 사이드카와 다르면 패널 dirty 를 올리고, 같아지면 내립니다(닫기 · 종료 확인 · 제목 표시가 이 값을 본다). 스택이 기준이다. */
        void syncDocumentDirty() override;
        /** @brief "Don't Save" — 스택을 마지막으로 읽거나 쓴 사이드카 상태로 돌립니다. */
        void revertDocument() override;
        void drawTargetRow( MeshModifierSession& session );
        void drawStackList( MeshModifierSession& session );
        /**
         * @brief 추가 단추와 종류 메뉴를 그리고, 고르면 @p stack 맨 아래에 더합니다. 더한 번호를, 아니면 -1 을 반환합니다.
         * @param pMarkPrefix 자동화 이름 접두(`<접두>.add` · `<접두>.category.<카테고리>` · `<접두>.item.<id>`)
         */
        int32 drawAddMenu( MeshModifierSession& session, MeshModifierStack& stack, const utf8* pMarkPrefix );
        /**
         * @brief 스택의 줄(켜짐 · 이름 · 위/아래 · 삭제)을 그리고 바뀐 고른 번호를 반환합니다. 목록은 다 그린 뒤 고친다.
         * @param pMarkPrefix    줄 이름 칸의 자동화 이름 접두(`<접두>.<번호>`)
         * @param pOutEnterIndex 주면 묶음(Deformable) 줄에 ">" 단추를 두고, 누른 줄 번호를 받습니다.
         */
        int32 drawStackRows( MeshModifierSession& session, MeshModifierStack& stack, int32 selectedIndex, const utf8* pMarkPrefix, int32* pOutEnterIndex );
        /**
         * @brief 묶음 모디파이어의 하위 스택 편집 — 경로(`_listNestedPath`)로 들어간 단계의 목록 · 추가 · 고른 하위 모디파이어의 기즈모와 매개변수.
         * @details 번호 경로 모델은 `DeformableModifier::findByPath` 다. 되돌리기는 바깥 스택 문서째(하위 스택은 `extra.stack`) 기록되므로 따로 두지 않는다.
         */
        void drawNestedStack( MeshModifierSession& session, DeformableModifier& root );
        /** @brief 보이는 하위 스택 @p shown 을 워프 정의 파일(`.mfwarp`)로 내보내는 경로 칸과 단추를 그립니다. */
        void drawWarpExport( const MeshModifierStack& shown );
        void drawSelectedModifier( MeshModifierSession& session );
        void drawGizmo( MeshModifierSession& session, MeshModifier& modifier );
        /** @brief 매개변수를 구역(`_pGroup`)별 접히는 머리로 묶어 그립니다. 구역 없는 매개변수는 맨 위입니다. */
        void drawParams( MeshModifierSession& session, MeshModifier& modifier );
        void drawParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex );
        /** @brief 곡선 매개변수를 그립니다(이름 + `drawCurveField`). */
        void drawCurveParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex );
        /**
         * @brief 곡선 미리보기를 그리고, 누르면 공용 곡선 편집기 팝업을 엽니다. 편집을 마칠 때 되돌리기 기록 하나로 값에 넣습니다.
         * @param itemIndex 곡선 목록의 항목 번호입니다. 단일 곡선이면 -1 입니다.
         */
        void drawCurveField( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex, int32 itemIndex, const FloatCurve& curve );
        /** @brief 곡선 목록을 항목마다 곡선 칸 + 위 · 아래 · 복제 · 삭제 단추로 그리고, 끝에 추가 단추를 둡니다. */
        void drawCurveListParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex );
        /** @brief NumberList 를 행 × 열 표로 그립니다(열 선언이 맞으면 이름 · 종류별 입력칸, 아니면 일반 숫자 열). */
        void drawNumberListParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex );
        /** @brief NumberList 의 칸 하나를 열 종류(실수 · 정수 · 콤보 · 벡터3)에 맞는 입력칸으로 그립니다. 값이 바뀌었으면 true 입니다. */
        [[nodiscard]] bool drawNumberCell( MeshModifierSession& session, const MeshModifierParamDef& def, bool bHasColumns, uint32 column, float32* pValue );
        /** @brief 씬 오브젝트 고르기 콤보와 "선택 사용" 단추를 그립니다. */
        void drawReferenceParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex );
        /** @brief 참조 목록을 항목마다 고르기 칸 + 위 · 아래 · 복제 · 삭제 단추로 그리고, 끝에 추가 단추를 둡니다. */
        void drawReferenceListParam( MeshModifierSession& session, MeshModifier& modifier, uint32 paramIndex );
        /** @brief 추가 메뉴 항목에 마우스가 있으면 모디파이어 설명을 툴팁으로 보입니다(설명은 항목마다 한 번 만들어 둡니다). */
        void drawRegistrationTooltip( const MeshModifierRegistration& registration );
        void drawLatticePoints( MeshModifierSession& session, LatticeModifier& lattice );
        void drawFooter( MeshModifierSession& session );
        /** @brief 입력칸이 값을 바꾼 직후 부릅니다. 끌기 한 번에 되돌리기 기록을 한 번만 남기고 미리보기를 다시 걸게 합니다. */
        void noteEdit( MeshModifierSession& session );
        /** @brief 입력칸을 그린 뒤 부릅니다. 끌기가 끝났으면 다음 편집이 새 기록을 남기게 합니다. */
        void finishItem();

        EditorCurveEditorState _curveState;          ///< 열린 곡선 편집기 팝업 하나의 상태
        const MeshModifier*    _pCurveModifier;      ///< 곡선 팝업을 연 모디파이어(그 프레임 안에서 비교만 한다 — 역참조하지 않는다)
        uint32                 _curveParamIndex;     ///< 곡선 팝업을 연 매개변수 번호
        int32                  _curveItemIndex;      ///< 곡선 팝업을 연 목록 항목 번호(단일 곡선이면 -1)
        string                 _descriptionID;       ///< 설명을 만들어 둔 등록 id
        string                 _description;         ///< `_descriptionID` 모디파이어의 설명
        const MeshModifier*    _pNestedRoot;         ///< 하위 스택 편집을 연 묶음(비교만 한다 — 역참조하지 않는다)
        vector<int32>          _listNestedPath;      ///< 묶음의 하위 스택에서 들어간 묶음들의 번호 경로(비면 고른 묶음 자신의 하위 스택)
        int32                  _nestedSelectedIndex; ///< 보이는 하위 스택에서 고른 번호(-1 이면 없음)
        string                 _warpExportPath;      ///< 보이는 하위 스택을 내보낼 워프 정의 파일 경로(`.mfwarp`, `WorldWarpModifier::writeWarpFile`)
        bool                   _bEditing;
    };
} // namespace sw::editor
