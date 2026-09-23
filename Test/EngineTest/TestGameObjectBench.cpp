/**
 * @file TestGameObjectBench.cpp
 * @brief GameObject · Component 마이크로벤치 — 스폰 · 틱(무버) · 파괴 · 활성 토글 · 컴포넌트 조회.
 * @details 숫자를 **찍기만** 하고 판정하지 않는다(기계마다 다르다). 판정은 정합성만 본다. `TaskManagerBenchTest` 와 같은 규칙:
 *          Release 로 읽고, 인용하려면 이전/이후 바이너리를 같은 시각에 번갈아 잰다.
 *
 *          재는 것:
 *          - 오브젝트 8000 개 스폰(씬 컴포넌트 + 틱 컴포넌트) · 첫 틱(병합 + 등록부) · 정상 상태 틱 · 파괴.
 *          - 틱 안에서 위치·스케일을 쓰는 무버 8000 개의 `tick()` — 병렬 틱 + 슬롯 큐 적용 + 플러시.
 *          - 깊은 계층(1000 단)의 활성 토글 — 계층 재계산이 몇 번 도는가.
 *          - 컴포넌트 조회 `getComponent<T>` 의 뜨거운 비용.
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/vector.h"

#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestFramework.h"

SW_LOG_CALLER( "GameObjectBench" );

namespace
{
    /** @brief 정렬한 표본의 백분위 값. */
    int64 percentile( sw::vector<int64>& listSample, uint32 percent )
    {
        if ( listSample.empty() )
            return 0;
        std::sort( listSample.begin(), listSample.end() );
        size_t rank = ( listSample.size() * percent ) / 100;
        if ( rank >= listSample.size() )
            rank = listSample.size() - 1;
        return listSample[rank];
    }

    int64 elapsedMicro( const std::chrono::steady_clock::time_point& start )
    {
        return std::chrono::duration_cast<std::chrono::microseconds>( std::chrono::steady_clock::now() - start ).count();
    }

    /** @brief 표본 하나를 [min · p50 · max] 로 찍습니다. Shipping 은 Info 로그가 컴파일에서 빠져 값만 계산하고 만다. */
    void logSamples( [[maybe_unused]] const utf8* pLabel, sw::vector<int64>& listSample )
    {
        [[maybe_unused]] const int64 minValue = percentile( listSample, 0 );
        [[maybe_unused]] const int64 p50      = percentile( listSample, 50 );
        [[maybe_unused]] const int64 maxValue = percentile( listSample, 100 );
        SW_LOG_INFO( "[Bench] %#  min %# us  p50 %# us  max %# us  (%# samples)", pLabel, minValue, p50, maxValue, listSample.size() );
    }

    constexpr uint32 kObjectCount = 8000;

    /** @brief 이름은 전부 같다 — 총알처럼 같은 이름으로 거듭 만드는 모양. 유일화가 O(1) 인지도 같이 잰다. */
    sw::GameObject* spawnMoverObject( sw::GameObjectManager& manager, bool bWritesTransform )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "BenchObject" ) );
        if ( pObj == nullptr )
            return nullptr;
        sw::MockTickSceneComponent* pMover = pObj->addComponent<sw::MockTickSceneComponent>();
        if ( pMover != nullptr )
        {
            pMover->_bWriteLocalOnTick = bWritesTransform ? SW_TRUE : SW_FALSE;
            pMover->_bWriteScaleOnTick = bWritesTransform ? SW_TRUE : SW_FALSE;
            pMover->_tickLocalPos      = sw::float3{ 1.0f, 0.0f, 1.0f };
            pMover->_tickLocalScale    = sw::float3{ 0.8f, 0.8f, 0.8f };
        }
        return pObj;
    }
} // namespace

/**
 * @brief [GameObjectBenchTest] 스폰 8000 · 첫 틱 · 정상 틱 · 파괴
 */
SW_TEST_CASE( GameObjectBenchTest, SpawnTickDestroy )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::vector<sw::GameObject*> listObject;
    listObject.reserve( kObjectCount );

    const auto spawnStart = std::chrono::steady_clock::now();
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        listObject.push_back( spawnMoverObject( manager, false ) );
    }
    [[maybe_unused]] const int64 spawnMicro = elapsedMicro( spawnStart );

    const auto firstTickStart = std::chrono::steady_clock::now();
    manager.tick( 0.016f );
    [[maybe_unused]] const int64 firstTickMicro = elapsedMicro( firstTickStart );

    sw::vector<int64> listTick;
    for ( uint32 round = 0; round < 30; ++round )
    {
        const auto start = std::chrono::steady_clock::now();
        manager.tick( 0.016f );
        listTick.push_back( elapsedMicro( start ) );
    }

    const auto destroyStart = std::chrono::steady_clock::now();
    for ( sw::GameObject* pObj : listObject )
    {
        if ( pObj != nullptr )
            manager.destroyObject( pObj );
    }
    manager.processDeferredDestruction();
    [[maybe_unused]] const int64 destroyMicro = elapsedMicro( destroyStart );

    SW_LOG_INFO( "[Bench] sizeof GameObject %#  Component %#  SceneComponent %#  TickItemList %#", sizeof( sw::GameObject ), sizeof( sw::Component ), sizeof( sw::SceneComponent ), sizeof( sw::TickItemList ) );
    SW_LOG_INFO( "[Bench] spawn %# objects (scene + tick component): %# us (%# ns each)", kObjectCount, spawnMicro, ( spawnMicro * 1000 ) / kObjectCount );
    SW_LOG_INFO( "[Bench] first tick (merge + registry build): %# us", firstTickMicro );
    logSamples( "steady tick, 8000 tick-only components", listTick );
    SW_LOG_INFO( "[Bench] destroy %# objects + process: %# us (%# ns each)", kObjectCount, destroyMicro, ( destroyMicro * 1000 ) / kObjectCount );

    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );
}

