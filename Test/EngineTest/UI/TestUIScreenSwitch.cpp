#include "pch.h"

#include "Core/String/RegistrationList.h"

#include "Engine/Input/InputManager.h"
#include "Engine/UI/Layout/CanvasPanel.h"
#include "Engine/UI/Screen/UIScreen.h"
#include "Engine/UI/Screen/UIScreenSwitch.h"
#include "Engine/UI/UISystem.h"

#include "TestFramework/TestFramework.h"

// UIScreenSwitchTest — 화면 스위치 등록(견본 · 벤치 · 옵션 메뉴가 스스로 등록): 중복 거절 · 리로드 대체 · (순서, 이름) 순 · UISystem::update 가 연 화면을 이어 준다. 디바이스 없음(nogpu).

namespace
{
    struct UIScreenSwitchTestUtil
    {
        static constexpr float32 kFrameSeconds = 1.0f / 60.0f;

        static sw::UIScreenHandle returnCurrent( sw::UISystem& uiSystem, sw::UIScreenHandle current )
        {
            (void)uiSystem;
            return current;
        }

        static sw::UIScreenHandle returnInvalid( sw::UISystem& uiSystem, sw::UIScreenHandle current )
        {
            (void)uiSystem;
            (void)current;
            return sw::kInvalidUIScreenHandle;
        }
    };

    /** @brief 테스트 스위치가 열지 여부와, 불릴 때 받은 화면 · 불린 횟수입니다. */
    bool               s_bTestSwitchOn       = false;
    sw::UIScreenHandle s_lastReceivedScreen  = sw::kInvalidUIScreenHandle;
    uint32             s_testSwitchCallCount = 0;

    /** @brief `s_bTestSwitchOn` 을 따라 빈 HUD 화면을 열고 닫는 테스트 스위치입니다. */
    sw::UIScreenHandle syncTestSwitch( sw::UISystem& uiSystem, sw::UIScreenHandle current )
    {
        ++s_testSwitchCallCount;
        s_lastReceivedScreen = current;
        const bool bOpen     = current != sw::kInvalidUIScreenHandle && uiSystem.findScreen( current ) != nullptr;
        if ( s_bTestSwitchOn == bOpen )
            return current;
        if ( bOpen )
        {
            uiSystem.closeScreen( current );
            return sw::kInvalidUIScreenHandle;
        }
        sw::UIScreenDesc desc{};
        desc._layer = sw::UILayer::HUD;
        return uiSystem.pushScreen( sw::make_unique<sw::UIScreen>( desc, sw::make_unique<sw::CanvasPanel>() ) );
    }

    void runFrame( sw::InputManager& input, sw::UISystem& ui )
    {
        input.beginFrame( UIScreenSwitchTestUtil::kFrameSeconds );
        ui.processInput( UIScreenSwitchTestUtil::kFrameSeconds );
        ui.update( UIScreenSwitchTestUtil::kFrameSeconds, sw::UIViewport{
                                                              sw::float2{ 1920.0f, 1080.0f }
        } );
        input.endFrame();
    }
} // namespace

/**
 * @brief [UIScreenSwitchTest] 엔진 화면(견본 · 벤치 · 옵션 메뉴)은 UISystem 이 아니라 자기 .cpp 에서 스스로 등록하고, 목록은 (순서, 이름) 순이다
 */
SW_TEST_CASE( UIScreenSwitchTest, EngineScreensRegisterThemselvesInOrder )
{
    const sw::UIScreenSwitchRegistration* pDemo    = sw::UIScreenSwitches::find( "UIDemo" );
    const sw::UIScreenSwitchRegistration* pBench   = sw::UIScreenSwitches::find( "UIBench" );
    const sw::UIScreenSwitchRegistration* pOptions = sw::UIScreenSwitches::find( "OptionsMenu" );
    SW_ASSERT_NOT_NULL( pDemo );
    SW_ASSERT_NOT_NULL( pBench );
    SW_ASSERT_NOT_NULL( pOptions );

    // 순서 값이 앞서면 이름과 등록 시점에 상관없이 먼저 돈다. 같은 순서 값이면 이름순이다.
    const sw::UIScreenSwitchRegistration first{ "ZFirst", -100, &UIScreenSwitchTestUtil::returnCurrent };
    const sw::UIScreenSwitchRegistration sameOrderB{ "B", -50, &UIScreenSwitchTestUtil::returnCurrent };
    const sw::UIScreenSwitchRegistration sameOrderA{ "A", -50, &UIScreenSwitchTestUtil::returnCurrent };
    const sw::UIScreenSwitchRegistrar    firstRegistrar{ &first };
    const sw::UIScreenSwitchRegistrar    sameOrderBRegistrar{ &sameOrderB };
    const sw::UIScreenSwitchRegistrar    sameOrderARegistrar{ &sameOrderA };
    SW_ASSERT_TRUE( sw::UIScreenSwitches::getCount() >= 6u );
    SW_EXPECT_TRUE( sw::UIScreenSwitches::getAt( 0 ) == &first );
    SW_EXPECT_TRUE( sw::UIScreenSwitches::getAt( 1 ) == &sameOrderA );
    SW_EXPECT_TRUE( sw::UIScreenSwitches::getAt( 2 ) == &sameOrderB );

    uint32 demoIndex    = 0;
    uint32 benchIndex   = 0;
    uint32 optionsIndex = 0;
    for ( uint32 index = 0; index < sw::UIScreenSwitches::getCount(); ++index )
    {
        if ( sw::UIScreenSwitches::getAt( index ) == pDemo )
            demoIndex = index;
        if ( sw::UIScreenSwitches::getAt( index ) == pBench )
            benchIndex = index;
        if ( sw::UIScreenSwitches::getAt( index ) == pOptions )
            optionsIndex = index;
    }
    SW_EXPECT_TRUE( demoIndex < benchIndex && benchIndex < optionsIndex );
}

