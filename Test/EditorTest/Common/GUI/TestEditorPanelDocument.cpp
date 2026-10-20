#include "pch.h"

#include "Editor/Common/GUI/EditorDocumentPanel.h"
#include "Editor/Common/GUI/IEditorPanel.h"

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
        void  setSaveAllowed( bool bAllowed ) { _bSaveAllowed = bAllowed; }
        int32 getRevertCount() const { return _revertCount; }
        void  setSaveSucceeds( bool bSucceeds ) { _bSaveSucceeds = bSucceeds; }

    protected:
        void drawContent() override {}

        /**
         * @brief 파생이 하는 일은 **쓰고, 됐는지 답하는 것**이 전부다.
         * @details 일부러 `clearDocumentDirty()` 를 부르지 않는다 — dirty 를 지우는 것은 기반의
         *          `saveDocumentAndClearDirty()` 몫이다. 그 순서를 파생이 각자 구현하면 서로 달라진다
         *          (실패해도 지우거나, 성공해도 안 지우거나).
         */
        [[nodiscard]] bool saveDocument() override
        {
            ++_saveCount;
            return _bSaveSucceeds;
        }

        void revertDocument() override { ++_revertCount; }
        bool canSaveDocument() const override { return _bSaveAllowed; }

    private:
        int32 _saveCount{ 0 };
        int32 _revertCount{ 0 };
        bool  _bSaveSucceeds{ true };
        bool  _bSaveAllowed{ true };
    };

    /** @brief 편집이 패널 밖에 살아서 닫아도 잃는 것이 없는 패널 — 닫기 확인을 두지 않는다(`GlobalVariablesPanel` 이 이렇다). */
    class FakeLivePanel : public FakeDocumentPanel
    {
    protected:
        bool asksBeforeClose() const override { return false; }
    };

    /**
     * @brief 문서 패널의 최소 파생 — `drawDocumentContent` 와 `loadDocument` 와 `saveDocument` 만 구현한다(`revertDocument` 는 구현하지 않는다).
     * @details `EditorDocumentPanelCore.cpp` 가 ImGui 없이 링크되므로 가짜 파생이 링크된다.
     */
    class FakeReloadPanel : public EditorDocumentPanel
    {
    public:
        FakeReloadPanel()
            : EditorDocumentPanel{ EditorAssetType::Data, false }
        {
        }

        void  edit() { markDocumentDirty(); }
        int32 getLoadCount() const { return _loadCount; }

    protected:
        void                drawDocumentContent() override {}
        ToolAssetLoadResult loadDocument() override
        {
            ++_loadCount;
            return ToolAssetLoadResult::Loaded;
        }
        [[nodiscard]] bool saveDocument() override { return true; }

    private:
        int32 _loadCount{ 0 };
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
 * @details 패널이 자기 dirty 비트만 들고 계약을 구현하지 않으면 화면에는 미저장 표시를 띄우면서
 *          Ctrl+S(`saveFocusedOrScene`)와 종료 확인에는 보이지 않는다. 비트를 기반이 들기 때문에 그 반쪽
 *          상태가 존재할 수 없다.
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
 * @details "저장했으면 dirty 를 지운다" 는 순서를 파생이 각자 구현하면 서로 달라진다 — **실패해도 무조건**
 *          지우고 `true` 를 돌려주면 문서를 바꾸거나 닫을 때 확인 없이 편집이 사라지고, **성공해도 지우지 않으면**
 *          저장했는데 계속 미저장으로 남는다. 순서를 기반(`saveDocumentAndClearDirty`)이 들면 둘 다 존재할 수 없다.
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

/**
 * @brief [EditorPanelDocumentTest] 저장해도 되지 않는 문서는 `saveDocument` 를 부르지 않고, dirty 도 그대로 남는다
 * @details 문서를 읽지 못한 패널은 앞 문서의 데이터를 들고 있다(`EditorDocumentPanel::markDocumentLoadFailed`). 그 상태에서 저장하면
 *          읽지 못한 파일을 앞 문서로 덮는다.
 */
SW_TEST_CASE( EditorPanelDocumentTest, SaveIsRefusedWhenTheDocumentCannotBeSaved )
{
    FakeDocumentPanel panel;
    panel.setSaveAllowed( false );
    panel.edit();

    SW_EXPECT_FALSE( panel.saveDocumentAndClearDirty() );
    SW_EXPECT_FALSE( panel.trySaveDirtyDocument() );
    SW_EXPECT_EQUAL( 0, panel.getSaveCount() );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );

    panel.setSaveAllowed( true );
    SW_EXPECT_TRUE( panel.saveDocumentAndClearDirty() );
    SW_EXPECT_EQUAL( 1, panel.getSaveCount() );
}