/**
 * @brief [GameObjectBenchTest] 틱 안에서 위치·스케일을 쓰는 무버 8000 개의 tick()
 */
SW_TEST_CASE( GameObjectBenchTest, TickMovers )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    sw::vector<sw::MockTickSceneComponent*> listMover;
    listMover.reserve( kObjectCount );
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = spawnMoverObject( manager, true );
        listMover.push_back( pObj != nullptr ? pObj->getComponent<sw::MockTickSceneComponent>() : nullptr );
    }
    manager.tick( 0.016f );

    sw::vector<int64> listTick;
    for ( uint32 round = 0; round < 30; ++round )
    {
        // 값이 매 틱 달라야 세터가 실제로 쓴다 — 같은 값은 건너뛴다.
        const float32 height = static_cast<float32>( round + 1 ) * 0.25f;
        for ( sw::MockTickSceneComponent* pMover : listMover )
        {
            if ( pMover != nullptr )
                pMover->_tickLocalPos._y = height;
        }
        const auto start = std::chrono::steady_clock::now();
        manager.tick( 0.016f );
        listTick.push_back( elapsedMicro( start ) );
    }
    logSamples( "tick, 8000 movers writing position + scale", listTick );

    uint32 wrongCount = 0;
    for ( sw::MockTickSceneComponent* pMover : listMover )
    {
        if ( pMover == nullptr || sw::float3::getDistanceSquared( pMover->getWorldPosition(), pMover->_tickLocalPos ) > 1e-6f )
            ++wrongCount;
    }
    SW_EXPECT_EQUAL( 0u, wrongCount );
}

/**
 * @brief [GameObjectBenchTest] 1000 단 계층의 활성 토글
 */
SW_TEST_CASE( GameObjectBenchTest, SetActiveDeepChain )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    constexpr uint32 kDepth = 1000;
    sw::GameObject*  pRoot  = nullptr;
    sw::GameObject*  pPrev  = nullptr;
    for ( uint32 depth = 0; depth < kDepth; ++depth )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "Chain" ) );
        pObj->addComponent<sw::SceneComponent>();
        if ( pPrev != nullptr )
            pObj->attachToParent( pPrev );
        else
            pRoot = pObj;
        pPrev = pObj;
    }
    manager.tick( 0.016f );

    sw::vector<int64> listToggle;
    for ( uint32 round = 0; round < 20; ++round )
    {
        const auto start = std::chrono::steady_clock::now();
        pRoot->setActive( false );
        pRoot->setActive( true );
        listToggle.push_back( elapsedMicro( start ) );
    }
    logSamples( "setActive false+true on a 1000-deep chain root", listToggle );
    SW_EXPECT_TRUE( pPrev->isActiveInHierarchy() );
}

/**
 * @brief [GameObjectBenchTest] getComponent<T> 의 뜨거운 비용
 */
SW_TEST_CASE( GameObjectBenchTest, GetComponentHot )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents( manager );

    // 찾는 것은 진짜 리플렉션 타입(`SceneComponent`)이어야 한다 — 모의 컴포넌트의 `StaticType()` 은 부를 때마다 이름을 인턴해
    // 그 비용이 조회를 가린다. 앞의 둘은 캐스트가 실패하는 모의 타입이다.
    sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "Lookup" ) );
    pObj->addComponent<sw::MockMeshComponent>();
    pObj->addComponent<sw::MockAudioComponent>();
    pObj->addComponent<sw::SceneComponent>();
    manager.tick( 0.016f );

    constexpr uint32 kIterationCount = 2000000;
    uintptr_t        sink            = 0;
    const auto       start           = std::chrono::steady_clock::now();
    for ( uint32 iteration = 0; iteration < kIterationCount; ++iteration )
    {
        sink += reinterpret_cast<uintptr_t>( pObj->getComponent<sw::SceneComponent>() );
    }
    [[maybe_unused]] const int64 micro = elapsedMicro( start );
    SW_LOG_INFO( "[Bench] getComponent<SceneComponent> (third of three, two misses first): %# ns per call", ( micro * 1000 ) / kIterationCount );
    SW_EXPECT_TRUE( sink != 0 );
}
