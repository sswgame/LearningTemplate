#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Appearance/CharacterWetnessComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/CharacterWetnessSystem.h"

using namespace sw;

// CharacterWetnessTest — 표면 노출의 프레임 일이 시스템 한 곳에서 도는지(nogpu, 물리 없음).

SW_TEST_CASE( CharacterWetnessTest, SystemRunsRegisteredExposureComponents )
{
    GameObjectManager manager;
    SW_EXPECT_TRUE( CharacterWetnessSystem::find( manager ) == nullptr );
    GameObject*                pBody     = manager.createGameObject( hashed_string( "Body" ) );
    CharacterWetnessComponent* pExposure = pBody->addComponent<CharacterWetnessComponent>();
    SW_REQUIRE_TRUE( pExposure != nullptr );
    CharacterWetnessSystem* pSystem = CharacterWetnessSystem::find( manager );
    SW_REQUIRE_TRUE( pSystem != nullptr ); // 첫 노출 컴포넌트가 등록될 때 붙는다
    pSystem->runFramePass( manager );      // 틱이 쌓은 시간이 없다 — 외형이 없어도 아무것도 하지 않고 지난다(편집 모드와 같은 길)
    SW_EXPECT_EQUAL( 0.0f, pExposure->getSubmersion() );
    SW_EXPECT_EQUAL( 0.0f, pExposure->getRainExposure() );
}
