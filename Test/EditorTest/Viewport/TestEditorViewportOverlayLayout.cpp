#include "pch.h"

#include "Editor/Viewport/EditorViewportOverlayLayout.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TestEditorViewportOverlayLayoutInternal
    {
        static EditorOverlayBarState makeBar( const utf8* pID, EditorOverlayDock dock, float32 fractionX, float32 fractionY )
        {
            EditorOverlayBarState state{};
            state._id       = pID;
            state._dock     = dock;
            state._fraction = float2{ fractionX, fractionY };
            return state;
        }
    };
} // namespace

/**
 * @brief [EditorViewportOverlayLayoutTest] 자리는 남는 자리에 대한 비율이라 뷰포트가 줄어도 바가 뷰포트 안에 남는다
 */
SW_TEST_CASE( EditorViewportOverlayLayoutTest, PositionStaysInsideWhenTheViewShrinks )
{
    const EditorOverlayBarState bar   = TestEditorViewportOverlayLayoutInternal::makeBar( "tools", EditorOverlayDock::Bottom, 1.0f, 0.0f );
    const float2                large = EditorViewportOverlayLayout::computePosition( bar, float2{ 100.0f, 30.0f }, float2{ 800.0f, 600.0f } );
    SW_EXPECT_NEAR_EQUAL( 700.0f, large._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 570.0f, large._y, 0.01f ); // 아래에 붙었다 — y 비율은 1 로 고정
    const float2 shrunk = EditorViewportOverlayLayout::computePosition( bar, float2{ 100.0f, 30.0f }, float2{ 300.0f, 200.0f } );
    SW_EXPECT_NEAR_EQUAL( 200.0f, shrunk._x, 0.01f );
    SW_EXPECT_NEAR_EQUAL( 170.0f, shrunk._y, 0.01f );
    const float2 tooSmall = EditorViewportOverlayLayout::computePosition( bar, float2{ 100.0f, 30.0f }, float2{ 50.0f, 20.0f } );
    SW_EXPECT_NEAR_EQUAL( 0.0f, tooSmall._x, 0.01f );
}

/**
 * @brief [EditorViewportOverlayLayoutTest] 가장자리 가까이 놓으면 그 쪽에 붙고(왼쪽 · 오른쪽은 세로), 가운데에 놓으면 떠 있다
 */
SW_TEST_CASE( EditorViewportOverlayLayoutTest, DropSnapsToTheNearestEdge )
{
    EditorOverlayBarState bar = TestEditorViewportOverlayLayoutInternal::makeBar( "view", EditorOverlayDock::Top, 0.0f, 0.0f );
    const float2          barSize{ 100.0f, 30.0f };
    const float2          viewSize{ 800.0f, 600.0f };
    EditorViewportOverlayLayout::applyDrop( bar, float2{ 350.0f, 280.0f }, barSize, viewSize, 16.0f );
    SW_EXPECT_TRUE( bar._dock == EditorOverlayDock::Free );
    SW_EXPECT_NEAR_EQUAL( 0.5f, bar._fraction._x, 0.001f );
    EditorViewportOverlayLayout::applyDrop( bar, float2{ 5.0f, 280.0f }, barSize, viewSize, 16.0f );
    SW_EXPECT_TRUE( bar._dock == EditorOverlayDock::Left );
    SW_EXPECT_TRUE( EditorViewportOverlayLayout::isVertical( bar ) );
    EditorViewportOverlayLayout::applyDrop( bar, float2{ 690.0f, 280.0f }, barSize, viewSize, 16.0f );
    SW_EXPECT_TRUE( bar._dock == EditorOverlayDock::Right );
    EditorViewportOverlayLayout::applyDrop( bar, float2{ -50.0f, 900.0f }, barSize, viewSize, 16.0f ); // 밖에 놓아도 안으로 묶는다
    SW_EXPECT_TRUE( bar._dock == EditorOverlayDock::Bottom );
    SW_EXPECT_NEAR_EQUAL( 0.0f, bar._fraction._x, 0.001f );
}

/**
 * @brief [EditorViewportOverlayLayoutTest] 저장한 값을 다시 읽으면 같고, 등록 전에 읽어도 등록이 그 값을 덮지 않는다 · 되돌리기는 기본값으로
 */
SW_TEST_CASE( EditorViewportOverlayLayoutTest, SaveLoadAndReset )
{
    EditorViewportOverlayLayout saved;
    saved.registerBar( TestEditorViewportOverlayLayoutInternal::makeBar( "view", EditorOverlayDock::Top, 0.0f, 0.0f ) );
    EditorOverlayBarState* pView = saved.findBar( "view" );
    SW_ASSERT_NOT_NULL( pView );
    pView->_dock       = EditorOverlayDock::Left;
    pView->_fraction   = float2{ 0.0f, 0.25f };
    pView->_bCollapsed = true;
    pView->_bVisible   = false;
    KeyValueMap mapData;
    saved.writeTo( "scene", mapData );

    EditorViewportOverlayLayout loaded;
    loaded.readFrom( "scene", mapData );
    loaded.registerBar( TestEditorViewportOverlayLayoutInternal::makeBar( "view", EditorOverlayDock::Top, 0.0f, 0.0f ) );
    const EditorOverlayBarState* pLoaded = loaded.findBar( "view" );
    SW_ASSERT_NOT_NULL( pLoaded );
    SW_EXPECT_TRUE( pLoaded->_dock == EditorOverlayDock::Left );
    SW_EXPECT_NEAR_EQUAL( 0.25f, pLoaded->_fraction._y, 0.001f );
    SW_EXPECT_TRUE( pLoaded->_bCollapsed );
    SW_EXPECT_FALSE( pLoaded->_bVisible );

    loaded.resetToDefault();
    SW_EXPECT_TRUE( loaded.findBar( "view" )->_dock == EditorOverlayDock::Top );
    SW_EXPECT_TRUE( loaded.findBar( "view" )->_bVisible );
}
