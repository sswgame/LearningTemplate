#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Visual/Component/MeshComponent.h"
#include "Engine/Visual/MeshInstanceBatch.h"

using namespace sw;

// PrimitiveRegistryTest — 프리미티브 레지스트리의 인스턴스 배치 등록 · 해제, 고정 칸 · 프리리스트, 더티 칸만으로 따라간 사본이 전체 다시 읽기와 같은지(GPUScene 부분 수집의 전제).

namespace
{
    struct TestPrimitiveRegistryInternal
    {
        /** @brief 배치 하나의 항목 수입니다(1 ~ 19 개 — 평균 10 개라 배치 천 개가 항목 약 1 만 개다). */
        static uint32 getEntryCountOf( uint32 batchIndex ) { return 1u + batchIndex % 19u; }

        /** @brief 이 번호의 배치를 지우는지 정합니다(셋 중 하나와 100 ~ 199 번 연속 구간). */
        static bool isRemoved( uint32 batchIndex ) { return batchIndex % 3u == 0u || ( 100u <= batchIndex && batchIndex < 200u ); }

        /** @brief 남은 배치마다 첫 자리가 유효하고 항목 목록이 그 배치를 가리키며, 빈 항목이 아닌 것은 모두 남은 배치의 것인지 확인합니다. */
        static bool isConsistent( const PrimitiveRegistry& registry, const vector<unique_ptr<MeshInstanceBatch>>& listBatch )
        {
            const vector<PrimitiveInstanceEntry>& listEntry = registry.getInstanceEntries();
            uint32                                expected  = 0;
            for ( const unique_ptr<MeshInstanceBatch>& pBatch : listBatch )
            {
                if ( pBatch->isRegistered() == false )
                    continue;
                expected += pBatch->getCount();
                for ( uint32 index = 0; index < pBatch->getCount(); ++index )
                {
                    const uint32 slot = pBatch->getFirstEntrySlot() + index;
                    if ( slot >= listEntry.size() )
                        return false;
                    const bool bPointsHere = listEntry[slot]._pBatch == pBatch.get() && listEntry[slot]._index == index;
                    if ( bPointsHere == false )
                        return false;
                }
            }
            uint32 liveEntryCount = 0;
            for ( const PrimitiveInstanceEntry& entry : listEntry )
                liveEntryCount += entry._pBatch != nullptr ? 1u : 0u;
            return expected == liveEntryCount;
        }
    };
} // namespace

/**
 * @brief [PrimitiveRegistryTest] 배치 여럿을 한 번에 지운 결과가 하나씩 지운 결과와 같다 (배치 천 개, 항목 약 1 만 개)
 * @details 같은 모양의 두 레지스트리에 배치 천 개를 등록하고, 한쪽은 `removeInstanceBatches`(거꾸로 넘기고 널 · 두 번 넘긴 배치를 섞는다)로, 다른 쪽은 `removeInstanceBatch` 를
 *          하나씩 불러 지운다. 남은 배치의 첫 자리 · 항목 목록의 순서가 같아야 하고, 집합 세대는 일괄 해제에서 한 번만 오른다.
 */
