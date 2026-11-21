/**
 * @file Editor/Common/Workspace/Undo/DocumentEditSpan.h
 * @brief 문서 편집 하나의 상호작용 구간(위젯 활성 ~ 해제)입니다 — 구간 전 글을 쥐고 끝에 (전, 후)를 한 번 냅니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorExports.h"

namespace sw::editor
{
    /**
     * @struct DocumentEdit
     * @brief 되돌리기 한 단계로 기록할 문서 편집입니다 — 구간 전 글, 끝 글, 이름.
     * @details 글은 공유 버퍼다 — 연속한 편집에서 앞 단계의 끝 글이 곧 다음 단계의 전 글이고 구간의 기준선이라 한 벌만 든다(타일맵 · 그래프 문서는 MB 단위).
     *          VS Code piece tree · Godot `UndoRedo` 처럼 바뀐 것만 남기는 것은 기록 쪽(`EditorTransaction::recordDocumentText` 의 바뀐 구간)이 한다.
     */
    struct DocumentEdit
    {
        shared_ptr<const string> _pBefore; ///< 구간 전 글(앞 편집의 `_pAfter` 와 같은 버퍼)
        shared_ptr<const string> _pAfter;  ///< 끝 글(구간의 다음 기준선과 같은 버퍼)
        string                   _label;

        /** @brief 글 @p before · @p after 를 새 버퍼로 담습니다(구간 밖에서 만든 편집 — 자동 저장 사본 복원). */
        static DocumentEdit make( string before, string after, string label )
        {
            return DocumentEdit{ make_shared<const string>( std::move( before ) ), make_shared<const string>( std::move( after ) ), std::move( label ) };
        }
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
        const string& getBaseline() const { return *_pBaseline; }
        /** @brief 구간을 쥐고 있으면 true 입니다(편집이 있었고 아직 기록하지 않았다). */
        bool isHolding() const { return _bHolding == SW_TRUE; }

        /**
         * @brief 편집을 알립니다. @p bInteracting 이면 구간을 열거나 이어 쥐고 아무것도 내지 않는다. 아니면 기준 → 지금 글을 낸다.
         * @param capture 지금 문서 전체 글을 만드는 함수(`string()`)입니다. **필요할 때만 부른다** — 이미 쥔 구간을 이어 쥘 때는 부르지 않는다.
         * @details 예전에는 알림마다 문서 전체를 직렬화해 기준과 견줬다 — 끌기 · 칠하기는 매 프레임 알려 타일맵 512² 면 프레임마다 MB 단위였다(감사 E-9).
         *          직렬화는 구간의 시작(바뀌었는지)과 끝(`finish`) 두 번이다. 언리얼 dirty 깃발 · Godot 프로퍼티 do/undo 처럼 끄는 동안은 표시만 한다.
         * @return 기록할 편집입니다. 바뀐 것이 없거나 구간을 쥐었으면 비어 있다(쥐었는지는 `isHolding`).
         */
        template <typename TCapture>
        [[nodiscard]] optional<DocumentEdit> notifyEdited( string_view label, bool bInteracting, TCapture&& capture )
        {
            if ( bInteracting && isHolding() )
            {
                _pendingLabel = string{ label }; // 쥔 구간을 잇는다 — 끝 글은 `finish` 가 한 번 만든다
                return nullopt;
            }
            return notifyEditedText( label, capture(), bInteracting );
        }
        /**
         * @brief 구간이 끝났습니다 — 쥔 구간을 (기준, @p after) 로 냅니다. 쥔 것이 없거나 처음 글로 돌아왔으면 비어 있다.
         * @details 쥔 구간이 있을 때만 부른다(`isHolding`) — @p after 는 문서 전체 글이라 부르는 쪽이 그때만 만든다.
         */
        [[nodiscard]] optional<DocumentEdit> finish( string after );

    private:
        /** @brief 지금 글 @p after 로 편집을 판정합니다(`notifyEdited` 가 글이 필요할 때만 부른다). */
        [[nodiscard]] optional<DocumentEdit> notifyEditedText( string_view label, string after, bool bInteracting );

    private:
        shared_ptr<const string> _pBaseline;    ///< 구간 전 글(마지막으로 기록한 글 — 낸 편집의 `_pAfter` 와 같은 버퍼)
        string                   _pendingLabel; ///< 쥔 구간의 되돌리기 이름(구간의 마지막 알림)
        uint8                    _bHolding : 1; ///< 구간을 쥐고 있다
        [[maybe_unused]] uint8   _reserved : 7;
    };
} // namespace sw::editor
