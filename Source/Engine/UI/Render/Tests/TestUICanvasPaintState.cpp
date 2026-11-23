// UICanvasPaintStateTest — 화면 UI 그리기 목록의 공유 출력(내용이 바뀔 때만 새 목록 · 번호), 레이어 숨김 요청 수, 지난 목록 재사용 판정(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Render/UICanvasPaintState.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"

using namespace sw;

namespace
{
    struct UICanvasPaintStateTestUtil
    {
        static constexpr float2 kSize{ 1280.0f, 720.0f };

        /** @brief 빨강 값이 @p red 인 사각형 하나를 칠합니다. */
        static void paintQuad( CanvasDrawList& canvas, float32 red )
        {
            CanvasQuad quad{};
            quad._color._x = red;
            canvas._listQuad.push_back( quad );
        }
    };
    using Util = UICanvasPaintStateTestUtil;
} // namespace

/** @brief [UICanvasPaintStateTest] 같은 내용을 다시 칠하면 낸 목록 · 번호가 그대로고, 내용이나 대상 크기가 바뀌면 새 목록이다. 밖에서 쥔 지난 목록은 바뀌지 않는다 */
SW_TEST_CASE( UICanvasPaintStateTest, OutputChangesOnlyWithContentOrSize )
{
    UICanvasOutput output;
    Util::paintQuad( output.beginPaint( Util::kSize ), 0.25f );
    output.endPaint();
    const uint64                           firstRevision = output.getRevision();
    const shared_ptr<const CanvasDrawList> first         = output.getSharedCanvas();

    Util::paintQuad( output.beginPaint( Util::kSize ), 0.25f );
    output.endPaint();
    SW_EXPECT_EQUAL( firstRevision, output.getRevision() );
    SW_EXPECT_TRUE( output.getSharedCanvas() == first );

    Util::paintQuad( output.beginPaint( Util::kSize ), 0.5f );
    output.endPaint();
    SW_EXPECT_EQUAL( firstRevision + 1, output.getRevision() );
    SW_REQUIRE_EQUAL( size_t{ 1 }, first->_listQuad.size() );
    SW_EXPECT_NEAR( 0.25f, first->_listQuad[0]._color._x, 1.0e-6f ); // 렌더 스레드가 쥔 목록에 제자리로 쓰지 않는다

    const float2 resized{ 1920.0f, 1080.0f };
    Util::paintQuad( output.beginPaint( resized ), 0.5f );
    output.endPaint();
    SW_EXPECT_EQUAL( firstRevision + 2, output.getRevision() );
    SW_EXPECT_TRUE( output.getCanvas()._targetSize == resized );
}

/** @brief [UICanvasPaintStateTest] 숨김은 요청 수다 — 겹친 요청은 모두 풀려야 보이고, 다른 레이어는 그대로다 */
SW_TEST_CASE( UICanvasPaintStateTest, LayerHideRequestsCount )
{
    UICanvasPaintState state;
    const UILayerMask  both = EnumUtil::setFlag( UILayerMask::HUD, UILayerMask::Overlay );
    state.addLayerHideRequest( both );
    state.addLayerHideRequest( UILayerMask::HUD );
    SW_EXPECT_TRUE( state.isLayerHidden( UILayer::HUD ) && state.isLayerHidden( UILayer::Overlay ) );
    SW_EXPECT_FALSE( state.isLayerHidden( UILayer::Menu ) );
    state.removeLayerHideRequest( both );
    SW_EXPECT_TRUE( state.isLayerHidden( UILayer::HUD ) );
    SW_EXPECT_FALSE( state.isLayerHidden( UILayer::Overlay ) );
    state.removeLayerHideRequest( UILayerMask::HUD );
    SW_EXPECT_TRUE( state.getHiddenLayers() == UILayerMask::None );
}

/** @brief [UICanvasPaintStateTest] 지난 목록은 재사용할 수 있는 프레임에, 화면 수 · 대상 크기 · 숨긴 레이어가 그대로일 때만 다시 쓴다. 창 크기가 바뀐 뒤 같은 내용을 칠해도 다음 프레임은 다시 쓴다 */
SW_TEST_CASE( UICanvasPaintStateTest, ReuseNeedsTheSameFrame )
{
    UICanvasPaintState state;
    WidgetTree         tree;
    SW_EXPECT_FALSE( state.canReuseLastCanvas( true, 0, float2{} ) ); // 칠한 적이 없다
    const auto paintFrame = [&state, &tree]( const float2& size )
    {
        Util::paintQuad( state.beginPaint( true, size ), 0.25f );
        state.addPaintedTree( tree );
        state.endPaint( 0 );
    };
    paintFrame( Util::kSize );
    SW_EXPECT_TRUE( state.canReuseLastCanvas( true, 1, Util::kSize ) );
    SW_EXPECT_TRUE( state.isPaintedTree( 0, tree ) );
    SW_EXPECT_FALSE( state.canReuseLastCanvas( false, 1, Util::kSize ) );
    SW_EXPECT_FALSE( state.canReuseLastCanvas( true, 2, Util::kSize ) );

    const float2 resized{ 1920.0f, 1080.0f };
    SW_EXPECT_FALSE( state.canReuseLastCanvas( true, 1, resized ) );
    paintFrame( resized );
    SW_EXPECT_TRUE( state.canReuseLastCanvas( true, 1, resized ) );

    state.addLayerHideRequest( UILayerMask::HUD );
    SW_EXPECT_FALSE( state.canReuseLastCanvas( true, 1, resized ) );
    paintFrame( resized );
    SW_EXPECT_TRUE( state.canReuseLastCanvas( true, 1, resized ) );
    state.endPaint( 3 );
    SW_EXPECT_EQUAL( 3u, state.getDeferredGlyphCount() );
    state.markLastCanvasReused();
    SW_EXPECT_EQUAL( 0u, state.getDeferredGlyphCount() );
}
