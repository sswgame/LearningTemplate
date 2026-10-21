/**
 * @file CatalogTableEditor.h
 * @brief `CatalogDocument` 를 원소 이름별 테이블로 그리고 셀 · 행을 고치는 ImGui 위젯입니다. 카탈로그 편집기 창과 Data Table 의 게임 데이터 탭이 같이 씁니다.
 * @details 셀은 글을 칠 때 문서를 바꾸지 않고, 입력 필드를 떠날 때 한 번 반영합니다(되돌리기 한 단계 = 셀 편집 한 번). 반영 · 행 더하기 · 행 빼기는 테이블을 다 그린 뒤에 합니다 —
 *          그리는 도중 문서의 원소 인덱스가 바뀌면 그리던 테이블이 깨지기 때문입니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

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
            RemoveRow
        };

        void               drawCell( const CatalogDocument& document, int32 elementIndex, const string& column, bool bFirstCell );
        [[nodiscard]] bool applyPending( CatalogDocument& inoutDocument );

        string      _editText;
        string      _pendingColumn;
        string      _pendingText;
        string      _lastActionText;
        uint32      _editID;
        int32       _pendingElement;
        PendingKind _pendingKind;
    };
} // namespace sw::editor
