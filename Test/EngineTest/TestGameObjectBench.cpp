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
 *          - `findGameObjectById` 의 락 없는 id 표 조회(흩어진 순서).
 */
#include "pch.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Object/Component/2D/BoxCollider2DComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/Component/SceneTransformHierarchy.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/TestGameObjectMocks.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

SW_LOG_CALLER( "GameObjectBench" );

namespace
{
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

    constexpr uint32 kChainDepth = 1000;

    /** @brief 씬 컴포넌트 하나씩 든 오브젝트 @p depth 개를 한 줄로 잇습니다. 루트를 돌려주고, 맨 끝은 @p pOutLeaf 에 적습니다. */
    sw::GameObject* buildDeepChain( sw::GameObjectManager& manager, uint32 depth, sw::GameObject*& pOutLeaf )
    {
        sw::GameObject* pRoot = nullptr;
        sw::GameObject* pPrev = nullptr;
        for ( uint32 level = 0; level < depth; ++level )
        {
            sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "Chain" ) );
            pObj->addComponent<sw::SceneComponent>();
            if ( pPrev != nullptr )
                SW_EXPECT_TRUE( pObj->attachToParent( pPrev ) );
            else
                pRoot = pObj;
            pPrev = pObj;
        }
        pOutLeaf = pPrev;
        return pRoot;
    }
} // namespace

/**
 * @brief [GameObjectBenchTest] 스폰 8000 · 첫 틱 · 정상 틱 · 파괴
 */
SW_TEST_CASE( GameObjectBenchTest, SpawnTickDestroy )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::vector<sw::GameObject*> listObject;
    listObject.reserve( kObjectCount );

    const auto spawnStart = std::chrono::steady_clock::now();
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        listObject.push_back( spawnMoverObject( manager, false ) );
    }
    [[maybe_unused]] const int64 spawnMicro = test::getElapsedMicroseconds( spawnStart );

    const auto firstTickStart = std::chrono::steady_clock::now();
    manager.tick( 0.016f );
    [[maybe_unused]] const int64 firstTickMicro = test::getElapsedMicroseconds( firstTickStart );

    sw::vector<int64> listTick;
    for ( uint32 round = 0; round < 30; ++round )
    {
        const auto start = std::chrono::steady_clock::now();
        manager.tick( 0.016f );
        listTick.push_back( test::getElapsedMicroseconds( start ) );
    }

    const auto destroyStart = std::chrono::steady_clock::now();
    for ( sw::GameObject* pObj : listObject )
    {
        if ( pObj != nullptr )
            manager.destroyObject( pObj );
    }
    manager.processDeferredDestruction();
    [[maybe_unused]] const int64 destroyMicro = test::getElapsedMicroseconds( destroyStart );

    SW_LOG_INFO( "[Bench] sizeof GameObject %#  Component %#  SceneComponent %#  TickItemList %#", sizeof( sw::GameObject ), sizeof( sw::Component ), sizeof( sw::SceneComponent ), sizeof( sw::TickItemList ) );
    SW_LOG_INFO( "[Bench] spawn %# objects (scene + tick component): %# us (%# ns each)", kObjectCount, spawnMicro, ( spawnMicro * 1000 ) / kObjectCount );
    SW_LOG_INFO( "[Bench] first tick (merge + registry build): %# us", firstTickMicro );
    test::logBenchSamples( "steady tick, 8000 tick-only components", listTick );
    SW_LOG_INFO( "[Bench] destroy %# objects + process: %# us (%# ns each)", kObjectCount, destroyMicro, ( destroyMicro * 1000 ) / kObjectCount );

    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );
}

/**
 * @brief [GameObjectBenchTest] 틱 안에서 위치·스케일을 쓰는 무버 8000 개의 tick()
 */
SW_TEST_CASE( GameObjectBenchTest, TickMovers )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

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
        listTick.push_back( test::getElapsedMicroseconds( start ) );
    }
    test::logBenchSamples( "tick, 8000 movers writing position + scale", listTick );

    uint32 wrongCount = 0;
    for ( sw::MockTickSceneComponent* pMover : listMover )
    {
        if ( pMover == nullptr || sw::float3::getDistanceSquared( pMover->getWorldPosition(), pMover->_tickLocalPos ) > 1e-6f )
            ++wrongCount;
    }
    SW_EXPECT_EQUAL( 0u, wrongCount );
}

/**
 * @brief [GameObjectBenchTest] 배치 트랜스폼 쓰기 8000 건(잎 루트, 핸들은 모두 다름) — `GameObjectManager::applyTransformBatch`
 * @details 벤치 씬의 인스턴스 모드가 매 프레임 이 길로 큐브 8000 개를 움직인다. 병렬 적용이 같은 대상을 두 워커에 주지 않게 대상 버킷으로
 *          나눈 비용(세는 정렬 두 번)을 여기서 본다.
 */