/**
 * @brief [EditorPanelDocumentTest] 깨끗한 패널은 닫기 요청에 바로 닫히고, 이미 닫힌 패널은 확인을 쥐지 않는다
 */
SW_TEST_CASE( EditorPanelDocumentTest, CleanPanelClosesAtOnce )
{
    FakeDocumentPanel panel;
    panel.setOpen( true );

    panel.setOpen( false );
    SW_EXPECT_FALSE( panel.isOpen() );
    SW_EXPECT_FALSE( panel.isCloseConfirmPending() );

    panel.edit();
    panel.requestClose(); // 닫힌 패널은 그려지지 않는다 — 확인이 남으면 다시 열 때 갑자기 뜬다
    SW_EXPECT_FALSE( panel.isCloseConfirmPending() );
}

/**
 * @brief [EditorPanelDocumentTest] 미저장 편집이 있으면 어느 길로 닫으려 해도 열어 둔 채 확인을 기다린다
 * @details 패널 메뉴 토글 · 레이아웃 불러오기 · 최대화 · 프리팹 창의 Close 는 `setOpen( false )`, X 는 ImGui 가 내린 열림 표시(`absorbCloseButton`)다.
 */
SW_TEST_CASE( EditorPanelDocumentTest, DirtyPanelAsksOnEveryClosePath )
{
    FakeDocumentPanel panel;
    panel.setOpen( true );
    panel.edit();

    panel.setOpen( false ); // 메뉴 토글 등
    SW_EXPECT_TRUE( panel.isOpen() );
    SW_EXPECT_TRUE( panel.isCloseConfirmPending() );

    panel.applyCloseChoice( EditorUnsavedChoice::Cancel );
    SW_EXPECT_TRUE( panel.isOpen() );
    SW_EXPECT_FALSE( panel.isCloseConfirmPending() );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );

    *panel.getOpenFlagAddress() = false; // X: ImGui 는 열림 표시만 내린다
    panel.absorbCloseButton();
    SW_EXPECT_TRUE( panel.isOpen() );
    SW_EXPECT_TRUE( panel.isCloseConfirmPending() );
    SW_EXPECT_EQUAL( 0, panel.getRevertCount() ); // 묻는 동안 아무것도 버리지 않는다
}

/**
 * @brief [EditorPanelDocumentTest] X 가 내린 열림 표시도 깨끗한 패널은 그대로 닫는다
 */
SW_TEST_CASE( EditorPanelDocumentTest, CleanPanelAcceptsTheCloseButton )
{
    FakeDocumentPanel panel;
    panel.setOpen( true );

    *panel.getOpenFlagAddress() = false;
    panel.absorbCloseButton();
    SW_EXPECT_FALSE( panel.isOpen() );
    SW_EXPECT_FALSE( panel.isCloseConfirmPending() );
}

/**
 * @brief [EditorPanelDocumentTest] 확인의 선택에 따라 Save 는 저장하고 닫고, Don't Save 는 버리고 닫고, Cancel 은 열어 둔다
 */
