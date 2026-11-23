#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/AI/AIPerception.h"
#include "GameFramework/Runtime/Character/AI/AIStimulusSourceComponent.h"
#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"

using namespace sw;

// AIPerceptionTest — AI 판단 간격(`ControlFrameInterval`)과 감각의 공유 질의(자극 원천이 팀 · 종류별 씬 공간 색인에 오르고 감각이 직접 묻는다)를 본다(nogpu).

namespace
{
    struct AIPerceptionTestUtil
    {
        /** @brief @p position 에 선 자극 원천 하나를 만듭니다. 팀은 기본(0)입니다 — 팀은 등록 전에 정해야 해서 붙이는 순간 등록되는 테스트는 기본 팀을 쓴다. */
        static AIStimulusSourceComponent* makeSource( GameObjectManager& manager, const char* pName, const float3& position )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( pName ) );
            pObject->addComponent<SceneComponent>()->setLocalPosition( position );
            return pObject->addComponent<AIStimulusSourceComponent>();
        }
    };
    using Util = AIPerceptionTestUtil;
} // namespace

/**
 * @brief [AIPerceptionTest] 간격 N 이면 조종 프레임 N 번에 한 번 차례가 오고, 그 사이 프레임 시간을 모아 낸다. 간격 1 은 매 프레임이다
 */
SW_TEST_CASE( AIPerceptionTest, ThinkIntervalRunsOncePerInterval )
{
    ControlFrameInterval schedule;
    ControlFrameContext  context;
    context._deltaTime = 0.1f;
    int32   runCount   = 0;
    float32 lastDelta  = 0.0f;
    for ( uint32 tick = 0; tick < 9; ++tick )
    {
        context._tick = tick;
        float32 delta = 0.0f;
        if ( schedule.advance( context, 3, 0, delta ) )
        {
            ++runCount;
            lastDelta = delta;
        }
    }
    SW_EXPECT_EQUAL( 3, runCount );           // 틱 0 · 3 · 6
    SW_EXPECT_NEAR( 0.3f, lastDelta, 1e-5f ); // 틱 4 · 5 · 6 을 모았다
    SW_EXPECT_NEAR( 0.2f, schedule._elapsed, 1e-5f );

    ControlFrameInterval everyFrame;
    int32                everyCount = 0;
    for ( uint32 tick = 0; tick < 5; ++tick )
    {
        context._tick = tick;
        float32 delta = 0.0f;
        everyCount += everyFrame.advance( context, 1, 7, delta ) ? 1 : 0;
    }
    SW_EXPECT_EQUAL( 5, everyCount );
}

/**
 * @brief [AIPerceptionTest] 등록한 자극 원천이 공유 질의로 잡히고(자기 팀 · 상자 밖은 빠진다), 지운 원천은 잡히지 않는다. 씬 판 감각이 그 질의로 본다
 */
SW_TEST_CASE( AIPerceptionTest, SharedQueryFindsRegisteredSources )
{
    GameObjectManager          manager;
    AIStimulusSourceComponent* pNear = Util::makeSource( manager, "Near", float3{ 3.0f, 0.0f, 0.0f } );
    (void)Util::makeSource( manager, "Far", float3{ 50.0f, 0.0f, 0.0f } );

    vector<AIStimulus> listStimulus;
    vector<Component*> listKey;
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, listStimulus, listKey );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStimulus.size() );
    SW_EXPECT_EQUAL( pNear->getOwner()->getObjectID(), listStimulus[0]._id );
    SW_EXPECT_NEAR( 3.0f, listStimulus[0]._position._x, 1e-5f );

    AIStimulusSourceComponent::gatherStimuli( manager, 0, float3{}, 10.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() ); // 자기 팀은 묻지 않는다

    AIPerception perception;
    perception.sense( manager, 1, float3{}, float3{ 1.0f, 0.0f, 0.0f }, nullptr, 0.1f );
    const AIPerceivedTarget* pTarget = perception.findTarget( pNear->getOwner()->getObjectID() );
    SW_REQUIRE_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( pTarget->_bSeen != SW_FALSE );

    const uint64 nearID = pNear->getOwner()->getObjectID();
    SW_REQUIRE_TRUE( pNear->getOwner()->removeComponent( pNear ) );
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );
    perception.sense( manager, 1, float3{}, float3{ 1.0f, 0.0f, 0.0f }, nullptr, 0.1f );
    pTarget = perception.findTarget( nearID );
    SW_REQUIRE_NOT_NULL( pTarget ); // 기억은 남는다
    SW_EXPECT_TRUE( pTarget->_bSeen == SW_FALSE );
}

/**
 * @brief [AIPerceptionTest] 월드 원점을 옮기면 안 보이게 된 대상의 마지막 자리도 같은 만큼 옮겨진다 — 수색이 옛 원점의 엉뚱한 자리로 가지 않는다
 */
SW_TEST_CASE( AIPerceptionTest, RememberedPositionFollowsTheWorldOrigin )
{
    GameObjectManager          manager;
    AIStimulusSourceComponent* pSource = Util::makeSource( manager, "Near", float3{ 3.0f, 0.0f, 4.0f } );
    const uint64               id      = pSource->getOwner()->getObjectID();
    manager.setWorldOrigin( double3{ 500.0, 0.0, 0.0 } ); // 감각이 생기기 전의 이동은 기억과 상관없다

    AIPerception perception;
    perception.sense( manager, 1, float3{}, float3{ 1.0f, 0.0f, 0.0f }, nullptr, 0.1f );
    SW_REQUIRE_NOT_NULL( perception.findTarget( id ) );
    SW_EXPECT_NEAR( 3.0f, perception.findTarget( id )->_lastKnownPosition._x, 1e-4f );

    SW_REQUIRE_TRUE( pSource->getOwner()->removeComponent( pSource ) ); // 대상이 사라졌다 — 기억만 남는다
    manager.setWorldOrigin( double3{ 1500.0, 0.0, 500.0 } );
    perception.sense( manager, 1, float3{ -1000.0f, 0.0f, -500.0f }, float3{ 1.0f, 0.0f, 0.0f }, nullptr, 0.1f );
    const AIPerceivedTarget* pTarget = perception.findTarget( id );
    SW_REQUIRE_NOT_NULL( pTarget );
    SW_EXPECT_NEAR( -997.0f, pTarget->_lastKnownPosition._x, 1e-3f );
    SW_EXPECT_NEAR( -496.0f, pTarget->_lastKnownPosition._z, 1e-3f );
}

/**
 * @brief [AIPerceptionTest] 소리는 반경만큼 색인 상자를 넓혀 시야 상자 밖에서도 들린다. 거두면 다시 들리지 않는다
 */
SW_TEST_CASE( AIPerceptionTest, NoiseWidensTheHearingEntry )
{
    GameObjectManager          manager;
    AIStimulusSourceComponent* pLoud = Util::makeSource( manager, "Loud", float3{ 15.0f, 0.0f, 0.0f } );
    vector<AIStimulus>         listStimulus;
    vector<Component*>         listKey;
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );

    pLoud->reportNoise( 8.0f ); // 틱 밖이라 바로 고친다
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, listStimulus, listKey );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStimulus.size() );
    SW_EXPECT_NEAR( 8.0f, listStimulus[0]._noiseRadius, 1e-5f );

    pLoud->clearNoise();
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );
}
