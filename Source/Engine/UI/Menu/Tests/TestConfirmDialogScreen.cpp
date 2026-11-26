// ConfirmDialogTest — 공통 확인 대화 상자의 답 규칙: 단추 · 뒤로 가기의 답, 답은 한 번뿐(중복 누름), 화면 스택 밖에서도 닫기 표시(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Menu/ConfirmDialogScreen.h"
#include "Engine/UI/WidgetTree/Tests/UITestWidgets.h"

using namespace sw;

namespace
{
    /** @brief 받은 답을 셉니다. */
    struct ConfirmDialogProbe
    {
        int32               _count{ 0 };
        ConfirmDialogResult _last{ ConfirmDialogResult::Cancelled };
    };

    /** @brief 시스템에 붙이지 않은 대화 상자 하나와 단추 자리 잎 하나입니다. */
    struct ConfirmDialogFixture
    {
        ConfirmDialogProbe  _probe;
        ConfirmDialogScreen _screen;
        Widget*             _pButton;

        ConfirmDialogFixture()
            : _probe{}
            , _screen{ UIScreenDef{}, make_unique<CanvasPanel>() }
            , _pButton{ nullptr }
        {
            _pButton                   = &UITestUtil::addLeaf( *castTo<PanelWidget>( _screen.getTree().getRoot() ), float2{ 10.0f, 10.0f } );
            ConfirmDialogProbe* pProbe = &_probe;
            ConfirmDialogDesc   desc{};
            desc._title      = "Title";
            desc._message    = "Message";
            desc._acceptText = "Yes";
            desc._cancelText = "No";
            _screen.present( desc, SW_DELEGATE_LAMBDA( ConfirmDialogScreen::ResultDelegate, [pProbe]( ConfirmDialogResult result )
            {
                ++pProbe->_count;
                pProbe->_last = result;
            } ) );
        }
    };
} // namespace

/** @brief [ConfirmDialogTest] Accept 는 확인, Cancel 은 취소로 답하고 창을 닫는다 */
SW_TEST_CASE( ConfirmDialogTest, ButtonsAnswerAndClose )
{
    ConfirmDialogFixture accept;
    SW_EXPECT_TRUE( accept._screen.onCommand( hashed_string( ConfirmDialogScreen::kAcceptCommand ), *accept._pButton ) );
    SW_EXPECT_EQUAL( 1, accept._probe._count );
    SW_EXPECT_TRUE( accept._probe._last == ConfirmDialogResult::Accepted );
    SW_EXPECT_TRUE( accept._screen.isClosing() );
    SW_EXPECT_TRUE( accept._screen.hasFinished() );

    ConfirmDialogFixture cancel;
    SW_EXPECT_TRUE( cancel._screen.onCommand( hashed_string( ConfirmDialogScreen::kCancelCommand ), *cancel._pButton ) );
    SW_EXPECT_EQUAL( 1, cancel._probe._count );
    SW_EXPECT_TRUE( cancel._probe._last == ConfirmDialogResult::Cancelled );
}

/** @brief [ConfirmDialogTest] 뒤로 가기는 취소다 */
SW_TEST_CASE( ConfirmDialogTest, BackCancels )
{
    ConfirmDialogFixture fixture;
    SW_EXPECT_TRUE( fixture._screen.onBack() );
    SW_EXPECT_EQUAL( 1, fixture._probe._count );
    SW_EXPECT_TRUE( fixture._probe._last == ConfirmDialogResult::Cancelled );
}

/** @brief [ConfirmDialogTest] 답은 한 번뿐이다 — 닫기 애니메이션 중 다시 누른 확인 · 뒤로 가기는 버린다(구매 확인의 중복 누름) */
SW_TEST_CASE( ConfirmDialogTest, AnswersOnlyOnce )
{
    ConfirmDialogFixture fixture;
    SW_EXPECT_TRUE( fixture._screen.onCommand( hashed_string( ConfirmDialogScreen::kAcceptCommand ), *fixture._pButton ) );
    SW_EXPECT_TRUE( fixture._screen.onCommand( hashed_string( ConfirmDialogScreen::kAcceptCommand ), *fixture._pButton ) );
    SW_EXPECT_TRUE( fixture._screen.onCommand( hashed_string( ConfirmDialogScreen::kCancelCommand ), *fixture._pButton ) );
    SW_EXPECT_TRUE( fixture._screen.onBack() );
    SW_EXPECT_EQUAL( 1, fixture._probe._count );
    SW_EXPECT_TRUE( fixture._probe._last == ConfirmDialogResult::Accepted );
}
