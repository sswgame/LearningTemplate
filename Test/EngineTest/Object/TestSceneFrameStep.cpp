/**
 * @file TestSceneFrameStep.cpp
 * @brief 씬 프레임 표(`SceneFrameStepList.xxx`) — `tick` 이 표의 줄 순서대로 돌고, 표의 자리가 실제 효과의 자리인가.
 */
#include "pch.h"

#include "Core/Container/vector.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/SceneFrameStep.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

/**
 * @brief [SceneFrameStepTest] tick 한 번이 표의 단계를 줄 순서대로 한 번씩 돌고, 단계마다 이름이 있다
 */
SW_TEST_CASE( SceneFrameStepTest, StepsRunInTableOrder )
{
    sw::GameObjectManager          manager;
    sw::vector<sw::SceneFrameStep> listStep;
    manager.setFrameStepObserver( sw::GameObjectManager::FrameStepObserver( [&listStep]( sw::SceneFrameStep step )
    { listStep.push_back( step ); } ) );

    manager.tick( 0.016f );

    const uint32 stepCount = static_cast<uint32>( sw::SceneFrameStep::Count );
    SW_ASSERT_EQUAL( static_cast<size_t>( stepCount ), listStep.size() );
    for ( uint32 index = 0; index < stepCount; ++index )
    {
        SW_EXPECT_EQUAL( index, static_cast<uint32>( listStep[index] ) );
        SW_EXPECT_TRUE( sw::string_view( sw::getSceneFrameStepName( listStep[index] ) ) != "Unknown" );
    }
    SW_EXPECT_STREQ( "Unknown", sw::getSceneFrameStepName( sw::SceneFrameStep::Count ) );

    // 관찰자를 풀면 더 부르지 않는다.
    manager.setFrameStepObserver( sw::GameObjectManager::FrameStepObserver{} );
    manager.tick( 0.016f );
    SW_EXPECT_EQUAL( static_cast<size_t>( stepCount ), listStep.size() );
}

/**
 * @brief [SceneFrameStepTest] 물리 앞 그룹의 틱 중 트랜스폼 쓰기는 표의 ApplyPrePhysics 단계에서 적용된다 — 그 단계 앞에서는 옛 값, 다음 단계(내비게이션)부터 새 값
 */
SW_TEST_CASE( SceneFrameStepTest, PrePhysicsTickWritesLandInTheApplyStep )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::GameObject*             pObj   = manager.createGameObject( sw::hashed_string( "FrameStepMover" ) );
    sw::MockTickSceneComponent* pMover = pObj->addComponent<sw::MockTickSceneComponent>();
    SW_ASSERT_TRUE( pMover != nullptr );
    pMover->setTickGroup( sw::TickGroup::DuringPhysics );
    pMover->_bWriteLocalOnTick = SW_TRUE;
    pMover->_tickLocalPos      = sw::float3{ 5.0f, 0.0f, 0.0f };

    sw::vector<float32> listLocalX( static_cast<size_t>( sw::SceneFrameStep::Count ), -1.0f );
    manager.setFrameStepObserver( sw::GameObjectManager::FrameStepObserver( [&listLocalX, pMover]( sw::SceneFrameStep step )
    { listLocalX[static_cast<size_t>( step )] = pMover->getLocalPosition()._x; } ) );

    manager.tick( 0.016f );

    SW_EXPECT_NEAR_EQUAL( 0.0f, listLocalX[static_cast<size_t>( sw::SceneFrameStep::TickPrePhysics )], 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, listLocalX[static_cast<size_t>( sw::SceneFrameStep::ApplyPrePhysics )], 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, listLocalX[static_cast<size_t>( sw::SceneFrameStep::Navigation )], 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 5.0f, listLocalX[static_cast<size_t>( sw::SceneFrameStep::Physics )], 1e-5f );
}