SW_TEST_CASE( GameObjectBenchTest, ApplyTransformBatch )
{
    sw::GameObjectManager           manager;
    sw::vector<sw::SceneComponent*> listComp;
    listComp.reserve( kObjectCount );
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "BatchBench" ) );
        listComp.push_back( pObj->addComponent<sw::SceneComponent>() );
    }
    manager.flushSceneTransforms();

    sw::vector<sw::SceneTransformWrite> listWrite( kObjectCount );
    sw::vector<int64>                   listApply;
    for ( uint32 round = 0; round < 60; ++round )
    {
        // 값이 매 라운드 달라야 실제로 쓴다 — 같은 값은 건너뛴다.
        const float32 offset = static_cast<float32>( round + 1 ) * 0.25f;
        for ( uint32 index = 0; index < kObjectCount; ++index )
        {
            listWrite[index]._handle = listComp[index]->getHandle();
            listWrite[index].setValue( sw::SceneTransformPage::kLocalPosition, sw::float3( static_cast<float32>( index ), offset, 0.0f ) );
            listWrite[index].setValue( sw::SceneTransformPage::kLocalRotation, sw::float3( 0.0f, offset, 0.0f ) );
        }
        const auto start = std::chrono::steady_clock::now();
        manager.applyTransformBatch( listWrite.data(), kObjectCount );
        listApply.push_back( test::getElapsedMicroseconds( start ) );
    }
    test::logBenchSamples( "applyTransformBatch, 8000 leaf roots (position + rotation)", listApply );

    manager.flushSceneTransforms();
    uint32 wrongCount = 0;
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        if ( sw::MathUtil::nearEqual( listComp[index]->getWorldPosition()._x, static_cast<float32>( index ) ) == false )
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
    sw::RegisterMockComponents();

    constexpr uint32 kDepth = 1000;
    sw::GameObject*  pRoot  = nullptr;
    sw::GameObject*  pPrev  = nullptr;
    for ( uint32 depth = 0; depth < kDepth; ++depth )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "Chain" ) );
        pObj->addComponent<sw::SceneComponent>();
        if ( pPrev != nullptr )
            SW_EXPECT_TRUE( pObj->attachToParent( pPrev ) );
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
        listToggle.push_back( test::getElapsedMicroseconds( start ) );
    }
    test::logBenchSamples( "setActive false+true on a 1000-deep chain root", listToggle );
    SW_EXPECT_TRUE( pPrev->isActiveInHierarchy() );
}

/**
 * @brief [GameObjectBenchTest] getComponent<T> 의 뜨거운 비용
 */
SW_TEST_CASE( GameObjectBenchTest, GetComponentHot )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

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
    [[maybe_unused]] const int64 micro = test::getElapsedMicroseconds( start );
    SW_LOG_INFO( "[Bench] getComponent<SceneComponent> (third of three, two misses first): %# ns per call", ( micro * 1000 ) / kIterationCount );
    SW_EXPECT_TRUE( sink != 0 );
}

/**
 * @brief [GameObjectBenchTest] findGameObjectById — 락 없는 id 표 조회. 컴포넌트 핸들을 풀 때마다 지나는 길입니다.
 */
SW_TEST_CASE( GameObjectBenchTest, FindById )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();

    sw::vector<uint64> listObjectId;
    listObjectId.reserve( kObjectCount );
    for ( uint32 index = 0; index < kObjectCount; ++index )
    {
        sw::GameObject* pObj = manager.createGameObject( sw::hashed_string( "BenchObject" ) );
        if ( pObj != nullptr )
            listObjectId.push_back( pObj->getObjectId() );
    }
    manager.tick( 0.016f );
    SW_ASSERT_EQUAL( kObjectCount, static_cast<uint32>( listObjectId.size() ) );

    // 조회 순서를 흩는다. id 는 연속이라 순서대로 읽으면 캐시가 실제보다 좋다(곱셈 해시로 섞는다).
    constexpr uint32   kProbeCount = 1u << 18;
    sw::vector<uint64> listProbe;
    listProbe.reserve( kProbeCount );
    for ( uint32 index = 0; index < kProbeCount; ++index )
        listProbe.push_back( listObjectId[( static_cast<uint64>( index ) * 2654435761ull ) % kObjectCount] );

    // 다섯 판 중 가장 빠른 판 — 첫 판의 캐시 · 페이지 비용을 걸러낸다.
    int64  bestNanos  = std::numeric_limits<int64>::max();
    uint32 wrongCount = 0;
    for ( uint32 round = 0; round < 5; ++round )
    {
        uintptr_t  sink  = 0;
        const auto start = std::chrono::steady_clock::now();
        for ( uint32 index = 0; index < kProbeCount; ++index )
        {
            const sw::GameObject* pFound = manager.findGameObjectById( listProbe[index] );
            if ( pFound == nullptr )
                ++wrongCount;
            sink += reinterpret_cast<uintptr_t>( pFound );
        }
        const int64 nanos = std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::steady_clock::now() - start ).count();
        bestNanos         = std::min( bestNanos, nanos );
        SW_EXPECT_TRUE( sink != 0 );
    }
    [[maybe_unused]] const int64 deciNanos = ( bestNanos * 10 ) / kProbeCount;
    SW_LOG_INFO( "[Bench] findGameObjectById (8000 objects, scattered): %#.%# ns per call", deciNanos / 10, deciNanos % 10 );
    SW_EXPECT_EQUAL( 0u, wrongCount );
}

