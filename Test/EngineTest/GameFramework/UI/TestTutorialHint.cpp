#include "pch.h"

#include "Engine/Input/InputManager.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/UI/Screen/UiNotificationService.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/UI/Hud/TutorialHintComponent.h"

#include "TestFramework/TestFramework.h"

// TutorialHintTest — 튜토리얼 힌트 볼륨(runtime-ui 8-3): 태그가 맞는 활성자에게 한 번, 종류 Hint 알림으로. 설정 gameplay.showTutorials 가 꺼져 있으면 띄우지 않는다.

using namespace sw;

/** @brief [TutorialHintTest] 태그가 맞는 활성자에게 한 번 알림을 올리고, 튜토리얼을 끈 플레이어에게는 올리지 않는다(발동한 것으로 치지 않는다) */
SW_TEST_CASE( TutorialHintTest, PostsOnceAndHonorsTheSetting )
{
    InputManager input;
    UiSystem     ui;
    SW_ASSERT_TRUE( input.initialize() );
    SW_ASSERT_TRUE( ui.initialize( input, nullptr ) );
    UserSettingsManager settings;
    settings.initialize( UserSettingsTargets{} );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText(
        R"(<UserSettingsSchema version="1"><Category id="gameplay"/><Setting id="gameplay.showTutorials" category="gameplay" type="bool" default="false"/></UserSettingsSchema>)",
        "hint.settings.xml" ) );
    game::bindLocalService<UserSettingsManager>( &settings );

    GameObjectManager objects;
    GameObject*       pVolume = objects.createGameObject( hashed_string( "HintVolume" ) );
    GameObject*       pPlayer = objects.createGameObject( hashed_string( "Player" ) );
    GameObject*       pCrate  = objects.createGameObject( hashed_string( "Crate" ) );
    SW_ASSERT_NOT_NULL( pVolume );
    TutorialHintComponent* pHint = pVolume->addComponent<TutorialHintComponent>();
    SW_ASSERT_NOT_NULL( pHint );
    pHint->configure( "[action=Interact] to open", TagID::request( "Player" ) );
    pHint->setUiSystem( &ui );
    pPlayer->addTag( TagID::request( "Player" ) );
    objects.mergePendingAdds();

    SW_EXPECT_FALSE( pHint->activate( pPlayer ) ); // 튜토리얼을 껐다
    SW_EXPECT_FALSE( pHint->hasFired() );
    SW_EXPECT_TRUE( settings.setPendingBoolValue( "gameplay.showTutorials", true ) == UserSettingSetResult::Accepted );
    (void)settings.applyPending();                // 적용 결과는 아래 activate 단언이 확인한다
    SW_EXPECT_FALSE( pHint->activate( pCrate ) ); // 태그가 없다
    SW_EXPECT_TRUE( pHint->activate( pPlayer ) );
    SW_EXPECT_FALSE( pHint->activate( pPlayer ) ); // 한 번
    ui.update( 0.016f, UiViewport{
                           float2{ 1280.0f, 720.0f }
    } );
    SW_ASSERT_EQUAL( 1u, ui.getNotifications().getVisibleCount() );
    SW_EXPECT_STREQ( "[action=Interact] to open", ui.getNotifications().getVisibleText( 0 ).c_str() );

    game::unbindLocalService<UserSettingsManager>();
    ui.shutdown();
    input.shutdown();
    settings.shutdown();
}