SW_TEST_CASE( EditorPanelDocumentTest, CloseChoiceDecidesWhatHappensToTheEdits )
{
    FakeDocumentPanel panel;
    panel.setOpen( true );

    panel.edit();
    panel.requestClose();
    panel.applyCloseChoice( EditorUnsavedChoice::None ); // 아직 고르지 않았다
    SW_EXPECT_TRUE( panel.isCloseConfirmPending() );
    panel.applyCloseChoice( EditorUnsavedChoice::Save );
    SW_EXPECT_FALSE( panel.isOpen() );
    SW_EXPECT_FALSE( panel.isDocumentDirty() );
    SW_EXPECT_EQUAL( 1, panel.getSaveCount() );
    SW_EXPECT_EQUAL( 0, panel.getRevertCount() );

    panel.setOpen( true );
    panel.edit();
    panel.requestClose();
    panel.applyCloseChoice( EditorUnsavedChoice::Discard );
    SW_EXPECT_FALSE( panel.isOpen() );
    SW_EXPECT_FALSE( panel.isDocumentDirty() );
    SW_EXPECT_EQUAL( 1, panel.getSaveCount() );
    SW_EXPECT_EQUAL( 1, panel.getRevertCount() );
}

/**
 * @brief [EditorPanelDocumentTest] 저장하지 못하면 닫지 않는다 — 닫으면 편집이 사라진다
 */
SW_TEST_CASE( EditorPanelDocumentTest, FailedSaveKeepsThePanelOpen )
{
    FakeDocumentPanel panel;
    panel.setOpen( true );
    panel.setSaveSucceeds( false );
    panel.edit();
    panel.requestClose();

    panel.applyCloseChoice( EditorUnsavedChoice::Save );
    SW_EXPECT_TRUE( panel.isOpen() );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );
    SW_EXPECT_FALSE( panel.isCloseConfirmPending() ); // 확인은 끝났다 — 다시 닫으려 하면 다시 묻는다
}

/**
 * @brief [EditorPanelDocumentTest] 편집이 패널 밖에 사는 패널은 확인 없이 닫히고 편집은 그대로 남는다
 */
SW_TEST_CASE( EditorPanelDocumentTest, LivePanelClosesWithoutAsking )
{
    FakeLivePanel panel;
    panel.setOpen( true );
    panel.edit();

    panel.setOpen( false );
    SW_EXPECT_FALSE( panel.isOpen() );
    SW_EXPECT_FALSE( panel.isCloseConfirmPending() );
    SW_EXPECT_TRUE( panel.isDocumentDirty() );
    SW_EXPECT_EQUAL( 0, panel.getRevertCount() );
}

/**
 * @brief [EditorPanelDocumentTest] 문서 패널이 `revertDocument` 를 구현하지 않아도 "저장 안 함" 은 문서를 다시 읽어 편집을 버린다
 * @details 기본이 없으면 저장하지 않고 닫은 뒤에도 편집한 메모리 상태가 남아, 다시 열면 미저장 표시 없이 편집본이 보인다.
 */
SW_TEST_CASE( EditorPanelDocumentTest, DiscardReloadsTheDocumentByDefault )
{
    FakeReloadPanel panel;
    SW_EXPECT_EQUAL( 0, panel.getLoadCount() );

    panel.discardDirtyDocument(); // 깨끗하면 읽지 않는다
    SW_EXPECT_EQUAL( 0, panel.getLoadCount() );

    panel.edit();
    panel.discardDirtyDocument();
    SW_EXPECT_EQUAL( 1, panel.getLoadCount() );
    SW_EXPECT_FALSE( panel.isDocumentDirty() );

    panel.setOpen( true ); // 닫기 확인의 "저장 안 함" 도 같은 길이다
    panel.edit();
    panel.requestClose();
    panel.applyCloseChoice( EditorUnsavedChoice::Discard );
    SW_EXPECT_EQUAL( 2, panel.getLoadCount() );
    SW_EXPECT_FALSE( panel.isOpen() );
}