/**
 * @brief [GameObjectBenchTest] 1000 단 계층의 루트 이동 — 더티 표시(세터)와 플러시를 따로 잽니다.
 */
SW_TEST_CASE( GameObjectBenchTest, DeepChainMove )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();
    sw::GameObject* pLeaf = nullptr;
    sw::GameObject* pRoot = buildDeepChain( manager, kChainDepth, pLeaf );
    manager.tick( 0.016f );
    sw::SceneComponent* pRootScene = pRoot->getComponent<sw::SceneComponent>();
    sw::SceneComponent* pLeafScene = pLeaf->getComponent<sw::SceneComponent>();
    SW_ASSERT_NOT_NULL( pRootScene );
    SW_ASSERT_NOT_NULL( pLeafScene );

    sw::vector<int64> listMark;
    sw::vector<int64> listFlush;
    for ( uint32 round = 0; round < 40; ++round )
    {
        const auto markStart = std::chrono::steady_clock::now();
        pRootScene->setLocalPosition( sw::float3{ static_cast<float32>( round + 1 ), 0.0f, 0.0f } );
        listMark.push_back( test::getElapsedMicroseconds( markStart ) );
        const auto flushStart = std::chrono::steady_clock::now();
        manager.flushSceneTransforms();
        listFlush.push_back( test::getElapsedMicroseconds( flushStart ) );
    }
    test::logBenchSamples( "mark dirty: setLocalPosition on a 1000-deep chain root", listMark );
    test::logBenchSamples( "flush: flushSceneTransforms after that move", listFlush );
    SW_EXPECT_NEAR_EQUAL( 40.0f, pLeafScene->getWorldPosition()._x, 1e-3f );
}

/**
 * @brief [GameObjectBenchTest] 1000 단 계층의 첫 틱(병합) · 파괴 · 매니저 해체 — 판마다 새 매니저.
 */
SW_TEST_CASE( GameObjectBenchTest, DeepChainLifecycle )
{
    sw::vector<int64> listFirstTick;
    sw::vector<int64> listDestroy;
    sw::vector<int64> listTeardown;
    for ( uint32 round = 0; round < 5; ++round )
    {
        // 판마다 새 매니저다 — 하나를 돌려 쓰면 앞 판이 남긴 상태(풀 · 이름 번호)가 뒤 판을 잰다.
        {
            sw::GameObjectManager manager;
            sw::RegisterMockComponents();
            sw::GameObject* pLeaf = nullptr;
            sw::GameObject* pRoot = buildDeepChain( manager, kChainDepth, pLeaf );

            const auto tickStart = std::chrono::steady_clock::now();
            manager.tick( 0.016f );
            listFirstTick.push_back( test::getElapsedMicroseconds( tickStart ) );

            const auto destroyStart = std::chrono::steady_clock::now();
            manager.destroyObject( pRoot, true );
            manager.processDeferredDestruction();
            listDestroy.push_back( test::getElapsedMicroseconds( destroyStart ) );
            SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );
        }
        {
            sw::GameObjectManager* pManager = sw_new sw::GameObjectManager();
            sw::RegisterMockComponents();
            sw::GameObject* pLeaf = nullptr;
            buildDeepChain( *pManager, kChainDepth, pLeaf );
            pManager->tick( 0.016f );
            const auto teardownStart = std::chrono::steady_clock::now();
            sw_delete( pManager );
            listTeardown.push_back( test::getElapsedMicroseconds( teardownStart ) );
        }
    }
    test::logBenchSamples( "first tick (merge) of a 1000-deep chain", listFirstTick );
    test::logBenchSamples( "destroyObject(root) + process on a 1000-deep chain", listDestroy );
    test::logBenchSamples( "manager teardown with a 1000-deep chain", listTeardown );
}

