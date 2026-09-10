#include "pch.h"

#include "Editor/Common/Gui/IEditorPanel.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /**
     * @brief 문서를 하나 들고 있는 최소 패널.
     * @details `IEditorPanel` 의 문서 계약이 파생에게 무엇을 요구하는지를 그대로 보여 준다 —
     *          dirty 비트를 들거나 네 메서드를 다시 구현하는 것이 아니라, 알리기(markDocumentDirty)
     *          와 실제 저장·되돌리기(saveDocument/revertDocument)만 구현한다.
     */
    class FakeDocumentPanel : public IEditorPanel
    {
    public:
        FakeDocumentPanel()
            : IEditorPanel{ false }
        {
        }

        const utf8* getPanelTitle() const override { return "Fake Document"; }

        /** @brief 패널 바깥에서 편집을 흉내 냅니다. */
        void edit() { markDocumentDirty(); }

        int32 getSaveCount() const { return _saveCount; }
        int32 getRevertCount() const { return _revertCount; }
        void  setSaveSucceeds( bool bSucceeds ) { _bSaveSucceeds = bSucceeds; }

    protected:
        void drawContent() override {}

        bool saveDocument() override
        {
            ++_saveCount;
            if ( _bSaveSucceeds == false )
                return false;
            clearDocumentDirty();
            return true;
        }

        void revertDocument() override { ++_revertCount; }

    private:
        int32 _saveCount{ 0 };
        int32 _revertCount{ 0 };
        bool  _bSaveSucceeds{ true };
    };

    /** @brief 문서가 없는 패널 — 계약을 아무것도 구현하지 않는다. */
    class FakePlainPanel : public IEditorPanel
    {
    public:
        const utf8* getPanelTitle() const override { return "Fake Plain"; }

    protected:
        void drawContent() override {}
    };
} // namespace

/**
 * @brief [EditorPanelDocumentTest] 편집을 알리기만 하면 dirty 상태가 잡히는지 검증
 * @details InputMapEditorPanel 이 자기 `_bDirty` 만 들고 계약을 구현하지 않아, 화면에는 미저장
 *          표시를 띄우면서 Ctrl+S(`saveFocusedOrScene`)와 종료 확인에는 보이지 않았다. 이제
 *          비트를 기반이 들기 때문에 그 반쪽 상태가 존재할 수 없다.
 */
SW_TEST_CASE( EditorPanelDocumentTest, MarkDirtyIsVisibleToBaseContract )
{
    FakeDocumentPanel panel;
    SW_EXPECT_FALSE( panel.isDocumentDirty() );

    panel.edit();
    SW_EXPECT_TRUE( panel.isDocumentDirty() );
}

/**
 * @brief [EditorPanelDocumentTest] dirty일 때만 저장하고, 저장 성공 시 dirty가 지워지는지 검증
 */
SW_TEST_CASE( EditorPanelDocumentTest, SaveRunsOnlyWhenDirty )
{
    FakeDocumentPanel panel;

    // 깨끗하면 저장하지 않는다 — saveAllDirtyDocuments 가 매 패널을 훑을 때 헛일하지 않도록.
    SW_EXPECT_FALSE( panel.trySaveDirtyDocument() );
    SW_EXPECT_EQUAL( 0, panel.getSaveCount() );

    panel.edit();
    SW_EXPECT_TRUE( panel.trySaveDirtyDocument() );
    SW_EXPECT_EQUAL( 1, panel.getSaveCount() );
    SW_EXPECT_FALSE( panel.isDocumentDirty() );
}

/**
 * @brief [EditorPanelDocumentTest] 저장이 실패하면 dirty가 남는지 검증
 * @details 종료 흐름은 저장 실패를 보고 종료를 멈춘다(`applyUnsavedSceneChoice`). 실패한 저장이
 *          dirty 를 지우면 편집이 조용히 사라진다.
 */
SW_TEST_CASE( EditorPanelDocumentTest, FailedSaveKeepsDocumentDirty )
{
    FakeDocumentPanel panel;
    panel.setSaveSucceeds( false );
    panel.edit();

    SW_EXPECT_FALSE( panel.trySaveDirtyDocument() );
    SW_EXPECT_EQUAL( 1, panel.getSaveCount() );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );
}

/**
 * @brief [EditorPanelDocumentTest] 버리기는 되돌리기를 부른 뒤 dirty를 지우고, 깨끗하면 아무것도 하지 않는지 검증
 */
SW_TEST_CASE( EditorPanelDocumentTest, DiscardRevertsOnlyWhenDirty )
{
    FakeDocumentPanel panel;

    panel.discardDirtyDocument();
    SW_EXPECT_EQUAL( 0, panel.getRevertCount() );

    panel.edit();
    panel.discardDirtyDocument();
    SW_EXPECT_EQUAL( 1, panel.getRevertCount() );
    SW_EXPECT_FALSE( panel.isDocumentDirty() );
}

/**
 * @brief [EditorPanelDocumentTest] 문서가 없는 패널은 계약을 구현하지 않아도 조용히 지나가는지 검증
 */
SW_TEST_CASE( EditorPanelDocumentTest, PanelWithoutDocumentStaysClean )
{
    FakePlainPanel panel;

    SW_EXPECT_FALSE( panel.isDocumentDirty() );
    SW_EXPECT_FALSE( panel.trySaveDirtyDocument() );
    panel.discardDirtyDocument();
    SW_EXPECT_FALSE( panel.isDocumentDirty() );
}