SW_TEST_CASE( PrimitiveRegistryTest, BulkBatchRemovalMatchesOneByOneRemoval )
{
    constexpr uint32                      kBatchCount = 1000;
    PrimitiveRegistry                     bulkRegistry;
    PrimitiveRegistry                     singleRegistry;
    vector<unique_ptr<MeshInstanceBatch>> listBulkBatch;
    vector<unique_ptr<MeshInstanceBatch>> listSingleBatch;
    for ( uint32 batchIndex = 0; batchIndex < kBatchCount; ++batchIndex )
    {
        listBulkBatch.push_back( make_unique<MeshInstanceBatch>( nullptr, nullptr, nullptr, TestPrimitiveRegistryInternal::getEntryCountOf( batchIndex ), MeshInstanceWriter::CPU ) );
        listSingleBatch.push_back( make_unique<MeshInstanceBatch>( nullptr, nullptr, nullptr, TestPrimitiveRegistryInternal::getEntryCountOf( batchIndex ), MeshInstanceWriter::CPU ) );
        bulkRegistry.addInstanceBatch( listBulkBatch.back().get() );
        singleRegistry.addInstanceBatch( listSingleBatch.back().get() );
    }
    SW_REQUIRE_TRUE( TestPrimitiveRegistryInternal::isConsistent( bulkRegistry, listBulkBatch ) );
    const uint32 entryTotal = bulkRegistry.getSlotCount();
    SW_EXPECT_TRUE( 5000u < entryTotal );

    vector<ScenePrimitiveBatch*> listBulkRemoved;
    for ( uint32 batchIndex = kBatchCount; batchIndex > 0; --batchIndex )
    {
        if ( TestPrimitiveRegistryInternal::isRemoved( batchIndex - 1 ) )
            listBulkRemoved.push_back( listBulkBatch[batchIndex - 1].get() );
    }
    listBulkRemoved.push_back( nullptr );                // 널은 건너뛴다
    listBulkRemoved.push_back( listBulkBatch[0].get() ); // 두 번 넘긴 배치는 한 번만 지운다
    const uint64 generationBefore = bulkRegistry.getSetGeneration();
    bulkRegistry.removeInstanceBatches( listBulkRemoved );
    SW_EXPECT_EQUAL( generationBefore + 1, bulkRegistry.getSetGeneration() );
    for ( uint32 batchIndex = 0; batchIndex < kBatchCount; ++batchIndex )
    {
        if ( TestPrimitiveRegistryInternal::isRemoved( batchIndex ) )
            singleRegistry.removeInstanceBatch( listSingleBatch[batchIndex].get() );
    }

    SW_EXPECT_EQUAL( singleRegistry.getSlotCount(), bulkRegistry.getSlotCount() );
    SW_EXPECT_TRUE( TestPrimitiveRegistryInternal::isConsistent( bulkRegistry, listBulkBatch ) );
    SW_EXPECT_TRUE( TestPrimitiveRegistryInternal::isConsistent( singleRegistry, listSingleBatch ) );
    bool bSameLayout = true;
    for ( uint32 batchIndex = 0; batchIndex < kBatchCount; ++batchIndex )
    {
        const bool bRemoved = TestPrimitiveRegistryInternal::isRemoved( batchIndex );
        bSameLayout         = bSameLayout && listBulkBatch[batchIndex]->isRegistered() == listSingleBatch[batchIndex]->isRegistered() &&
                      listBulkBatch[batchIndex]->isRegistered() == ( bRemoved == false );
        if ( bRemoved == false )
            bSameLayout = bSameLayout && listBulkBatch[batchIndex]->getFirstEntrySlot() == listSingleBatch[batchIndex]->getFirstEntrySlot();
    }
    SW_EXPECT_TRUE( bSameLayout );

    // 지운 배치는 다시 지워도 아무 일이 없고, 같은 레지스트리에 다시 등록할 수 있다. 다시 등록하면 비워 둔 자리(배치 0 의 옛 자리)를 쓰고 번호 공간은 늘지 않는다.
    const uint32 slotCount = bulkRegistry.getSlotCount();
    bulkRegistry.removeInstanceBatches( listBulkRemoved );
    SW_EXPECT_EQUAL( slotCount, bulkRegistry.getSlotCount() );
    bulkRegistry.addInstanceBatch( listBulkBatch[0].get() );
    SW_EXPECT_TRUE( TestPrimitiveRegistryInternal::isConsistent( bulkRegistry, listBulkBatch ) );
    SW_EXPECT_EQUAL( slotCount, bulkRegistry.getSlotCount() );
    SW_EXPECT_EQUAL( 0u, listBulkBatch[0]->getFirstEntrySlot() );
}

