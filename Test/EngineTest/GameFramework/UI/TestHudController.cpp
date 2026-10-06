#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/UI/Screen/UiScreen.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Base/UI/HudControllerComponent.h"

#include "TestFramework/TestFramework.h"

// HudControllerTest — 오브젝트가 플레이하는 동안 HUD 문서를 Hud 층에 여는 컴포넌트. 문서는 메모리 문서로 넣는다. 디바이스 없음(nogpu).

/**
 * @brief [HudControllerTest] HUD 문서는 Hud 층 화면으로 열리고(포커스 · 게임 입력을 가져가지 않는다) 이름으로 위젯을 찾으며, UI 시스템을 풀면 닫힌다
 * @details 게임은 `findWidget<T>( 이름 )` 으로 값을 넣는다(값 바인딩 전의 길). 변이: `bindUiSystem` 이 옛 화면을 닫지 않으면 마지막 단언(화면 0)이 진다.
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
