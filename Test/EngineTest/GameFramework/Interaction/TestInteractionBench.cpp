/**
 * @file TestInteractionBench.cpp
 * @brief 상호작용 마이크로벤치 — 하는 쪽 하나가 씬의 상호작용 대상을 고르는 틱 비용이 씬 크기를 따라 어떻게 느는가.
 * @details 숫자를 찍기만 하고 판정하지 않는다(`GameObjectBenchTest` 와 같은 규칙 — Release 로 읽는다). 하는 쪽이 있는 줄과 없는 줄의 차가
 *          `InteractorComponent` 의 고르기(`forEachComponentOfType` 로 씬 전체를 훑는다) 비용이다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/Interaction/InteractableComponent.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractionCatalog.h"
#include "GameFramework/Base/Gameplay/Interaction/InteractorComponent.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

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
            manager.tick( 1.0f / 60.0f );
            outListMicro.clear();
            for ( uint32 frame = 0; frame < kFrameCount; ++frame )
            {
                const sw::Stopwatch stopwatch;
                manager.tick( 1.0f / 60.0f );
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