/**
 * @brief [PrimitiveRegistryTest] 메시 하나를 더하고 지워도 다른 메시의 칸 번호는 그대로이고, 지운 칸은 다음 등록이 다시 쓴다
 * @details 예전에는 지우기가 끝 칸을 그 자리로 옮기고(swap-and-pop) 집합 세대를 올려 GPUScene 이 모든 칸을 다시 모았다(감사 RN-6). 지금은 칸을 비워 프리리스트에 넣고
 *          그 칸만 더티로 올린다. 칸 배치 세대(`getLayoutGeneration`)는 영역을 늘릴 때만 오른다.
 */
SW_TEST_CASE( PrimitiveRegistryTest, MeshSlotsStayFixedAndFreedSlotIsReused )
{
    GameObjectManager      manager;
    vector<GameObject*>    listObject;
    vector<MeshComponent*> listMesh;
    for ( uint32 meshIndex = 0; meshIndex < 4; ++meshIndex )
    {
        GameObject* pObject = manager.createGameObject( hashed_string( "SlotMesh" ) );
        SW_REQUIRE_NOT_NULL( pObject );
        MeshComponent* pMesh = pObject->addComponent<MeshComponent>();
        SW_REQUIRE_NOT_NULL( pMesh );
        listObject.push_back( pObject );
        listMesh.push_back( pMesh );
    }
    manager.mergePendingAdds();
    PrimitiveRegistry& registry = manager.getPrimitiveRegistry();
    vector<uint32>     listSlot;
    for ( const MeshComponent* pMesh : listMesh )
    {
        SW_REQUIRE_TRUE( pMesh->getPrimitiveIndex() != ScenePrimitive::kInvalidPrimitiveIndex );
        listSlot.push_back( pMesh->getPrimitiveIndex() );
    }
    const uint64   layoutBefore = registry.getLayoutGeneration();
    vector<uint32> listStateSlot;
    vector<uint32> listTransformSlot;
    registry.consumeDirty( listStateSlot, listTransformSlot );

    // 둘째를 지운다 — 나머지는 번호가 그대로이고, 지운 칸 하나만 더티다.
    const uint32 removedSlot = listSlot[1];
    manager.destroyGameObject( listObject[1] );
    manager.processDeferredDestruction();
    SW_EXPECT_TRUE( registry.getAll()[removedSlot] == nullptr );
    SW_EXPECT_EQUAL( listSlot[0], listMesh[0]->getPrimitiveIndex() );
    SW_EXPECT_EQUAL( listSlot[2], listMesh[2]->getPrimitiveIndex() );
    SW_EXPECT_EQUAL( listSlot[3], listMesh[3]->getPrimitiveIndex() );
    registry.consumeDirty( listStateSlot, listTransformSlot );
    bool bRemovedSlotDirty = false;
    bool bOtherSlotDirty   = false;
    for ( const uint32 slot : listStateSlot )
    {
        bRemovedSlotDirty = bRemovedSlotDirty || slot == removedSlot;
        bOtherSlotDirty   = bOtherSlotDirty || slot == listSlot[0] || slot == listSlot[2] || slot == listSlot[3];
    }
    SW_EXPECT_TRUE( bRemovedSlotDirty );
    SW_EXPECT_FALSE( bOtherSlotDirty ); // 남은 메시는 다시 모을 일이 없다

    // 새 메시는 비운 칸을 다시 쓴다. 칸 배치 세대는 그대로다(전체 수집 없음).
    GameObject* pAdded = manager.createGameObject( hashed_string( "SlotMeshAdded" ) );
    SW_REQUIRE_NOT_NULL( pAdded );
    MeshComponent* pAddedMesh = pAdded->addComponent<MeshComponent>();
    SW_REQUIRE_NOT_NULL( pAddedMesh );
    manager.mergePendingAdds();
    SW_EXPECT_EQUAL( removedSlot, pAddedMesh->getPrimitiveIndex() );
    SW_EXPECT_EQUAL( listSlot[0], listMesh[0]->getPrimitiveIndex() );
    SW_EXPECT_EQUAL( listSlot[3], listMesh[3]->getPrimitiveIndex() );
    SW_EXPECT_EQUAL( layoutBefore, registry.getLayoutGeneration() );
}

