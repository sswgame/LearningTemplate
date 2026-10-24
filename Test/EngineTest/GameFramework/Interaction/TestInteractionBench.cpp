/**
 * @file TestInteractionBench.cpp
 * @brief 상호작용 마이크로벤치 — 하는 쪽 하나가 씬의 상호작용 대상을 고르는 틱 비용이 씬 크기를 따라 어떻게 느는가, 하는 쪽 여럿이 공유 격자를 쓸 때 어떻게 느는가.
 * @details 숫자를 찍기만 하고 판정하지 않는다(`GameObjectBenchTest` 와 같은 규칙 — Release 로 읽는다). 하는 쪽이 있는 줄과 없는 줄의 차가
 *          `InteractorComponent` 의 고르기(`forEachComponentOfType` 로 씬 전체를 훑는다) 비용이다.
 */
#include "pch.h"

#include "Core/Container/formatString.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractorComponent.h"

#include "TestFramework/Harness/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_TEST_LOAD_SUITE( InteractionBenchTest, "interactor tick cost as the scene grows" );
SW_TEST_TIME_BUDGET( InteractionBenchTest, 3000, "첫 케이스 최대 36 ms(2026-10-15, Ryzen 7 6800H, 세 번 중 최대)의 네 배에 하는 쪽 여럿 케이스(여섯 구성 × 200 프레임)의 여유를 더함 — 통합 측정 뒤 맞춘다" );

SW_LOG_CALLER( "InteractionBench" );

namespace
{
    struct InteractionBenchInternal
    {
        static constexpr uint32 kInteractableCount = 16;
        static constexpr uint32 kFrameCount        = 200;

        /** @brief 씬 컴포넌트만 든 오브젝트 @p objectCount 개(그중 16 개는 상호작용 대상)와 하는 쪽 하나(있으면)를 세우고 틱 표본을 받습니다. */
        static void measure( uint32 objectCount, bool bWithInteractor, sw::vector<int64>& outListMicro )
        {
            sw::GameObjectManager  manager;
            sw::InteractionDef     open;
            sw::InteractionStepDef step;
            step._mode        = sw::InteractionInputMode::Press;
            open._id          = sw::hashed_string( "Open" );
            open._maxDistance = 3.0f;
            open._listStep.push_back( step );
            for ( uint32 index = 0; index < objectCount; ++index )
            {
                sw::GameObject*     pObject = manager.createGameObject( sw::hashed_string( "Prop" ) );
                sw::SceneComponent* pScene  = pObject->addComponent<sw::SceneComponent>();
                pScene->setLocalPosition( sw::float3( static_cast<float32>( index % 100 ), 0.0f, static_cast<float32>( index / 100 ) ) );
                if ( index % ( objectCount / kInteractableCount ) == 0 )
                    pObject->addComponent<sw::InteractableComponent>()->setDefinition( open );
            }
            if ( bWithInteractor )
            {
                sw::GameObject* pActor = manager.createGameObject( sw::hashed_string( "Actor" ) );
                (void)pActor->addComponent<sw::SceneComponent>();
                (void)pActor->addComponent<sw::InteractorComponent>();
            }
            manager.beginPlay();
            manager.update( 1.0f / 60.0f );
            outListMicro.clear();
            for ( uint32 frame = 0; frame < kFrameCount; ++frame )
            {
                const sw::Stopwatch stopwatch;
                manager.update( 1.0f / 60.0f );
                outListMicro.push_back( stopwatch.getElapsedMicroseconds() );
            }
            manager.endPlay();
        }

