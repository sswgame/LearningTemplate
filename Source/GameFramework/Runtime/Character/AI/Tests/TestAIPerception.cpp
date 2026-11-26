#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/AI/AIPerception.h"
#include "GameFramework/Runtime/Character/AI/AIStimulusSourceComponent.h"
#include "GameFramework/Runtime/Character/Combat/TargetFilter.h"
#include "GameFramework/Runtime/Character/Control/Controller/ControllerComponent.h"
#include "GameFramework/Runtime/Character/Navigation/NavGrid.h"

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
    context._deltaTime   = GameSeconds{ 0.1f };
    int32       runCount = 0;
    GameSeconds lastDelta{};
    for ( uint32 tick = 0; tick < 9; ++tick )
    {
        context._tick = tick;
        GameSeconds delta{};
        if ( schedule.advance( context, 3, 0, delta ) )
        {
            ++runCount;
            lastDelta = delta;
        }
    }
    SW_EXPECT_EQUAL( 3, runCount );                   // 틱 0 · 3 · 6
    SW_EXPECT_NEAR( 0.3f, lastDelta.count(), 1e-5f ); // 틱 4 · 5 · 6 을 모았다
    SW_EXPECT_NEAR( 0.2f, schedule._elapsed.count(), 1e-5f );

    ControlFrameInterval everyFrame;
    int32                everyCount = 0;
    for ( uint32 tick = 0; tick < 5; ++tick )
    {
        context._tick = tick;
        GameSeconds delta{};
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
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStimulus.size() );
    SW_EXPECT_EQUAL( pNear->getOwner()->getObjectID(), listStimulus[0].getID() );
    SW_EXPECT_NEAR( 3.0f, listStimulus[0].getPosition()._x, 1e-5f );

    AIStimulusSourceComponent::gatherStimuli( manager, 0, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() ); // 자기 팀은 묻지 않는다

    AIPerception perception;
    perception.sense( manager, 1, float3{}, float3{ 1.0f, 0.0f, 0.0f }, nullptr, 0.1f );
    const AIPerceivedTarget* pTarget = perception.findTarget( pNear->getOwner()->getObjectID() );
    SW_REQUIRE_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( pTarget->_bSeen != SW_FALSE );

    const uint64 nearID = pNear->getOwner()->getObjectID();
    SW_REQUIRE_TRUE( pNear->getOwner()->removeComponent( pNear ) );
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );
    perception.sense( manager, 1, float3{}, float3{ 1.0f, 0.0f, 0.0f }, nullptr, 0.1f );
    pTarget = perception.findTarget( nearID );
    SW_REQUIRE_NOT_NULL( pTarget ); // 기억은 남는다
    SW_EXPECT_TRUE( pTarget->_bSeen == SW_FALSE );
}

/** @brief [AIPerceptionTest] 숨은 대상(`State.Hidden`)은 보이지 않고 소리는 들린다. 나오면 다시 보인다 */
SW_TEST_CASE( AIPerceptionTest, HiddenObjectIsHeardButNotSeen )
{
    GameObjectManager          manager;
    AIStimulusSourceComponent* pSource = Util::makeSource( manager, "Near", float3{ 3.0f, 0.0f, 0.0f } );
    GameObject* const          pObject = pSource->getOwner();
    vector<AIStimulus>         listStimulus;
    vector<Component*>         listKey;
    pObject->addTag( TargetFilter::getHiddenTag() );
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );
    pSource->reportNoise( 5.0f );
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_EQUAL( size_t{ 1 }, listStimulus.size() );
    pSource->clearNoise();
    pObject->removeTag( TargetFilter::getHiddenTag() );
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_EQUAL( size_t{ 1 }, listStimulus.size() );
}

/** @brief [AIPerceptionTest] 쓰러졌거나(`State.Dead`) 꺼진 대상은 보이지 않는다 — 시체 · 풀에 돌아간 몸을 쫓지 않는다 */
SW_TEST_CASE( AIPerceptionTest, DeadOrInactiveObjectIsNotSeen )
{
    GameObjectManager          manager;
    AIStimulusSourceComponent* pSource = Util::makeSource( manager, "Near", float3{ 3.0f, 0.0f, 0.0f } );
    GameObject* const          pObject = pSource->getOwner();
    vector<AIStimulus>         listStimulus;
    vector<Component*>         listKey;
    pObject->addTag( TargetFilter::getDeadTag() );
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );
    pObject->removeTag( TargetFilter::getDeadTag() );
    pObject->setActive( false );
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );
    pObject->setActive( true );
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_EQUAL( size_t{ 1 }, listStimulus.size() );
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
 * @brief [AIPerceptionTest] 시야 격자는 절대 자리다 — 월드 원점을 옮긴 뒤에도 눈 · 대상(씬 자리)을 같은 칸에서 읽는다
 * @details 변이: `canSee` 가 씬 자리를 그대로 격자에 물으면 격자 밖이라 트인 곳도 막힌 것으로 읽는다.
 */