namespace
{
    /** @brief 칸 하나의 내용(어느 배치의 몇 번 항목인지, 빈 칸이면 널)입니다 — GPUScene 후보의 대역이다. */
    struct SlotContent
    {
        const ScenePrimitiveBatch* _pBatch{ nullptr };
        uint32                     _index{ 0 };
        bool                       operator==( const SlotContent& other ) const { return _pBatch == other._pBatch && _index == other._index; }
    };

    /** @brief 레지스트리를 칸마다 처음부터 읽은 표입니다(전체 수집의 대역). */
    vector<SlotContent> readAllSlots( const PrimitiveRegistry& registry )
    {
        const uint32        meshRegion = static_cast<uint32>( registry.getAll().size() );
        vector<SlotContent> listContent( registry.getSlotCount() );
        for ( size_t entryIndex = 0; entryIndex < registry.getInstanceEntries().size(); ++entryIndex )
        {
            const PrimitiveInstanceEntry& entry  = registry.getInstanceEntries()[entryIndex];
            listContent[meshRegion + entryIndex] = SlotContent{ entry._pBatch, entry._index };
        }
        return listContent;
    }
} // namespace

/**
 * @brief [PrimitiveRegistryTest] 더티 칸만 다시 읽어 따라간 표가 프레임마다 처음부터 읽은 표와 같다 (배치를 더하고 지우기를 섞은 200 프레임)
 * @details GPUScene 부분 수집의 전제다 — 후보 번호가 칸 번호이고 빌더는 더티 칸만 다시 채우므로, 레지스트리가 바뀐 칸을 빠짐없이 더티로 올리면 두 길의 결과가 같다.
 *          칸 배치 세대가 오른 프레임(영역 늘리기)만 처음부터 다시 읽는다(빌더의 전체 수집과 같다). 지운 배치의 자리를 다음 배치가 다시 쓰는 것도 본다.
 */
SW_TEST_CASE( PrimitiveRegistryTest, DirtySlotTrackingMatchesFullReread )
{
    constexpr uint32                      kBatchCount = 64;
    PrimitiveRegistry                     registry;
    vector<unique_ptr<MeshInstanceBatch>> listBatch;
    for ( uint32 batchIndex = 0; batchIndex < kBatchCount; ++batchIndex )
        listBatch.push_back( make_unique<MeshInstanceBatch>( nullptr, nullptr, nullptr, TestPrimitiveRegistryInternal::getEntryCountOf( batchIndex ), MeshInstanceWriter::CPU ) );

    vector<SlotContent> listTracked;
    uint64              trackedLayout = 0;
    vector<uint32>      listStateSlot;
    vector<uint32>      listTransformSlot;
    uint32              random      = 12345u;
    uint32              reuseCount  = 0;
    bool                bAllMatched = true;
    for ( uint32 frame = 0; frame < 200; ++frame )
    {
        // 프레임마다 배치 몇 개를 넣거나 뺀다(결정적 의사 난수).
        for ( uint32 step = 0; step < 3; ++step )
        {
            random                   = random * 1664525u + 1013904223u;
            MeshInstanceBatch& batch = *listBatch[( random >> 8 ) % kBatchCount];
            if ( batch.isRegistered() )
            {
                registry.removeInstanceBatch( &batch );
                continue;
            }
            const uint32 slotCountBefore = registry.getSlotCount();
            registry.addInstanceBatch( &batch );
            reuseCount += registry.getSlotCount() == slotCountBefore ? 1u : 0u;
        }

        registry.consumeDirty( listStateSlot, listTransformSlot );
        const vector<SlotContent> listFull = readAllSlots( registry );
        if ( registry.getLayoutGeneration() != trackedLayout )
        {
            listTracked   = listFull;
            trackedLayout = registry.getLayoutGeneration();
            continue;
        }
        // 부분 길: 칸은 끝에만 붙는다 — 붙은 칸은 더티로 온다. 더티 칸만 다시 읽는다.
        listTracked.resize( listFull.size() );
        for ( const uint32 slot : listStateSlot )
        {
            SW_REQUIRE_TRUE( slot < listFull.size() );
            listTracked[slot] = listFull[slot];
        }
        bAllMatched = bAllMatched && listTracked == listFull;
    }
    SW_EXPECT_TRUE( bAllMatched );
    SW_EXPECT_TRUE( reuseCount > 0u ); // 지운 배치의 자리를 다시 쓴 프레임이 있었다
}