        /**
         * @brief 상호작용 대상 @p targetCount 개를 넓은 판에 펴 놓고 하는 쪽 @p interactorCount 개를 같은 판에 흩어 세운 뒤 틱 표본을 받습니다.
         * @details 하는 쪽이 둘 이상이면 공유 격자(`InteractionSpatialIndex`)가, 하나면 목록 순회가 고르기를 한다. 대상 사이는 4 m, 도달 거리 3 m 라
         *          하는 쪽마다 후보는 한두 개다.
         */
        static void measureCrowd( uint32 targetCount, uint32 interactorCount, sw::vector<int64>& outListMicro )
        {
            sw::GameObjectManager  manager;
            sw::InteractionDef     open;
            sw::InteractionStepDef step;
            step._mode        = sw::InteractionInputMode::Press;
            open._id          = sw::hashed_string( "Open" );
            open._maxDistance = 3.0f;
            open._listStep.push_back( step );
            uint32 side = 1;
            while ( side * side < targetCount )
            {
                ++side;
            }
            for ( uint32 index = 0; index < targetCount; ++index )
            {
                sw::GameObject*     pObject = manager.createGameObject( sw::hashed_string( "Prop" ) );
                sw::SceneComponent* pScene  = pObject->addComponent<sw::SceneComponent>();
                pScene->setLocalPosition( sw::float3( static_cast<float32>( index % side ) * 4.0f, 0.0f, static_cast<float32>( index / side ) * 4.0f ) );
                pObject->addComponent<sw::InteractableComponent>()->setDefinition( open );
            }
            for ( uint32 index = 0; index < interactorCount; ++index )
            {
                sw::GameObject*     pActor = manager.createGameObject( sw::hashed_string( "Actor" ) );
                sw::SceneComponent* pScene = pActor->addComponent<sw::SceneComponent>();
                const uint32        cell   = ( index * 7919u ) % ( side * side );
                pScene->setLocalPosition( sw::float3( static_cast<float32>( cell % side ) * 4.0f, 0.0f, static_cast<float32>( cell / side ) * 4.0f + 1.0f ) );
                (void)pActor->addComponent<sw::InteractorComponent>();
            }
            manager.beginPlay();
            manager.update( 1.0f / 60.0f );
            outListMicro.clear();
            for ( uint32 frame = 0; frame < kFrameCount; ++frame )
            {
                const sw::Stopwatch stopwatch;
                manager.update( 1.0f / 60.0f );
                outListMicro.push_back( stopwatch.getElapsedMicroseconds() );
            }
            manager.endPlay();
        }
    };
} // namespace

/**
 * @brief [InteractionBenchTest] 씬 틱 — 하는 쪽 없음 / 하나, 오브젝트 1,000 · 10,000(상호작용 대상 16). 두 줄의 차가 고르기 비용이다
 */
SW_TEST_CASE( InteractionBenchTest, InteractorTickFollowsSceneSize )
{
    sw::vector<int64> listMicro;
    for ( const uint32 objectCount : { 1000u, 10000u } )
    {
        InteractionBenchInternal::measure( objectCount, false, listMicro );
        test::logBenchSamples( objectCount == 1000u ? "scene tick 1k objects, no interactor" : "scene tick 10k objects, no interactor", listMicro );
        InteractionBenchInternal::measure( objectCount, true, listMicro );
        test::logBenchSamples( objectCount == 1000u ? "scene tick 1k objects, 1 interactor" : "scene tick 10k objects, 1 interactor", listMicro );
        SW_EXPECT_EQUAL( static_cast<size_t>( InteractionBenchInternal::kFrameCount ), listMicro.size() );
    }
}

/**
 * @brief [InteractionBenchTest] 하는 쪽 여럿 × 대상 수천 — 하는 쪽 1 (목록 순회) · 8 · 64 (공유 격자), 대상 2,000 · 5,000. 하는 쪽 수로 나눈 틱 비용이 격자에서 평평한지 읽는다
 */
SW_TEST_CASE( InteractionBenchTest, SharedGridTickWithManyInteractors )
{
    sw::vector<int64> listMicro;
    for ( const uint32 targetCount : { 2000u, 5000u } )
    {
        for ( const uint32 interactorCount : { 1u, 8u, 64u } )
        {
            InteractionBenchInternal::measureCrowd( targetCount, interactorCount, listMicro );
            sw::fixed_string<sw::constant::kMaxBuffer128> label;
            sw::formatstring( label.data(), label.capacity(), "scene tick, %# interactables, %# interactors", targetCount, interactorCount );
            test::logBenchSamples( label.c_str(), listMicro );
            SW_EXPECT_EQUAL( static_cast<size_t>( InteractionBenchInternal::kFrameCount ), listMicro.size() );
        }
    }
}