/**
 * @brief [UIScreenSwitchTest] 같은 이름은 거절하고(먼저 것을 둔다), 핫 리로드의 교체 이미지는 같은 이름을 대체하며 옛 이미지가 내려가도 새 등록이 남는다
 */
SW_TEST_CASE( UIScreenSwitchTest, DuplicateIsRejectedAndReloadImageReplaces )
{
    SW_TEST_DEFENSIVE_SCOPE( "duplicate UI screen switch registration is reported" );
    const sw::UIScreenSwitchRegistration oldImage{ "TestSwitch", 0, &UIScreenSwitchTestUtil::returnCurrent };
    const sw::UIScreenSwitchRegistration newImage{ "TestSwitch", 0, &UIScreenSwitchTestUtil::returnInvalid };
    const sw::UIScreenSwitchRegistration stranger{ "TestSwitch", 0, &UIScreenSwitchTestUtil::returnInvalid };

    SW_EXPECT_TRUE( sw::UIScreenSwitches::registerSwitch( &oldImage ) );
    SW_EXPECT_FALSE( sw::UIScreenSwitches::registerSwitch( &stranger ) ); // 리로드 밖의 같은 이름은 거절한다(오류 로그)
    SW_EXPECT_TRUE( sw::UIScreenSwitches::find( "TestSwitch" ) == &oldImage );
    {
        sw::RegistrationReloadScope scope;
        SW_EXPECT_TRUE( sw::UIScreenSwitches::registerSwitch( &newImage ) );
    }
    SW_EXPECT_TRUE( sw::UIScreenSwitches::find( "TestSwitch" ) == &newImage );

    sw::UIScreenSwitches::unregisterSwitch( &oldImage ); // 옛 이미지가 내려간다
    SW_EXPECT_TRUE_MSG( sw::UIScreenSwitches::find( "TestSwitch" ) == &newImage, "the reloaded screen switch is lost when the old image unloads" );

    sw::UIScreenSwitches::unregisterSwitch( &newImage );
    SW_EXPECT_TRUE( sw::UIScreenSwitches::find( "TestSwitch" ) == nullptr );
}

/**
 * @brief [UIScreenSwitchTest] UISystem::update 가 등록된 스위치를 돌리고, 스위치가 연 화면을 다음 프레임에 다시 건넨다(끄면 닫고, 내리면 더 부르지 않는다)
 */
SW_TEST_CASE( UIScreenSwitchTest, UpdateDrivesRegisteredSwitchAndKeepsItsScreen )
{
    sw::InputManager input{};
    sw::UISystem     ui{};
    SW_ASSERT_TRUE( input.initialize() );
    SW_ASSERT_TRUE( ui.initialize( input, nullptr ) );
    s_bTestSwitchOn       = false;
    s_lastReceivedScreen  = sw::kInvalidUIScreenHandle;
    s_testSwitchCallCount = 0;
    {
        const sw::UIScreenSwitchRegistration registration{ "TestScreenSwitch", 0, &syncTestSwitch };
        const sw::UIScreenSwitchRegistrar    registrar{ &registration };

        runFrame( input, ui );
        SW_EXPECT_EQUAL( 1u, s_testSwitchCallCount );
        SW_EXPECT_EQUAL( 0u, ui.getScreenCount() );

        s_bTestSwitchOn = true;
        runFrame( input, ui );
        SW_EXPECT_EQUAL( 1u, ui.getScreenCount() );
        runFrame( input, ui );
        const sw::UIScreenHandle opened = s_lastReceivedScreen;
        SW_EXPECT_TRUE( opened != sw::kInvalidUIScreenHandle );
        SW_EXPECT_TRUE( ui.findScreen( opened ) != nullptr );
        SW_EXPECT_EQUAL( 1u, ui.getScreenCount() ); // 열린 화면을 이어 받아 다시 열지 않는다

        s_bTestSwitchOn = false;
        runFrame( input, ui );
        runFrame( input, ui );
        SW_EXPECT_EQUAL( 0u, ui.getScreenCount() );
        SW_EXPECT_TRUE( s_lastReceivedScreen == sw::kInvalidUIScreenHandle );
    }
    const uint32 callCountAfterUnload = s_testSwitchCallCount;
    runFrame( input, ui );
    SW_EXPECT_EQUAL( callCountAfterUnload, s_testSwitchCallCount );
    ui.shutdown();
    input.shutdown();
}
