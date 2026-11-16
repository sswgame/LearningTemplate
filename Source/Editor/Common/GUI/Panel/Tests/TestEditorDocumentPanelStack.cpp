// EditorDocumentPanelStackTest — 문서 패널마다 되돌리기 스택이 하나다(감사 E-3): 문서 편집은 그 문서의 스택에만 쌓여 메인(씬) 스택의 플레이 경계(`suspendHistory`)와 무관하고,
// 그 스택으로 되돌리면 글과 dirty 가 돌아오며, 문서를 다시 읽으면 앞 글의 기록을 비운다.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Editor/Common/GUI/Panel/EditorDocumentPanel.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 글 한 줄이 문서인 시험용 문서 패널입니다. 그리지 않고 편집 · 되돌리기 · 다시 읽기만 부른다. */
    class StackProbePanel final : public EditorDocumentPanel
    {
    public:
        StackProbePanel()
            : EditorDocumentPanel{ EditorAssetType{}, false }
        {
        }

        using EditorDocumentPanel::getDocumentCommandStack;
        using EditorDocumentPanel::syncDocumentUndoBaseline;

        /** @brief 글을 @p text 로 바꾸고 편집을 알립니다. */
        void edit( string_view text )
        {
            _text = string{ text };
            notifyDocumentEdited( "Probe Edit" );
        }
        /** @brief 디스크에서 다시 읽은 것처럼 @p text 로 문서를 읽습니다(`reloadDocument`). */
        void reloadFrom( string_view text )
        {
            _diskText = string{ text };
            EditorDocumentPanel::reloadDocument();
        }
        const string& getText() const { return _text; }

    protected:
        void                drawDocumentContent() override {}
        ToolAssetLoadResult loadDocument() override
        {
            _text = _diskText;
            return ToolAssetLoadResult::Loaded;
        }
        [[nodiscard]] bool saveDocument() override { return true; }
        string             captureDocumentText() const override { return _text; }
        void               applyDocumentText( string_view text ) override { _text = string{ text }; }

    private:
        string _text{ "a" };
        string _diskText{ "a" };
    };
} // namespace

/** @brief [EditorDocumentPanelStackTest] 문서 편집은 그 문서의 스택에만 쌓이고 메인 스택 수는 그대로다. 그 스택의 undo 가 글과 dirty 를 돌린다 */
SW_TEST_CASE( EditorDocumentPanelStackTest, EditGoesToTheDocumentStack )
{
    const CommandStack* const pMain           = editor::findSubsystem<CommandStack>(); // 에디터 범위의 메인 스택(없는 호스트면 비교를 건너뛴다)
    const size_t              mainCountBefore = pMain != nullptr ? pMain->getCommandCount() : 0u;

    StackProbePanel panel;
    panel.syncDocumentUndoBaseline();
    panel.edit( "ab" );

    CommandStack& stack = panel.getDocumentCommandStack();
    SW_REQUIRE_TRUE( &stack != pMain ); // 플레이 경계(`suspendHistory`)는 메인 스택만 맡는다 — 문서 스택은 다른 객체라 넘는다
    SW_REQUIRE_EQUAL( 1u, stack.getCommandCount() );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );
    if ( pMain != nullptr )
        SW_EXPECT_EQUAL( mainCountBefore, pMain->getCommandCount() ); // 예전에는 미리보기 없는 문서 패널이 메인 스택에 쌓아 플레이 경계가 함께 버렸다

    stack.undo();
    SW_EXPECT_TRUE( panel.getText() == "a" );
    SW_EXPECT_FALSE( panel.isDocumentDirty() ); // 저장한 글로 돌아왔다
    stack.redo();
    SW_EXPECT_TRUE( panel.getText() == "ab" );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );
}

/** @brief [EditorDocumentPanelStackTest] 문서를 다시 읽으면 그 문서의 기록을 비운다 — 앞 글의 구간을 새 글에 붙이면 문서가 깨진다 */
SW_TEST_CASE( EditorDocumentPanelStackTest, ReloadClearsTheDocumentStack )
{
    StackProbePanel panel;
    panel.syncDocumentUndoBaseline();
    panel.edit( "abc" );
    SW_REQUIRE_EQUAL( 1u, panel.getDocumentCommandStack().getCommandCount() );

    panel.reloadFrom( "rewritten on disk" );
    SW_EXPECT_EQUAL( 0u, panel.getDocumentCommandStack().getCommandCount() );
    SW_EXPECT_TRUE( panel.getText() == "rewritten on disk" );
    SW_EXPECT_FALSE( panel.isDocumentDirty() );
}
