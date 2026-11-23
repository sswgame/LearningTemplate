// PhysicsReadScopeTest — 물리 읽기 구간은 스텝 밖에서 열고, 닫아 받은 증표(`WorkerReadFence`)가 있어야 씬을 고친다(nogpu).
#include "pch.h"

#include "Core/Foundation/StdHeaders.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Foundation/WorkerReadFence.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

namespace sw
{
    /** @brief 안을 보는 테스트 창구입니다(`friend`). */
    struct PhysicsReadScopeTestAccess
    {
        static uint32 getOpenReadScopeCount( const ScenePhysics& physics ) { return physics._openReadScopeCount; }
    };
} // namespace sw

using namespace sw;

// 증표는 주인만 만든다 — 아무나 만들 수 있으면 워커를 기다리지 않고 씬을 고치는 길이 컴파일된다.
static_assert( std::is_default_constructible_v<WorkerReadFence<ScenePhysics>> == false, "only ScenePhysics makes its write fence" );
static_assert( std::is_copy_constructible_v<PhysicsReadScope> == false, "a read scope is closed exactly once" );

/**
 * @brief [PhysicsReadScopeTest] 연 구간 수를 세고, 닫으면 줄어든다 — 씬이 아직 없으면 질의 창은 비고 씬을 만들지 않는다
 * @details 변이: `closeReadScope` 의 감소를 빼면 마지막 단언이 진다(스텝이 영영 막힌다).
 */
SW_TEST_CASE( PhysicsReadScopeTest, ScopesAreCountedUntilClosed )
{
    GameObjectManager manager;
    ScenePhysics&     physics = manager.getSubsystem<ScenePhysics>();
    SW_EXPECT_EQUAL( 0u, PhysicsReadScopeTestAccess::getOpenReadScopeCount( physics ) );

    PhysicsReadScope first  = physics.openReadScope();
    PhysicsReadScope second = physics.openReadScope();
    SW_EXPECT_EQUAL( 2u, PhysicsReadScopeTestAccess::getOpenReadScopeCount( physics ) );
    SW_EXPECT_NULL( first.findQuery3D() );
    SW_EXPECT_NULL( physics.findScene3D() );

    PhysicsReadScope moved = std::move( first );
    (void)physics.closeReadScope( std::move( moved ) );
    (void)physics.closeReadScope( std::move( second ) );
    SW_EXPECT_EQUAL( 0u, PhysicsReadScopeTestAccess::getOpenReadScopeCount( physics ) );
}
