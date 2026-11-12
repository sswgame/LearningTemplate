/**
 * @file Editor/Common/Workspace/Undo/DocumentEditSpan.h
 * @brief 문서 편집 하나의 상호작용 구간(위젯 활성 ~ 해제)입니다 — 구간 전 글을 쥐고 끝에 (전, 후)를 한 번 냅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @struct DocumentEdit
     * @brief 되돌리기 한 단계로 기록할 문서 편집입니다 — 구간 전 글, 끝 글, 이름.
     */
    struct DocumentEdit
    {
        string _before;
        string _after;
        string _label;
    };

    /**
     * @class DocumentEditSpan
     * @brief 문서 패널의 편집 하나를 상호작용 구간 하나로 만듭니다. 구간 동안은 기록하지 않고, 끝나면 (구간 전 글, 끝 글)을 한 번 낸다.
     * @details 예전에는 합치기 키(`CommandStack::pushCoalesce`)로 이어진 편집을 합쳤는데, 기록이 거래 구간 안이라 병합에 닿지 않아 끌기 한 번이 프레임마다
     *          한 단계였다(감사 P0-1). 구간을 형식으로 두면 키 문자열 · 병합 분기가 없다. 언리얼이 누를 때 `FScopedTransaction` 을 열고 뗄 때 닫는 것,
     *          유니티 `Undo.CollapseUndoOperations`, Godot `MERGE_ENDS` 와 같은 자리다. ImGui 를 모른다 — 구간이 열려 있는지는 부르는 쪽(View)이 넘긴다.
     */
    class SW_EDITOR_API DocumentEditSpan final
    {
    public:
        DocumentEditSpan();

        /** @brief 기준 글을 @p text 로 맞춥니다(로드 · 저장 · 되돌리기 뒤). 쥔 구간은 버린다 — 그 기준선이 없어졌다. */
        void resetBaseline( string text );
        /** @brief 마지막으로 기록한(또는 맞춘) 글입니다. */
        const string& getBaseline() const { return _baseline; }
        /** @brief 구간을 쥐고 있으면 true 입니다(편집이 있었고 아직 기록하지 않았다). */
        bool isHolding() const { return _bHolding == SW_TRUE; }

        /**
         * @brief 편집을 알립니다. @p bInteracting 이면 구간을 열거나 이어 쥐고 아무것도 내지 않는다. 아니면 기준 → @p after 를 낸다.
         * @return 기록할 편집입니다. 바뀐 것이 없거나 구간을 쥐었으면 비어 있다(쥐었는지는 `isHolding`).
         */
        [[nodiscard]] optional<DocumentEdit> notifyEdited( string_view label, string after, bool bInteracting );
        /**
         * @brief 구간이 끝났습니다 — 쥔 구간을 (기준, @p after) 로 냅니다. 쥔 것이 없거나 처음 글로 돌아왔으면 비어 있다.
         * @details 쥔 구간이 있을 때만 부른다(`isHolding`) — @p after 는 문서 전체 글이라 부르는 쪽이 그때만 만든다.
         */
        [[nodiscard]] optional<DocumentEdit> finish( string after );

    private:
        string                 _baseline;     ///< 구간 전 글(마지막으로 기록한 글)
        string                 _pendingLabel; ///< 쥔 구간의 되돌리기 이름(구간의 마지막 알림)
        uint8                  _bHolding : 1; ///< 구간을 쥐고 있다
        [[maybe_unused]] uint8 _reserved : 7;
    };
} // namespace sw::editor
