/**
 * @file CatalogTableEditor.h
 * @brief `CatalogDocument` 를 원소 이름별 테이블로 그리고 셀 · 행을 고치는 ImGui 위젯입니다. 카탈로그 편집기 창과 Data Table 의 게임 데이터 탭이 같이 씁니다.
 * @details 셀은 글을 칠 때 문서를 바꾸지 않고, 입력 필드를 떠날 때 한 번 반영합니다(되돌리기 한 단계 = 셀 편집 한 번). 반영 · 행 더하기 · 행 빼기는 테이블을 다 그린 뒤에 합니다 —
 *          그리는 도중 문서의 원소 인덱스가 바뀌면 그리던 테이블이 깨지기 때문입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Widgets/EditorCurveEditorState.h"
#include "Editor/Panels/Catalog/Model/CatalogChildSection.h"
#include "Editor/Panels/Catalog/Model/CatalogCurve.h"
#include "Editor/Panels/Catalog/Model/CatalogDocument.h"
#include "Editor/Panels/Catalog/Model/CatalogTableEdit.h"

namespace sw::editor
{
    /**
     * @class CatalogTableEditor
     * @brief 테이블 그리기와 보류 중인 편집을 가진 위젯입니다. 문서는 가지지 않습니다.
     */
    class CatalogTableEditor
    {
    public:
        CatalogTableEditor();

        /** @brief 문서의 테이블을 그리고 보류된 편집을 반영합니다. 문서가 바뀌었으면 true 입니다. */
        [[nodiscard]] bool draw( CatalogDocument& inoutDocument );
        /** @brief 입력 중인 칸과 보류 편집을 버립니다. 다른 파일을 열 때 부릅니다. */
        void reset();
        /** @brief 마지막으로 반영한 편집의 한 줄 설명입니다. 실패했으면 그 이유입니다(상태 줄이 씁니다). */
        const string& getLastActionText() const { return _lastActionText; }
        /** @brief 지난 `draw` 의 편집이 거절되었으면 true 이고 표시를 지웁니다. 거절은 `draw` 가 false 를 돌려주므로 따로 묻지 않으면 이유가 사라진다. */
        [[nodiscard]] bool takeEditFailed();

    private:
        /** @brief 그룹 하나의 제목 · 표 · "Add row" 단추를 그립니다. 자체 시험 표식(`bFirstCell` · `bFirstCurveButton`)은 문서 전체에서 처음 한 번만 남기도록 호출 사이로 이어 받습니다. */
        void drawGroup( const CatalogDocument& document, const CatalogDocument::Group& group, bool bFirstGroup, bool& inoutbFirstCell, bool& inoutbFirstCurveButton, bool& inoutbFirstVariantButton );
        /** @brief 표의 한 행(칸 · 자식 수 · 곡선 단추 · 제거 단추)을 그립니다. */
        void drawRow( const CatalogDocument& document, const CatalogDocument::Group& group, int32 elementIndex, int32 rowPosition, bool bFirstGroup, bool& inoutbFirstCell, bool& inoutbFirstCurveButton, bool& inoutbFirstVariantButton );
        /** @param width 칸 폭입니다. 음수면 남은 폭 전부(표 칸), 양수면 그 폭(곡선 키 줄 — 한 줄에 칸 셋이 선다)입니다. */
        void drawCell( const CatalogDocument& document, int32 elementIndex, const string& column, bool bFirstCell, float32 width );
        /**
         * @brief 고칠 수 있는 칸 하나를 그립니다. 입력 칸을 떠날 때 값이 바뀌었으면 @p outCommitted 에 담아 true 입니다.
         * @param bAbsent 이 항목이 적지 않은 칸입니다(어둡게 — 비어 있는 값과 다르다).
         * @param bDim 물려받은 값입니다(흐리게 — 이 항목이 적은 값이 아니다).
         */
        [[nodiscard]] bool drawValueField( string_view shown, bool bAbsent, bool bDim, float32 width, bool bMarkFirst, string& outCommitted );
        /** @brief "자식 원소" 창입니다 - 행의 자식 원소를 규칙대로 펼쳐 보이고 고칩니다. 적은 칸만 저장합니다(`CatalogDocument::setChildCell`). */
        void drawChildPopup( const CatalogDocument& document );
        void drawChildSection( const CatalogChildSection& section, bool& inoutbFirstCell, bool& inoutbFirstAdd, bool& inoutbFirstRemove );
        void drawChildSectionActions( const CatalogChildSection& section, bool& inoutbFirstAdd );
        void drawChildNode( const CatalogChildSection& section, const CatalogChildNode& node, int32 nodePosition, bool& inoutbFirstCell, bool& inoutbFirstRemove );
        /** @brief "곡선 편집" 창입니다 - @p _curveOwner 원소 아래의 모든 곡선을 그래프 · 그래디언트 띠 · 키 표로 고칩니다(키 칸은 `drawCell` 과 같은 길이라 되돌리기 한 단계 = 칸 편집 한 번). */
        void drawCurvePopup( const CatalogDocument& document );
        void drawGradientBar( const CatalogGradient& gradient );
        /** @brief 곡선 창 위쪽 그래프입니다 — 미리보기로 고른 곡선(`_graphCurve`)의 키를 끌어 고치고, 놓으면 `SetCurveKeys` 편집 하나를 남깁니다. */
        void               drawCurveGraph();
        void               drawCurveBlock( const CatalogDocument& document, const CatalogCurve& curve );
        [[nodiscard]] bool applyPending( CatalogDocument& inoutDocument );

    private:
        vector<CatalogCurve>                    _listCurve;           ///< 곡선 창이 이번 프레임에 그리는 곡선
        vector<CatalogGradient>                 _listGradient;        ///< 그 가운데 네 채널이 갖춰진 묶음
        vector<CatalogCurve>                    _listProbeCurve;      ///< 행마다 "곡선이 있나" 를 묻는 재사용 버퍼
        vector<float32>                         _listSample;          ///< 곡선 표본 재사용 버퍼
        vector<float4>                          _listGradientColor;   ///< 그래디언트 표본 재사용 버퍼
        vector<int32>                           _listVisibleRow;      ///< 행 거르개에 맞는 행 재사용 버퍼
        vector<CatalogChildSection>             _listChildSection;    ///< 자식 창이 이번 프레임에 그리는 표들
        int32                                   _curveOwner;          ///< 곡선 창이 보는 원소(-1 이면 닫힘)
        int32                                   _childOwner;          ///< 자식 창이 보는 행(-1 이면 닫힘). 고쳐도 번호가 그대로다 - 행 안쪽만 고치고 원소 번호는 문서 순서다
        int32                                   _graphCurve;          ///< 곡선 창의 그래프가 고치는 곡선 원소(-1 이면 그래프를 닫았다)
        uint8                                   _bOpenCurvePopup : 1; ///< 다음 그리기에서 창을 연다
        uint8                                   _bOpenChildPopup : 1; ///< 다음 그리기에서 자식 창을 연다
        uint8                                   _bEditFailed     : 1; ///< 지난 편집이 거절되었다(`takeEditFailed`)
        string                                  _editText;
        string                                  _lastActionText;
        uint32                                  _editID;
        CatalogTableEdit                        _pending;    ///< 표를 다 그린 뒤 반영할 편집 하나
        EditorCurveEditorState                  _graphState; ///< 곡선 창 그래프(키 끌기 · 메뉴) 상태 — 놓을 때 `SetCurveKeys` 편집 하나가 된다
        fixed_string<constant::buffer::kMax128> _rowFilter;
    };
} // namespace sw::editor
