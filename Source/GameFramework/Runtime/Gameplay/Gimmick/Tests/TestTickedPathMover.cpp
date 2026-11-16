#include "pch.h"

#include "Core/Foundation/Constants.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Control/ControlSystem.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/Tests/TickOrderTestComponents.h"
#include "GameFramework/Runtime/Gameplay/Gimmick/TickedPathMoverComponent.h"

// TickedPathMoverTest — 틱 함수 바닥의 자리 계산(왕복 · 고리)과, 바닥이 서버 틱을 알리는 틱 뒤에 돌아 같은 프레임에 알린 틱의 자리에 서는지 본다(서브틱 선행 조건, 씬 순회 없음).

using namespace sw;

namespace
{
    /** @brief 서버 틱을 알리는 오브젝트와 바닥 둘을 둔 씬입니다. 바닥은 x 로 1 초에 1 m 씩 가는 직선(1000 초 구간)이라 x 가 곧 서버 틱의 시각이다. */
    struct TickedMoverOrderRig
    {
        static constexpr uint32 kMoverCount = 2;

        GameObjectManager              _manager;
        ServerTickSourceTestComponent* _pSource{ nullptr };
        SceneComponent*                _arrMoverRoot[kMoverCount]{};

        TickedMoverOrderRig()
        {
            (void)ControlSystem::findOrCreate( _manager );
            const vector<float3> listWaypoint{
                float3{ 1000.0f, 0.0f, 0.0f }
            };
            // 바닥을 알리는 쪽보다 먼저 만든다 — 만든 순서가 아니라 선행 조건이 순서를 정하는지 보려고.
            for ( uint32 index = 0; index < kMoverCount; ++index )
            {
                GameObject* pMover              = _manager.createGameObject( hashed_string( "Mover" ) );
                _arrMoverRoot[index]            = pMover->addComponent<SceneComponent>();
                TickedPathMoverComponent* pPath = pMover->addComponent<TickedPathMoverComponent>();
                pPath->setPath( listWaypoint, 1000.0f, false );
            }
            _pSource = _manager.createGameObject( hashed_string( "NetPawn" ) )->addComponent<ServerTickSourceTestComponent>();
            _manager.beginPlay();
        }

        /** @brief 서버 틱 @p serverTick 의 바닥 x 입니다. */
        static float32 computeExpectedX( uint32 serverTick )
        {
            const vector<float3> listWaypoint{
                float3{ 1000.0f, 0.0f, 0.0f }
            };
            const float64 seconds = static_cast<float64>( serverTick ) * static_cast<float64>( constant::time::kDefaultFixedStepSeconds );
            return TickedPathMoverComponent::computeOffset( listWaypoint, 1000.0f, false, seconds )._x;
        }
    };
} // namespace

/** @brief [TickedPathMoverTest] 바닥은 같은 프레임에 알린 서버 틱의 자리에 선다 — 알리는 차이가 프레임마다 바뀌어 한 프레임 늦으면 어긋난다 */
SW_TEST_CASE( TickedPathMoverTest, ReadsServerTickPublishedThisFrame )
{
    TickedMoverOrderRig rig;
    // 첫 프레임에 바닥이 선행 조건을 건다(틱 중 등록은 틱 뒤에 든다). 그 뒤 프레임부터 순서가 맞아야 한다.
    rig._manager.update( 1.0f / 60.0f );
    for ( uint32 frame = 0; frame < 4; ++frame )
    {
        rig._manager.update( 1.0f / 60.0f );
        const float32 expectedX = TickedMoverOrderRig::computeExpectedX( rig._pSource->_lastServerTick );
        for ( const SceneComponent* pRoot : rig._arrMoverRoot )
        {
            SW_EXPECT_NEAR( expectedX, pRoot->getWorldPosition()._x, 1.0e-4f );
        }
    }
}

/** @brief [TickedPathMoverTest] 서버 틱을 알리는 쪽이 없으면 바닥은 조종 틱의 자리에 선다(차이 0) */
SW_TEST_CASE( TickedPathMoverTest, WithoutSourceFollowsControlTick )
{
    GameObjectManager manager;
    ControlSystem&    control = ControlSystem::findOrCreate( manager );
    GameObject*       pMover  = manager.createGameObject( hashed_string( "Mover" ) );
    SceneComponent*   pRoot   = pMover->addComponent<SceneComponent>();
    pMover->addComponent<TickedPathMoverComponent>()->setPath( vector<float3>{
                                                                   float3{ 1000.0f, 0.0f, 0.0f }
    },
                                                               1000.0f, false );
    manager.beginPlay();
    manager.update( 1.0f / 60.0f );
    manager.update( 1.0f / 60.0f );
    SW_EXPECT_TRUE( control.getServerTick() == control.getTick() );
    SW_EXPECT_NEAR( TickedMoverOrderRig::computeExpectedX( control.getTick() ), pRoot->getWorldPosition()._x, 1.0e-4f );
}

/** @brief [TickedPathMoverTest] 왕복은 끝에서 되돌아오고 같은 시각이면 같은 자리다(틱의 순수 함수) */
SW_TEST_CASE( TickedPathMoverTest, PingPongGoesBackFromTheEnd )
{
    const vector<float3> listWaypoint{
        float3{ 6.0f, 0.0f, 0.0f }
    };
    SW_EXPECT_NEAR( 0.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 2.0f, true, 0.0 )._x, 1.0e-4f );
    SW_EXPECT_NEAR( 3.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 2.0f, true, 1.0 )._x, 1.0e-4f );
    SW_EXPECT_NEAR( 3.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 2.0f, true, 3.0 )._x, 1.0e-4f );
    SW_EXPECT_NEAR( 3.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 2.0f, true, 5.0 )._x, 1.0e-4f ); // 한 바퀴(4 초) 뒤
}

/** @brief [TickedPathMoverTest] 고리는 끝에서 시작으로 한 구간 더 가고, 경유점이 없으면 움직이지 않는다 */
SW_TEST_CASE( TickedPathMoverTest, LoopClosesAndEmptyPathStays )
{
    const vector<float3> listWaypoint{
        float3{2.0f, 0.0f, 0.0f},
        float3{2.0f, 0.0f, 2.0f}
    };
    SW_EXPECT_NEAR( 2.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 1.0f, false, 1.0 )._x, 1.0e-4f );
    SW_EXPECT_NEAR( 2.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 1.0f, false, 2.0 )._z, 1.0e-4f );
    SW_EXPECT_NEAR( 1.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 1.0f, false, 2.5 )._z, 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, TickedPathMoverComponent::computeOffset( listWaypoint, 1.0f, false, 3.0 )._x, 1.0e-4f );
    SW_EXPECT_NEAR( 0.0f, TickedPathMoverComponent::computeOffset( vector<float3>{}, 1.0f, false, 1.0 )._x, 1.0e-4f );
}
