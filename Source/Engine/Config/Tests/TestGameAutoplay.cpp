// GameAutoplay — 게임이 한 줄로 등록하는 자동 플레이 스위치: 툴바 · 콘솔의 스위치가 게임의 전역 변수를 표를 거쳐 바꾸고(변경 콜백), 내리면 등록이 사라지며,
// 핫 리로드의 교체 이미지는 같은 이름을 대체한다(nogpu). 지금 실행에 게임 모듈이 올라 있을 수 있으므로 "등록 전에 쓰던 것" 을 기준으로 본다.
#include "pch.h"

#include "Core/Foundation/DynamicModules.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/String/RegistrationList.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Config/GameAutoplay.h"
#include "Engine/Foundation/EngineServices.h"

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
    const GameAutoplayRegistration* pBefore = GameAutoplay::findActive(); // 올라 있는 게임의 등록(없으면 nullptr)

    GlobalVariableManager& variables = engine::getSubsystem<GlobalVariableManager>();
    SW_REQUIRE_TRUE( variables.registerVariable( "gv_fakeGameAutoPlay", GlobalVariableType::Int32, &s_fakeGameAutoPlay, int32{ 0 }, "fake game autoplay", "", "GameAutoplayTest" ) );
    GlobalVariableInfo* pInfo = variables.findVariable( "gv_fakeGameAutoPlay" );
    SW_EXPECT_NOT_NULL( pInfo );
    if ( pInfo != nullptr )
    {
        pInfo->_onValueChanged = GlobalVariableChangedDelegate::create<&onFakeAutoPlayChanged>();
        s_autoPlayChangedCount = 0;
        {
            const GameAutoplayRegistration registration{ "FakeGame", "AI plays the fake game", "gv_fakeGameAutoPlay", &s_fakeGameAutoPlay };
            const GameAutoplayRegistrar    registrar{ &registration };
            SW_EXPECT_TRUE( GameAutoplay::findActive() == &registration ); // 나중에 오른 등록이 쓰인다
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
        SW_EXPECT_TRUE_MSG( GameAutoplay::findActive() == pBefore, "the unloaded game's autoplay is still registered" );
    }
    variables.unregisterVariablesByModule( "GameAutoplayTest" );
    s_fakeGameAutoPlay = 0;
}

/**
 * @brief [GameAutoplayTest] 핫 리로드의 교체 이미지가 같은 이름(전역 변수 이름)을 다시 올려도, 옛 이미지가 내려간 뒤 새 등록이 남는다
 * @details 교체 이미지의 정적 등록자는 옛 이미지가 아직 올라 있는 동안 돈다(`RegistrationReloadScope`). 리로드 밖의 같은 이름은 거절한다.
 */
SW_TEST_CASE( GameAutoplayTest, ReloadImageReplacesTheSameNameAndSurvivesTheOldImageUnload )
{
    const GameAutoplayRegistration* pBefore = GameAutoplay::findActive();
    const GameAutoplayRegistration  oldImage{ "FakeGame", "old image", "gv_fakeGameAutoPlay", &s_fakeGameAutoPlay };
    const GameAutoplayRegistration  newImage{ "FakeGame", "new image", "gv_fakeGameAutoPlay", &s_fakeGameAutoPlay };
    const GameAutoplayRegistration  stranger{ "FakeGame", "stranger", "gv_fakeGameAutoPlay", &s_fakeGameAutoPlay };

    GameAutoplay::registerAutoplay( &oldImage );
    SW_EXPECT_LOG( "Registration.DuplicateName" );
    GameAutoplay::registerAutoplay( &stranger ); // 리로드 밖의 같은 이름은 거절한다(오류 로그)
    SW_EXPECT_TRUE( GameAutoplay::findActive() == &oldImage );
#if SW_WITH_DYNAMIC_MODULES
    {
        RegistrationReloadScope scope;
        GameAutoplay::registerAutoplay( &newImage );
    }
    SW_EXPECT_TRUE( GameAutoplay::findActive() == &newImage );

    GameAutoplay::unregisterAutoplay( &oldImage ); // 옛 이미지가 내려간다
    SW_EXPECT_TRUE_MSG( GameAutoplay::findActive() == &newImage, "the reloaded game's autoplay is lost when the old image unloads" );
#endif
    GameAutoplay::unregisterAutoplay( &newImage );
    GameAutoplay::unregisterAutoplay( &oldImage );
    SW_EXPECT_TRUE( GameAutoplay::findActive() == pBefore );
}