SW_TEST_CASE( AIPerceptionTest, SightGridIsAbsolute )
{
    GameObjectManager manager;
    NavGrid           grid;
    SW_REQUIRE_TRUE( grid.initialize( 8, 8, 1.0f, float3{ 500.0f, 0.0f, 0.0f } ) );
    manager.setWorldOrigin( double3{ 500.0, 0.0, 0.0 } );
    AIPerception perception;
    const float3 eye{ 0.5f, 0.0f, 0.5f };
    const float3 forward{ 1.0f, 0.0f, 0.0f };
    const float3 target{ 7.5f, 0.0f, 0.5f };
    perception.sense( manager, 1, eye, forward, &grid, 0.1f ); // 지금 월드 원점을 받는다
    SW_EXPECT_TRUE( perception.canSee( eye, forward, target, &grid, false ) );
    grid.setBlocked( 4, 0, true );
    SW_EXPECT_FALSE( perception.canSee( eye, forward, target, &grid, false ) );
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
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );

    pLoud->reportNoise( 8.0f ); // 틱 밖이라 바로 고친다
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listStimulus.size() );
    SW_EXPECT_NEAR( 8.0f, listStimulus[0].getNoiseRadius(), 1e-5f );

    pLoud->reportNoise( 4.0f ); // 보통 귀의 상자(10 + 4 m)에는 들지 않는 소리(15 m) — 밝은 귀는 반경 × 배율까지 후보를 넓힌다
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 3.0f, listStimulus, listKey );
    SW_EXPECT_EQUAL( size_t{ 1 }, listStimulus.size() );

    pLoud->clearNoise();
    AIStimulusSourceComponent::gatherStimuli( manager, 1, float3{}, 10.0f, 1.0f, listStimulus, listKey );
    SW_EXPECT_TRUE( listStimulus.empty() );
}

/**
 * @brief [AIPerceptionTest] 소리 자극은 시야 안이어도 보이지 않고 들리기만 한다. 보이던 대상이 시야에서 나가 소리만 내면 들리되 보이지 않는다
 */
SW_TEST_CASE( AIPerceptionTest, SoundIsHeardNotSeen )
{
    const float3 eye{};
    const float3 forward{ 1.0f, 0.0f, 0.0f };
    const float3 ahead{ 3.0f, 0.0f, 0.0f };
    AIPerception perception;
    perception.sense( eye, forward, vector<AIStimulus>{ AIStimulus::makeHearing( ahead, 7, 8.0f ) }, nullptr, 0.1f );
    const AIPerceivedTarget* pTarget = perception.findTarget( 7 );
    SW_REQUIRE_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( pTarget->_bHeard == SW_TRUE );
    SW_EXPECT_TRUE( pTarget->_bSeen == SW_FALSE );
    SW_EXPECT_NULL( perception.findNearestSeen( eye ) );

    perception.sense( eye, forward, vector<AIStimulus>{ AIStimulus::makeSight( ahead, 7 ) }, nullptr, 0.1f );
    pTarget = perception.findTarget( 7 );
    SW_REQUIRE_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( pTarget->_bSeen == SW_TRUE );
    SW_EXPECT_TRUE( pTarget->_bHeard == SW_FALSE );

    perception.sense( eye, forward, vector<AIStimulus>{ AIStimulus::makeHearing( ahead, 7, 8.0f ) }, nullptr, 0.1f );
    pTarget = perception.findTarget( 7 );
    SW_REQUIRE_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( pTarget->_bSeen == SW_FALSE );
    SW_EXPECT_TRUE( pTarget->_bHeard == SW_TRUE );
    SW_EXPECT_NEAR( 0.0f, pTarget->_age, 1.0e-6f );
}

/** @brief [AIPerceptionTest] 기억한 대상은 상태로 남는다 — 되살린 감각이 같은 자리 · 나이를 기억하고 이어서 잊는다 */
SW_TEST_CASE( AIPerceptionTest, MemorySurvivesRestore )
{
    AIPerceptionSettings settings;
    settings._memoryDuration = 2.0f;
    AIPerception perception;
    perception.setSettings( settings );
    const float3 eye{ 0.0f, 1.6f, 0.0f };
    const float3 forward{ 0.0f, 0.0f, 1.0f };
    perception.sense( eye, forward, { AIStimulus::makeHearing( float3{ 3.0f, 0.0f, 2.0f }, 7u, 10.0f ) }, nullptr, 0.1f );
    perception.sense( eye, forward, {}, nullptr, 1.0f );
    SW_REQUIRE_EQUAL( size_t( 1 ), perception.getTargets().size() );
    Archive written;
    perception.writeState( written );

    AIPerception restored;
    restored.setSettings( settings );
    Archive reading( written.getData(), written.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    const AIPerceivedTarget* pTarget = restored.findTarget( 7u );
    SW_REQUIRE_NOT_NULL( pTarget );
    SW_EXPECT_TRUE( ( pTarget->_lastKnownPosition == float3{ 3.0f, 0.0f, 2.0f } ) );
    SW_EXPECT_TRUE( pTarget->_age == perception.findTarget( 7u )->_age );
    restored.sense( eye, forward, {}, nullptr, 1.5f );
    SW_EXPECT_NULL( restored.findTarget( 7u ) );
}