/**
 * @brief [GameObjectBenchTest] 플레이 중 콜라이더 오브젝트 8000 스폰 · 그 onBeginPlay 를 부르는 다음 틱 · 파괴
 * @details 투사체가 거듭 생기는 모양이다. 엔진 컴포넌트가 onBeginPlay 에서 하는 일(바디 등록)의 스폰당 비용을 본다.
 */
SW_TEST_CASE( GameObjectBenchTest, SpawnCollidersDuringPlay )
{
    sw::GameObjectManager manager;
    sw::RegisterMockComponents();
    manager.beginPlay();

    sw::vector<sw::GameObject*> listObject;
    listObject.reserve( kObjectCount );
    sw::vector<int64> listSpawn;
    sw::vector<int64> listBeginTick;
    sw::vector<int64> listDestroy;
    size_t            componentCount{ 0 };
    for ( uint32 round = 0; round < 7; ++round )
    {
        listObject.clear();
        const auto spawnStart = std::chrono::steady_clock::now();
        for ( uint32 index = 0; index < kObjectCount; ++index )
        {
            sw::GameObject*             pObj = manager.createGameObject( sw::hashed_string( "Bullet" ) );
            sw::BoxCollider2DComponent* pBox = pObj != nullptr ? pObj->addComponent<sw::BoxCollider2DComponent>() : nullptr;
            if ( pBox != nullptr )
            {
                pBox->setOffsetScale( sw::float2{ 0.2f, 0.2f } );
                pBox->setLocalPosition( sw::float3{ static_cast<float32>( index ) * 2.0f, 0.0f, 0.0f } );
            }
            listObject.push_back( pObj );
        }
        listSpawn.push_back( test::getElapsedMicroseconds( spawnStart ) );

        const auto tickStart = std::chrono::steady_clock::now();
        manager.tick( 0.016f );
        listBeginTick.push_back( test::getElapsedMicroseconds( tickStart ) );
        componentCount = listObject.front() != nullptr ? listObject.front()->getComponentCount() : 0;

        const auto destroyStart = std::chrono::steady_clock::now();
        for ( sw::GameObject* pObj : listObject )
        {
            if ( pObj != nullptr )
                manager.destroyObject( pObj );
        }
        manager.processDeferredDestruction();
        listDestroy.push_back( test::getElapsedMicroseconds( destroyStart ) );
    }
    manager.endPlay();

    SW_LOG_INFO( "[Bench] components per spawned collider object after onBeginPlay: %#", componentCount );
    // onBeginPlay 가 소유 태그 컴포넌트를 붙이지 않는다 — 콜라이더 하나뿐이다.
    SW_EXPECT_EQUAL( size_t( 1 ), componentCount );
    test::logBenchSamples( "spawn 8000 collider objects during play", listSpawn );
    test::logBenchSamples( "next tick (onBeginPlay + body add + step) of 8000 colliders", listBeginTick );
    test::logBenchSamples( "destroy 8000 collider objects + process", listDestroy );
    SW_EXPECT_EQUAL( static_cast<size_t>( 0 ), manager.getAllGameObjects().size() );
}

/**
 * @brief [GameObjectBenchTest] 이름으로 컴포넌트 만들기 8000 회 — 씬 · 프리팹 로드가 컴포넌트마다 지나는 길(`addComponentByName`)입니다.
 * @details 오브젝트를 먼저 만들어 두고 붙이는 것만 잽니다. 코드젠이 생성 함수를 준 실제 컴포넌트(`SceneComponent` · `TagComponent`)로 잽니다.
 */
SW_TEST_CASE( GameObjectBenchTest, AddComponentByName )
{
    sw::GameObjectManager       manager;
    sw::vector<sw::GameObject*> listObject;
    listObject.reserve( kObjectCount );

    sw::vector<int64> listRound;
    uint32            addedCount{ 0 };
    for ( uint32 round = 0; round < 10; ++round )
    {
        listObject.clear();
        for ( uint32 index = 0; index < kObjectCount; ++index )
            listObject.push_back( manager.createGameObject( sw::hashed_string( "BenchObject" ) ) );

        const auto start = std::chrono::steady_clock::now();
        for ( sw::GameObject* pObj : listObject )
        {
            if ( manager.addComponentByName( pObj, sw::hashed_string( "SceneComponent" ) ) != nullptr )
                ++addedCount;
            if ( manager.addComponentByName( pObj, sw::hashed_string( "TagComponent" ) ) != nullptr )
                ++addedCount;
        }
        listRound.push_back( test::getElapsedMicroseconds( start ) );

        for ( sw::GameObject* pObj : listObject )
            manager.destroyObject( pObj );
        manager.processDeferredDestruction();
    }

    test::logBenchSamples( "addComponentByName x2 on 8000 objects (SceneComponent + TagComponent)", listRound );
    SW_EXPECT_EQUAL( kObjectCount * 2u * 10u, addedCount );
}
