/**
 * @file EditorStateTextHistory.h
 * @brief 에셋 문서가 없는 상태 패널(프로젝트 설정 · 환경설정)의 되돌리기입니다 — 상태를 글로 담아 편집 구간 하나를 한 단계로 올립니다(ImGui 없음).
 * @details 에셋 문서 패널은 `EditorDocumentPanel` 이 문서 글로 같은 일을 한다. 상태 패널은 대상이 여럿(설정 페이지 · 섹션)이고 파일이 아니라 리플렉션 인스턴스라
 *          그 길을 쓸 수 없어 그리드 편집 · 기본값 되돌리기가 기록 없이 남았다. 언리얼은 설정 객체도 `FScopedTransaction` 안에서 고쳐 같은 Ctrl+Z 로 되돌린다.
 *          단계는 자기 대상의 복원 함수를 값으로 쥐므로 다른 대상을 고른 뒤 되돌려도 그 편집이 있던 대상으로 돌아간다.
 */
#pragma once
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Types.h"

#include "Editor/Common/EditorExports.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/DocumentEditSpan.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

namespace sw::editor
{
    /**
     * @struct EditorStateTextSource
     * @brief 되돌리기 대상 하나입니다 — 지금 상태 글을 만드는 함수와 그 글을 적용하는 함수입니다. 단계가 값으로 쥐므로 대상(페이지)이 사라지면 기록을 비운다(`clear`).
     */
    struct EditorStateTextSource
    {
        Delegate<string()>            _capture{};
        Delegate<void( string_view )> _restore{};
    };

    /** @class EditorStateTextHistory @brief 파일 머리말 참고. */
    class SW_EDITOR_API EditorStateTextHistory final
    {
    public:
        explicit EditorStateTextHistory( const UndoBufferLimit& limit );
        EditorStateTextHistory( const EditorStateTextHistory& )            = delete;
        EditorStateTextHistory& operator=( const EditorStateTextHistory& ) = delete;

        /** @brief Ctrl+Z 가 갈 스택입니다(`EditorPanel::findPanelCommandStack`). */
        CommandStack& getStack() { return _stack; }
        /** @brief 편집할 대상을 @p source 로 바꿉니다. 쥔 편집이 있으면 먼저 앞 대상의 단계로 올리고, 새 대상의 지금 글이 기준이 된다. */
        void selectSource( const EditorStateTextSource& source );
        /** @brief 지금 대상을 고쳤습니다. @p bInteracting(위젯이 활성)이면 구간을 쥐고 `flush` 가 끝낸다. 글이 같으면 아무것도 올리지 않는다. */
        void notifyEdited( string_view label, bool bInteracting );
        /** @brief 쥔 구간이 있고 위젯이 풀렸으면 한 단계로 올립니다. 패널이 그리기마다 부른다. */
        void flush( bool bInteracting );
        /** @brief 지금 대상을 기록 밖에서 고쳤습니다(파일로 되돌리기) — 기준 글을 다시 잡는다. */
        void resetBaseline();
        /** @brief 글이 아닌 기록(표 문서의 노드 기록)을 움직이는 단계를 같은 스택에 올립니다 — 편집한 차례대로 되돌아가게 한 스택을 쓴다. */
        void pushStep( string_view label, const Delegate<void()>& undo, const Delegate<void()>& redo );
        /** @brief 단계를 모두 버리고 대상을 놓습니다. */
        void clear();

    private:
        void     record( const DocumentEdit& edit );
        void     syncInteraction();
        UndoStep makeRestoreStep( const shared_ptr<const string>& pText );

    private:
        UndoStepOwner                _owner; ///< 단계의 주인 — 스택보다 먼저 선언해 스택이 먼저 사라진다
        CommandStack                 _stack;
        CommandStackInteractionScope _interaction; ///< 구간을 쥔 동안 되돌리기를 거절한다(감사 E-2)
        DocumentEditSpan             _span;
        EditorStateTextSource        _source;
    };
} // namespace sw::editor
