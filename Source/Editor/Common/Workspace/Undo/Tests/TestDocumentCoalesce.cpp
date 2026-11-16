// DocumentCoalesceTest — 문서 편집의 상호작용 구간(`DocumentEditSpan`)을 `EditorTransaction` 경로로 본다: 끌기 한 번(프레임마다 거래 구간 하나)이
// 되돌리기 한 단계이고, 그 단계는 문서 패널이 답한 스택에 간다(nogpu).
// 예전에는 합치기 키(`CommandStack::pushCoalesce`)로 합쳤는데 기록이 거래 구간 안이라 병합에 닿지 않아 끌기 한 번이 프레임마다 한 단계였고(감사 P0-1),
// 옛 테스트는 `pushCoalesce` 를 직접 불러 그 길을 보지 못했다. 미리보기 창의 문서 편집은 전역 스택으로 가 그 창의 Ctrl+Z 가 비어 있었다(P0-2).
#include "pch.h"

#include "Core/Subsystem/SubsystemScope.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/Common/Workspace/Undo/DocumentEditSpan.h"
#include "Editor/Common/Workspace/Undo/UndoStep.h"

namespace
{
    /** @brief 문서 하나(글 한 벌)와 그 되돌리기 배관입니다 — 문서 패널의 `recordDocumentEdit` 와 같은 길로 기록한다. */
    struct DocumentCoalesceTestDocument
    {
        sw::string                   _text{};
        sw::editor::DocumentEditSpan _span{};
        sw::editor::UndoStepOwner    _owner{};

        void       restore( sw::string_view text ) { _text = sw::string{ text }; }
        sw::string capture() const { return _text; }

        /** @brief 한 프레임의 편집입니다 — 그 프레임의 거래 구간 안에서 알린다(문서 패널 · 명령 레지스트리가 여는 구간과 같다). */
        void editInFrame( sw::string_view text, bool bInteracting, sw::CommandStack& targetStack )
        {
            sw::editor::EditorTransaction transaction;
            _text                                             = sw::string{ text };
            const sw::optional<sw::editor::DocumentEdit> edit = _span.notifyEdited( "Drag", _text, bInteracting );
            if ( edit.has_value() )
                record( transaction, *edit, targetStack );
        }

        /** @brief 위젯이 풀린 프레임입니다(`flushDocumentInteraction`). */
        void releaseInFrame( sw::CommandStack& targetStack )
        {
            sw::editor::EditorTransaction transaction;
            if ( _span.isHolding() == false )
                return;
            const sw::optional<sw::editor::DocumentEdit> edit = _span.finish( _text );
            if ( edit.has_value() )
                record( transaction, *edit, targetStack );
        }

        void record( sw::editor::EditorTransaction& transaction, const sw::editor::DocumentEdit& edit, sw::CommandStack& targetStack )
        {
            transaction.recordDocumentText( edit._before, edit._after, edit._label, _owner,
                                            SW_DELEGATE_METHOD( sw::editor::EditorDocumentRestoreDelegate, &DocumentCoalesceTestDocument::restore, this ),
                                            SW_DELEGATE_METHOD( sw::editor::EditorDocumentCaptureDelegate, &DocumentCoalesceTestDocument::capture, this ),
                                            targetStack );
        }
    };
} // namespace

/** @brief [DocumentCoalesceTest] 거래 구간 안의 끌기 세 프레임과 해제는 한 단계이고, 되돌리면 처음 글 · 다시 하면 끝 글이다 */
SW_TEST_CASE( DocumentCoalesceTest, TransactionScopedEditsCoalesce )
{
    sw::CommandStack   stack;
    sw::SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind( &stack ) );
    const sw::editor::ScopedEditorScope scopedEditor( scope );

    DocumentCoalesceTestDocument document;
    document._text = "roughness=0.5";
    document._span.resetBaseline( document._text );

    document.editInFrame( "roughness=0.6", true, stack );
    document.editInFrame( "roughness=0.7", true, stack );
    document.editInFrame( "roughness=0.8", true, stack );
    SW_EXPECT_EQUAL( stack.getCommandCount(), size_t{ 0 } ); // 구간 동안은 기록하지 않는다
    SW_EXPECT_TRUE( document._span.isHolding() );

    document.releaseInFrame( stack );
    SW_EXPECT_EQUAL( stack.getCommandCount(), size_t{ 1 } );

    stack.undo();
    SW_EXPECT_TRUE( document._text == "roughness=0.5" );
    stack.redo();
    SW_EXPECT_TRUE( document._text == "roughness=0.8" );
}

/** @brief [DocumentCoalesceTest] 구간 밖의 편집은 편집마다 한 단계이고, 처음 글로 돌아온 구간은 단계를 남기지 않는다 */
SW_TEST_CASE( DocumentCoalesceTest, SpanReturningToStartRecordsNothing )
{
    sw::CommandStack   stack;
    sw::SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind( &stack ) );
    const sw::editor::ScopedEditorScope scopedEditor( scope );

    DocumentCoalesceTestDocument document;
    document._text = "a";
    document._span.resetBaseline( document._text );

    document.editInFrame( "b", false, stack );
    document.editInFrame( "c", false, stack );
    SW_EXPECT_EQUAL( stack.getCommandCount(), size_t{ 2 } );

    document.editInFrame( "d", true, stack );
    document.editInFrame( "c", true, stack ); // 끌다가 구간 전 값으로 돌아왔다
    document.releaseInFrame( stack );
    SW_EXPECT_EQUAL( stack.getCommandCount(), size_t{ 2 } );
}

/** @brief [DocumentCoalesceTest] 기록은 문서 패널이 답한 스택(미리보기 창의 스택)에 가고 작업 공간 스택에는 가지 않는다 — 그 창의 Ctrl+Z 가 되돌린다 */
SW_TEST_CASE( DocumentCoalesceTest, RecordGoesToTheDocumentStack )
{
    sw::CommandStack   workspaceStack;
    sw::SubsystemScope scope;
    SW_REQUIRE_TRUE( scope.bind( &workspaceStack ) );
    const sw::editor::ScopedEditorScope scopedEditor( scope );

    sw::CommandStack             previewStack;
    DocumentCoalesceTestDocument document;
    document._text = "before";
    document._span.resetBaseline( document._text );

    document.editInFrame( "after", true, previewStack );
    document.releaseInFrame( previewStack );
    SW_EXPECT_EQUAL( previewStack.getCommandCount(), size_t{ 1 } );
    SW_EXPECT_EQUAL( workspaceStack.getCommandCount(), size_t{ 0 } );

    previewStack.undo();
    SW_EXPECT_TRUE( document._text == "before" );
}
