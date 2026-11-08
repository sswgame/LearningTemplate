#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/Appearance/SurfaceExposureComponent.h"
#include "GameFramework/Runtime/Gameplay/Appearance/SurfaceExposureSystem.h"

using namespace sw;

// SurfaceExposureTest — 표면 노출의 프레임 일이 시스템 한 곳에서 도는지(nogpu, 물리 없음).

SW_TEST_CASE( SurfaceExposureTest, SystemRunsRegisteredExposureComponents )
{
    GameObjectManager manager;
    SW_EXPECT_TRUE( SurfaceExposureSystem::find( manager ) == nullptr );
    GameObject*               pBody     = manager.createGameObject( hashed_string( "Body" ) );
    SurfaceExposureComponent* pExposure = pBody->addComponent<SurfaceExposureComponent>();
    SW_REQUIRE_TRUE( pExposure != nullptr );
    SurfaceExposureSystem* pSystem = SurfaceExposureSystem::find( manager );
    SW_REQUIRE_TRUE( pSystem != nullptr ); // 첫 노출 컴포넌트가 등록될 때 붙는다
    pSystem->runFramePass( manager );      // 틱이 쌓은 시간이 없다 — 외형이 없어도 아무것도 하지 않고 지난다(편집 모드와 같은 길)
    SW_EXPECT_EQUAL( 0.0f, pExposure->getSubmersion() );
    SW_EXPECT_EQUAL( 0.0f, pExposure->getRainExposure() );
}
