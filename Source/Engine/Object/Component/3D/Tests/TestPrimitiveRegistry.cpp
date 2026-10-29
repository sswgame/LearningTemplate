#include "pch.h"

#include "Core/Container/unordered_map.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Object/Component/3D/MeshInstanceBatch.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"

using namespace sw;

// PrimitiveRegistryTest — 프리미티브 레지스트리의 인스턴스 배치 등록 · 해제.

namespace
{
    struct TestPrimitiveRegistryInternal
    {
        /** @brief 배치 하나의 항목 수입니다(1 ~ 19 개 — 평균 10 개라 배치 천 개가 항목 약 1 만 개다). */
        static uint32 entryCountOf( uint32 batchIndex ) { return 1u + batchIndex % 19u; }

        /** @brief 이 번호의 배치를 지우는지 정합니다(셋 중 하나와 100 ~ 199 번 연속 구간). */
        static bool isRemoved( uint32 batchIndex ) { return batchIndex % 3u == 0u || ( 100u <= batchIndex && batchIndex < 200u ); }

        /** @brief 남은 배치마다 첫 자리가 유효하고 항목 목록이 그 배치를 가리키는지 확인합니다. */
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
            return expected == listEntry.size();
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
        listBulkBatch.push_back( make_unique<MeshInstanceBatch>( nullptr, nullptr, nullptr, TestPrimitiveRegistryInternal::entryCountOf( batchIndex ), MeshInstanceWriter::CPU ) );
        listSingleBatch.push_back( make_unique<MeshInstanceBatch>( nullptr, nullptr, nullptr, TestPrimitiveRegistryInternal::entryCountOf( batchIndex ), MeshInstanceWriter::CPU ) );
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

    // 지운 배치는 다시 지워도 아무 일이 없고, 같은 레지스트리에 다시 등록할 수 있다.
    const uint32 slotCount = bulkRegistry.getSlotCount();
    bulkRegistry.removeInstanceBatches( listBulkRemoved );
    SW_EXPECT_EQUAL( slotCount, bulkRegistry.getSlotCount() );
    bulkRegistry.addInstanceBatch( listBulkBatch[0].get() );
    SW_EXPECT_TRUE( TestPrimitiveRegistryInternal::isConsistent( bulkRegistry, listBulkBatch ) );
    SW_EXPECT_EQUAL( slotCount + TestPrimitiveRegistryInternal::entryCountOf( 0 ), bulkRegistry.getSlotCount() );
}
