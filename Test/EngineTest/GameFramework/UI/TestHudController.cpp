#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/SliderWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Base/UI/Hud/HudControllerComponent.h"

#include "TestFramework/TestFramework.h"

// HudControllerTest — 오브젝트가 플레이하는 동안 HUD 문서를 Hud 층에 열고 HUD 뷰모델을 거는 컴포넌트. 문서는 메모리 문서로 넣는다. 디바이스 없음(nogpu).

/**
 * @brief [HudControllerTest] HUD 문서는 Hud 층 화면으로 열리고(포커스 · 게임 입력을 가져가지 않는다) 이름으로 위젯을 찾으며, UI 시스템을 풀면 닫힌다
 * @details `findWidget<T>( 이름 )` 은 바인딩으로 닿지 않는 일에 쓴다. 변이: `bindUiSystem` 이 옛 화면을 닫지 않으면 마지막 단언(화면 0)이 진다.
 */
SW_TEST_CASE( HudControllerTest, OpensTheHudDocumentOnTheHudLayer )
{
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::UiSystem ui;
    SW_ASSERT_TRUE( ui.initialize( input, nullptr ) );
    ui.getDocumentCache().registerMemoryDocument( "test/hud.ui.xml", "<UiDocument _schemaVersion=\"1\">\n"
                                                                     "\t<UiScreenDesc _layer=\"Hud\" _bTakesFocus=\"false\" _bShowCursor=\"false\" />\n"
                                                                     "\t<CanvasPanel>\n"
                                                                     "\t\t<TextWidget _name=\"Ammo\" />\n"
                                                                     "\t</CanvasPanel>\n"
                                                                     "</UiDocument>\n" );
    {
        sw::GameObjectManager manager;
        sw::GameObject*       pPlayer = manager.createGameObject( sw::hashed_string( "Player" ) );
        SW_ASSERT_NOT_NULL( pPlayer );
        sw::HudControllerComponent* pHud = pPlayer->addComponent<sw::HudControllerComponent>();
        SW_ASSERT_NOT_NULL( pHud );
        pHud->setDocumentPath( "test/hud.ui.xml" );
        pHud->bindUiSystem( &ui );

        const sw::UiScreen* pScreen = pHud->getScreen();
        SW_ASSERT_NOT_NULL( pScreen );
        SW_EXPECT_TRUE( pScreen->getDesc()._layer == sw::UiLayer::Hud );
        SW_EXPECT_TRUE( ui.getActiveScreen() == nullptr ); // HUD 는 포커스를 받지 않는다
        SW_EXPECT_FALSE( ui.isGameInputBlocked() );
        sw::TextWidget* pAmmo = pHud->findWidget<sw::TextWidget>( sw::hashed_string( "Ammo" ) );
        SW_ASSERT_NOT_NULL( pAmmo );
        pAmmo->setText( "30 / 90" );
        SW_EXPECT_TRUE( pHud->findWidget<sw::TextWidget>( sw::hashed_string( "Missing" ) ) == nullptr );

        pHud->bindUiSystem( nullptr );
        SW_EXPECT_TRUE( pHud->getScreen() == nullptr );
        ui.update( 1.0f / 60.0f, sw::UiViewport{
                                     sw::float2{ 1920.0f, 1080.0f }
        } );
        SW_EXPECT_EQUAL( 0u, ui.getScreenCount() );
    }
    ui.shutdown();
    input.shutdown();
}

/**
 * @brief [HudControllerTest] 게임이 HUD 뷰모델 세터만 부르면 문서의 `{bind:필드}` 가 위젯 칸에 잇는다 — 체력 글 · 막대 · 탄약 둘 · 무기 이름(현지화 키 그대로) · 조준선 보임,
 *        화면을 닫았다 다시 열어도 마지막 값이 바로 보인다
 * @details 변이: `HudControllerComponent::openScreen` 이 `setViewModel` 을 부르지 않으면 글이 비어 첫 단언부터 진다.
 */
