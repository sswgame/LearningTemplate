#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Utility/GameAutoplay.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 가짜 게임의 자동 플레이 스위치(게임 모듈의 `gv_<게임>AutoPlay` 자리)입니다. */
    int32 s_fakeGameAutoPlay = 0;

    /** @brief 변경 콜백이 불린 횟수입니다. */
    int32 s_autoPlayChangedCount = 0;

    void onFakeAutoPlayChanged( GlobalVariableInfo* )
    {
        ++s_autoPlayChangedCount;
    }
} // namespace

/**
 * @brief [GameAutoplayTest] 가짜 게임이 한 줄로 등록하면 툴바 · 콘솔이 쓰는 스위치가 그 게임의 전역 변수를 켜고, 내리면 등록이 사라진다
 * @details 켜짐은 게임의 전역 변수 하나에 있다 — 명령줄(`-gv_<게임>AutoPlay=1`)과 툴바가 같은 값을 바꾼다. 쓰기는 전역 변수 표를 거쳐 변경 콜백이
 *          불린다(패널 · 다른 구독자가 같이 따른다). 게임 모듈을 내리면 등록자가 사라지며 스위치도 사라진다.
 */
SW_TEST_CASE( GameAutoplayTest, FakeGameRegistersAndTheSwitchFlipsItsVariable )
{
    SW_EXPECT_TRUE( GameAutoplay::findActive() == nullptr ); // 엔진 시험에는 게임이 없다
    SW_EXPECT_FALSE( GameAutoplay::setOn( true ) );

    GlobalVariableManager& variables = engine::getGlobalVariableManager();
    SW_ASSERT_TRUE( variables.registerVariable( "gv_fakeGameAutoPlay", GlobalVariableType::Int32, &s_fakeGameAutoPlay, int32{ 0 }, "fake game autoplay", "", "GameAutoplayTest" ) );
    GlobalVariableInfo* pInfo = variables.findVariable( "gv_fakeGameAutoPlay" );
    SW_ASSERT_NOT_NULL( pInfo );
    pInfo->_onValueChanged = GlobalVariableChangedDelegate::create<&onFakeAutoPlayChanged>();
    s_autoPlayChangedCount = 0;
    {
        const GameAutoplayRegistration registration{ "FakeGame", "AI plays the fake game", "gv_fakeGameAutoPlay", &s_fakeGameAutoPlay };
        const GameAutoplayRegistrar    registrar{ &registration };
        SW_ASSERT_TRUE( GameAutoplay::findActive() == &registration );
        SW_EXPECT_FALSE( GameAutoplay::isOn() );

        // 명령줄 · 콘솔이 전역 변수를 켜면 스위치가 켜진 것이다.
        s_fakeGameAutoPlay = 1;
        SW_EXPECT_TRUE( GameAutoplay::isOn() );

        // 툴바 · 콘솔의 스위치는 전역 변수 표를 거쳐 쓴다 — 콜백이 불린다.
        SW_EXPECT_TRUE( GameAutoplay::setOn( false ) );
        SW_EXPECT_EQUAL( 0, s_fakeGameAutoPlay );
        SW_EXPECT_EQUAL( 1, s_autoPlayChangedCount );
        SW_EXPECT_TRUE( GameAutoplay::setOn( true ) );
        SW_EXPECT_EQUAL( 1, s_fakeGameAutoPlay );
    }
    SW_EXPECT_TRUE_MSG( GameAutoplay::findActive() == nullptr, "the unloaded game's autoplay is still registered" );
    SW_EXPECT_FALSE( GameAutoplay::isOn() );
    variables.unregisterVariablesByModule( "GameAutoplayTest" );
    s_fakeGameAutoPlay = 0;
}
