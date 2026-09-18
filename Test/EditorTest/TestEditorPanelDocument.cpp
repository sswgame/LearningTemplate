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

        /**
         * @brief 파생이 하는 일은 **쓰고, 됐는지 답하는 것**이 전부다.
         * @details 일부러 `clearDocumentDirty()` 를 부르지 않는다 — dirty 를 지우는 것은 기반의
         *          `saveDocumentAndClearDirty()` 몫이다. 예전에는 그 순서를 파생 아홉이 각자
         *          구현해서 서로 달랐다(둘은 실패해도 지웠고, 하나는 성공해도 안 지웠다).
         */
        bool saveDocument() override
        {
            ++_saveCount;
            return _bSaveSucceeds;
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

/**
 * @brief [EditorPanelDocumentTest] 파생이 지우지 않아도 성공한 저장은 dirty 를 지운다
 * @details "저장했으면 dirty 를 지운다" 는 순서를 파생 아홉이 각자 구현하고 있었고, 그래서
 *          서로 달랐다 — `AnimationGraphPanel` · `DialogueGraphPanel` 은 **실패해도 무조건**
 *          지우고 `true` 를 돌려줬고(문서를 바꾸거나 닫을 때 확인 없이 편집이 사라진다),
 *          `TileMapPanel` 은 **성공해도 지우지 않았다**(저장했는데 계속 미저장으로 남는다).
 *          순서를 기반(`saveDocumentAndClearDirty`)이 들면 둘 다 존재할 수 없다.
 */
SW_TEST_CASE( EditorPanelDocumentTest, BaseClearsDirtyOnSuccessfulSave )
{
    FakeDocumentPanel panel;
    panel.edit();
    SW_ASSERT_TRUE( panel.isDocumentDirty() );

    // 파생은 저장만 하고 dirty 는 건드리지 않는다 — 그래도 깨끗해져야 한다.
    SW_EXPECT_TRUE( panel.trySaveDirtyDocument() );
    SW_EXPECT_EQUAL( 1, panel.getSaveCount() );
    SW_EXPECT_FALSE( panel.isDocumentDirty() );

    // 실패하면 그대로 남는다 — 그리고 파생이 답한 것이 그대로 나온다.
    panel.edit();
    panel.setSaveSucceeds( false );
    SW_EXPECT_FALSE( panel.trySaveDirtyDocument() );
    SW_EXPECT_EQUAL( 2, panel.getSaveCount() );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );
}