namespace
{
    /**
     * @brief 대기 함수가 받는 컴포넌트 자리에 세우는 기록입니다. 함수는 컴포넌트 멤버를 읽지 않고 이 기록으로만 쓴다(주소만 빌린다).
     */
    struct WaiterProbe
    {
        PrimitiveRegistry* _pRegistry{ nullptr };
        uint32             _callCount{ 0 };
    };

    Component* asComponent( WaiterProbe& probe )
    {
        return reinterpret_cast<Component*>( &probe );
    }

    /** @brief 자기를 빼고 다 받았다고 답합니다(함수 안에서 자기 컴포넌트를 내리는 경우). */
    bool removeSelfAndFinish( Component& component, float32 /*deltaSeconds*/ )
    {
        WaiterProbe& probe = reinterpret_cast<WaiterProbe&>( component );
        ++probe._callCount;
        probe._pRegistry->removeAssetWaiter( &component );
        return false;
    }

    /** @brief 부른 수만 세고 계속 기다립니다. */
    bool countAndKeepWaiting( Component& component, float32 /*deltaSeconds*/ )
    {
        ++reinterpret_cast<WaiterProbe&>( component )._callCount;
        return true;
    }
} // namespace

/**
 * @brief [PrimitiveRegistryTest] 대기 함수가 도는 중에 자기를 빼도 다른 대기자는 불리고 남는다
 * @details 예전에는 빼기가 자리를 당겨(끝 대기자를 그 자리로) 도는 쪽이 그 자리를 "다 받음" 으로 한 번 더 빼, 끝 대기자를 부르지도 않고 잃었다 — 메시가 영영 붙지 않는다.
 */
SW_TEST_CASE( PrimitiveRegistryTest, WaiterRemovedDuringWalkDoesNotDropAnother )
{
    PrimitiveRegistry registry;
    WaiterProbe       first{ &registry };
    WaiterProbe       middle{ &registry };
    WaiterProbe       last{ &registry };
    registry.addAssetWaiter( asComponent( first ), &removeSelfAndFinish );
    registry.addAssetWaiter( asComponent( middle ), &countAndKeepWaiting );
    registry.addAssetWaiter( asComponent( last ), &countAndKeepWaiting );

    registry.updateAssetWaiters( 0.0f );
    SW_EXPECT_EQUAL( 1u, first._callCount );
    SW_EXPECT_EQUAL( 1u, middle._callCount );
    SW_EXPECT_EQUAL( 1u, last._callCount ); // 끝 대기자도 이 프레임에 불렸다
    SW_EXPECT_EQUAL( size_t( 2 ), registry.countAssetWaiters() );

    registry.removeAssetWaiter( asComponent( middle ) );
    registry.removeAssetWaiter( asComponent( last ) );
    SW_EXPECT_EQUAL( size_t( 0 ), registry.countAssetWaiters() );
}
