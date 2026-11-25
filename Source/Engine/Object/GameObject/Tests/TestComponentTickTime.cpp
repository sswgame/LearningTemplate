// ComponentTickTimeTest — 프레임의 컴포넌트 틱 시간이 프로파일러 구간에 쌓인다(감시 값 `Scene.ComponentTickShare` 의 재료, 틱 주기 등급 판정의 재는 장치)(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Tests/TickModeTestComponents.h"
#include "Engine/Object/GameObject/Tick/SceneTickScheduler.h"
#include "Engine/Profiling/FrameProfiler.h"

using namespace sw;

/**
 * @brief [ComponentTickTimeTest] 두 틱 단계가 한 프레임에 같은 구간(`SceneTickScheduler::kComponentTickScopeName`)으로 쌓인다 — 호출 둘, 시간은 0 보다 크다
 * @details 변이: 한 단계에서 구간을 빼면 호출 수가 1 이 되고, 구간 이름을 바꾸면 감시 값이 읽는 슬롯이 비어 단언이 진다.
 */
SW_TEST_CASE( ComponentTickTimeTest, BothPhasesAccumulateIntoTheComponentTickScope )
{
    GameObjectManager manager;
    for ( uint32 index = 0; index < 64; ++index )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( "Mover" ) );
        SW_REQUIRE_NOT_NULL( pObject );
        SW_REQUIRE_NOT_NULL( pObject->addComponent<TickModeMoverTestComponent>() );
    }
    manager.update( 1.0f / 60.0f ); // 만든 오브젝트가 합쳐지는 프레임(계측 밖) — 재는 프레임은 두 단계가 모두 돈다

    FrameProfiler& profiler    = engine::getSubsystem<FrameProfiler>();
    const bool     bWasEnabled = profiler.isEnabled();
    profiler.setEnabled( true );
    profiler.beginFrame();
    manager.update( 1.0f / 60.0f );
    profiler.endFrame();
    profiler.setEnabled( bWasEnabled );

    const uint32 slot = profiler.registerScope( SceneTickScheduler::kComponentTickScopeName );
    SW_EXPECT_EQUAL( uint64{ 2 }, profiler.getLastFrameCount( slot ) );
    SW_EXPECT_TRUE( profiler.getLastFrameNanos( slot ) > 0 );
}
