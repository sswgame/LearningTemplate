/**
 * @file CatalogTableEditor.h
 * @brief `CatalogDocument` 를 원소 이름별 테이블로 그리고 셀 · 행을 고치는 ImGui 위젯입니다. 카탈로그 편집기 창과 Data Table 의 게임 데이터 탭이 같이 씁니다.
 * @details 셀은 글을 칠 때 문서를 바꾸지 않고, 입력 필드를 떠날 때 한 번 반영합니다(되돌리기 한 단계 = 셀 편집 한 번). 반영 · 행 더하기 · 행 빼기는 테이블을 다 그린 뒤에 합니다 —
 *          그리는 도중 문서의 원소 인덱스가 바뀌면 그리던 테이블이 깨지기 때문입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Editor/Panels/Catalog/Model/CatalogCurveModel.h"

namespace sw::editor
{
    class CatalogDocument;

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
        /** @brief 마지막으로 반영한 편집의 한 줄 설명입니다(상태 줄과 로그가 씁니다). */
        const string& getLastActionText() const { return _lastActionText; }

    private:
        /** @brief 테이블을 다 그린 뒤 반영할 편집 하나입니다. */
        enum class PendingKind : uint8
        {
            None = 0,
            SetCell,
            UnsetCell,
            AddRow,
            RemoveRow,
            AddCurveKey ///< `_pendingElement` 곡선의 끝에 키를 더한다
        };

        /** @brief 그룹 하나의 제목 · 표 · "Add row" 단추를 그립니다. 자체 시험 표식(`bFirstCell` · `bFirstCurveButton`)은 문서 전체에서 처음 한 번만 남기도록 호출 사이로 이어 받습니다. */
        void drawGroup( const CatalogDocument& document, const CatalogDocument::Group& group, bool bFirstGroup, bool& inoutbFirstCell, bool& inoutbFirstCurveButton );
        /** @brief 표의 한 행(칸 · 자식 수 · 곡선 단추 · 제거 단추)을 그립니다. */
        void drawRow( const CatalogDocument& document, const CatalogDocument::Group& group, int32 rowPosition, bool bFirstGroup, bool& inoutbFirstCell, bool& inoutbFirstCurveButton );
        void drawCell( const CatalogDocument& document, int32 elementIndex, const string& column, bool bFirstCell );
        /** @brief "곡선 편집" 창입니다 - @p _curveOwner 원소 아래의 모든 곡선을 그래프 · 그래디언트 띠 · 키 표로 고칩니다(키 칸은 `drawCell` 과 같은 길이라 되돌리기 한 단계 = 칸 편집 한 번). */
        void               drawCurvePopup( const CatalogDocument& document );
        void               drawGradientBar( const CatalogGradient& gradient ) const;
        void               drawCurveBlock( const CatalogDocument& document, const CatalogCurve& curve );
        [[nodiscard]] bool applyPending( CatalogDocument& inoutDocument );

        vector<CatalogCurve>    _listCurve;       ///< 곡선 창이 이번 프레임에 그리는 곡선
        vector<CatalogGradient> _listGradient;    ///< 그 가운데 네 채널이 갖춰진 묶음
        vector<CatalogCurve>    _listProbeCurve;  ///< 행마다 "곡선이 있나" 를 묻는 재사용 버퍼
        vector<float32>         _listSample;      ///< 곡선 표본 재사용 버퍼
        int32                   _curveOwner;      ///< 곡선 창이 보는 원소(-1 이면 닫힘)
        bool                    _bOpenCurvePopup; ///< 다음 그리기에서 창을 연다
        string                  _editText;
        string                  _pendingColumn;
        string                  _pendingText;
        string                  _lastActionText;
        uint32                  _editID;
        int32                   _pendingElement;
        PendingKind             _pendingKind;
    };
} // namespace sw::editor
