// AbilityCueSinkTest — 클라이언트의 어빌리티 큐 창구는 서비스로 찾히고, 프리팹도 소리도 없는 큐는 오브젝트를 만들지 않으며, 흔들림을 적은 큐는 씬의 흔들림 채널로 낸다(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/CameraShakeChannel.h"
#include "Engine/Object/GameObject/Tests/SceneIterationTestAccess.h"

#include "GameFramework/Runtime/Foundation/Presentation/IAbilityCueSink.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/Ability/AbilityCatalog.h"

using namespace sw;

/** @brief [AbilityCueSinkTest] 빈 큐는 아무것도 스폰하지 않고 씬의 오브젝트는 그대로다 */
SW_TEST_CASE( AbilityCueSinkTest, EmptyCueSpawnsNothing )
{
    IAbilityCueSink* const pSink = game::findSubsystem<IAbilityCueSink>();
    SW_REQUIRE_NOT_NULL( pSink );
    GameObjectManager manager;
    (void)manager.createGameObject( hashed_string( "Caster" ) );
    manager.beginPlay();
    const size_t           countBefore = manager.getAllGameObjects( SceneIterationTestAccess::getKey() ).size();
    AbilityPresentationDef cue;
    cue._cueTag = TagID::request( "AbilityCueSinkTest.Empty" );
    pSink->presentCue( manager, cue, float3{ 1.0f, 2.0f, 3.0f } );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_EQUAL( countBefore, manager.getAllGameObjects( SceneIterationTestAccess::getKey() ).size() );
}

/** @brief [AbilityCueSinkTest] 흔들림을 적은 큐는 큐 자리에서 적은 크기 · 길이 · 반경으로 흔들림을 한 번 요청하고, 적지 않은 큐는 요청하지 않는다 */
SW_TEST_CASE( AbilityCueSinkTest, ShakeCueRequestsCameraShake )
{
    IAbilityCueSink* const pSink = game::findSubsystem<IAbilityCueSink>();
    SW_REQUIRE_NOT_NULL( pSink );
    GameObjectManager      manager;
    CameraShakeRequest     last;
    int32                  shakeCount   = 0;
    const Subscription     subscription = manager.getCameraRegistry().getShakeChannel().add( SW_DELEGATE_LAMBDA( CameraShakeRequestCallback, [&]( const CameraShakeRequest& request )
        {
        last = request;
        ++shakeCount;
    } ) );
    AbilityPresentationDef cue;
    cue._cueTag = TagID::request( "AbilityCueSinkTest.Shake" );
    pSink->presentCue( manager, cue, float3{} );
    SW_EXPECT_EQUAL( 0, shakeCount );
    cue._shake        = 0.2f;
    cue._shakeSeconds = 0.4f;
    cue._shakeRadius  = 15.0f;
    pSink->presentCue( manager, cue, float3{ 1.0f, 2.0f, 3.0f } );
    SW_REQUIRE_EQUAL( 1, shakeCount );
    SW_EXPECT_NEAR( 2.0f, last._origin._y, 1e-5f );
    SW_EXPECT_NEAR( 0.2f, last._amplitude, 1e-5f );
    SW_EXPECT_NEAR( 0.4f, last._duration, 1e-5f );
    SW_EXPECT_NEAR( 15.0f, last._falloffRadius, 1e-5f );
}
