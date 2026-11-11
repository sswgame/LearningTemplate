#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"

using namespace sw;

namespace
{
    /** @brief 답을 정해 둔 입력 포획 질의입니다. */
    struct GameInputViewTestUtil final : public IInputCaptureQuery
    {
        bool _bGameInputCaptured{ false }; // NOLINT(sw-bool-flag): 테스트 대역의 집합체다
        bool _bActionCaptured{ false };    // NOLINT(sw-bool-flag): 테스트 대역의 집합체다

        bool isGameInputCaptured() const override { return _bGameInputCaptured; }
        bool isActionCaptured( const InputMap& inputMap, const hashed_string& action ) const override
        {
            (void)inputMap;
            (void)action;
            return _bActionCaptured;
        }
        bool shouldShowCursor() const override { return false; }
    };
} // namespace

/**
 * @brief [GameInputViewTest] 질의가 없으면 거르지 않고, 액션을 먹었거나 게임 입력이 막혔으면 막는다
 */
SW_TEST_CASE( GameInputViewTest, BlocksOnlyWhatTheCaptureQueryReports )
{
    const InputMap      inputMap;
    const hashed_string action( "Jump" );
    SW_EXPECT_FALSE( GameInputView( inputMap, nullptr ).isActionBlocked( action ) );

    GameInputViewTestUtil capture;
    SW_EXPECT_FALSE( GameInputView( inputMap, &capture ).isActionBlocked( action ) );

    capture._bActionCaptured = true;
    SW_EXPECT_TRUE( GameInputView( inputMap, &capture ).isActionBlocked( action ) );

    capture._bActionCaptured    = false;
    capture._bGameInputCaptured = true;
    SW_EXPECT_TRUE( GameInputView( inputMap, &capture ).isActionBlocked( action ) );
}