SW_TEST_CASE( HudControllerTest, ViewModelDrivesTheHudDocument )
{
    struct HudTestUtil
    {
        static void runFrame( sw::InputManager& input, sw::UiSystem& ui )
        {
            constexpr float32 kFrameSeconds = 1.0f / 60.0f;
            input.beginFrame( kFrameSeconds );
            ui.processInput( kFrameSeconds );
            ui.update( kFrameSeconds, sw::UiViewport{
                                          sw::float2{ 1920.0f, 1080.0f }
            } );
        }
    };
    sw::InputManager input;
    SW_ASSERT_TRUE( input.initialize() );
    sw::UiSystem ui;
    SW_ASSERT_TRUE( ui.initialize( input, nullptr ) );
    ui.getDocumentCache().registerMemoryDocument( "test/boundhud.ui.xml",
                                                  "<UiDocument _schemaVersion=\"1\">\n"
                                                  "\t<UiScreenDesc _layer=\"Hud\" _bTakesFocus=\"false\" _bShowCursor=\"false\" />\n"
                                                  "\t<CanvasPanel>\n"
                                                  "\t\t<ImageWidget _name=\"Crosshair\" _visibility=\"{bind:_crosshairVisibility}\" />\n"
                                                  "\t\t<TextWidget _name=\"Health\" _text=\"{bind:_health}\" _bLocalized=\"false\" />\n"
                                                  "\t\t<ProgressBarWidget _name=\"HealthBar\" _percent=\"{bind:_healthRatio}\" />\n"
                                                  "\t\t<TextWidget _name=\"Magazine\" _text=\"{bind:_magazineAmmo}\" _bLocalized=\"false\" />\n"
                                                  "\t\t<TextWidget _name=\"Reserve\" _text=\"{bind:_reserveAmmo}\" _bLocalized=\"false\" />\n"
                                                  "\t\t<TextWidget _name=\"Weapon\" _text=\"{bind:_weaponName}\" />\n"
                                                  "\t</CanvasPanel>\n"
                                                  "</UiDocument>\n" );
    {
        sw::GameObjectManager manager;
        sw::GameObject*       pPlayer = manager.createGameObject( sw::hashed_string( "Player" ) );
        SW_ASSERT_NOT_NULL( pPlayer );
        sw::HudControllerComponent* pHud = pPlayer->addComponent<sw::HudControllerComponent>();
        SW_ASSERT_NOT_NULL( pHud );
        pHud->setDocumentPath( "test/boundhud.ui.xml" );
        pHud->bindUiSystem( &ui );

        sw::HudViewModel& hud = pHud->getViewModel();
        hud.setHealth( 72.4f, 0.724f );
        hud.setAmmo( 12, 90 );
        hud.setWeaponName( "Rifle" );
        hud.setCrosshairShown( true );
        HudTestUtil::runFrame( input, ui );

        const auto findText = [pHud]( const utf8* pName ) -> sw::string
        {
            const sw::TextWidget* pText = pHud->findWidget<sw::TextWidget>( sw::hashed_string( pName ) );
            return pText != nullptr ? pText->getText() : sw::string( "<missing>" );
        };
        SW_EXPECT_STREQ( "73", findText( "Health" ).c_str() ); // 올림
        SW_EXPECT_STREQ( "12", findText( "Magazine" ).c_str() );
        SW_EXPECT_STREQ( "90", findText( "Reserve" ).c_str() );
        SW_EXPECT_STREQ( "Rifle", findText( "Weapon" ).c_str() ); // 키 그대로 — 글 위젯이 문화권으로 푼다
        const sw::ProgressBarWidget* pBar = pHud->findWidget<sw::ProgressBarWidget>( sw::hashed_string( "HealthBar" ) );
        SW_ASSERT_NOT_NULL( pBar );
        SW_EXPECT_NEAR_EQUAL( 0.724f, pBar->getPercent(), 1e-4f );
        const sw::Widget* pCrosshair = pHud->findWidget( sw::hashed_string( "Crosshair" ) );
        SW_ASSERT_NOT_NULL( pCrosshair );
        SW_EXPECT_TRUE( pCrosshair->getVisibility() == sw::WidgetVisibility::HitTestInvisible );

        hud.setCrosshairShown( false );
        hud.setAmmo( 11, 90 );
        HudTestUtil::runFrame( input, ui );
        SW_EXPECT_TRUE( pHud->findWidget( sw::hashed_string( "Crosshair" ) )->getVisibility() == sw::WidgetVisibility::Collapsed );
        SW_EXPECT_STREQ( "11", findText( "Magazine" ).c_str() );

        // 닫았다 다시 열면 새 화면이 지금 값을 바로 받는다(뷰모델이 화면보다 오래 산다).
        pHud->bindUiSystem( nullptr );
        HudTestUtil::runFrame( input, ui );
        SW_EXPECT_EQUAL( 0u, ui.getScreenCount() );
        pHud->bindUiSystem( &ui );
        HudTestUtil::runFrame( input, ui );
        SW_EXPECT_STREQ( "11", findText( "Magazine" ).c_str() );
        SW_EXPECT_STREQ( "73", findText( "Health" ).c_str() );
        pHud->bindUiSystem( nullptr );
    }
    ui.shutdown();
    input.shutdown();
}
