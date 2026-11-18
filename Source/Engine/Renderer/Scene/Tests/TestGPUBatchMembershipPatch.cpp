// GPUBatchMembershipPatchTest — 배치 구성원만 고치는 차례 다시 쓰기: 빠진 칸은 빼고 들어온 칸은 그 배치 끝에 붙이며, 뒤 배치의 차례 · 가시 구간이 밀리고 LOD 단계 배치는 머리만큼의 가시 구간을 받는다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Renderer/Scene/GPUBatchMembershipPatch.h"

using namespace sw;

namespace
{
    struct MembershipPatchTestUtil
    {
        /** @brief 차례 구간 [@p base, @p base + @p count) 의 배치입니다(가시 구간도 같다). */
        static GPUMeshBatch makeBatch( uint32 base, uint32 count, uint32 lodLevelCount = 0u )
        {
            GPUMeshBatch batch{};
            batch._instanceBase    = base;
            batch._instanceCount   = count;
            batch._visibleBase     = base;
            batch._visibleCapacity = count;
            batch._lodLevelCount   = lodLevelCount;
            return batch;
        }
    };
} // namespace

/**
 * @brief [GPUBatchMembershipPatchTest] 앞 배치에서 하나 빠지고 뒤 배치에 둘 들어오면 각 배치의 구성원만 바뀌고 뒤 배치의 구간이 밀린다
 * @details 칸 → 차례 표도 새 차례를 가리켜야 한다(메시 바꾸기 · VFX 메시 칸 찾기가 그 표를 읽는다).
 */
SW_TEST_CASE( GPUBatchMembershipPatchTest, LeaveAndEnterRewriteOnlyMembers )
{
    using Util = MembershipPatchTestUtil;
    // 배치 0 = 칸 {4, 1, 7}, 배치 1 = 칸 {2, 5}. 칸 1 이 빠지고 칸 9 · 칸 0 이 배치 1 에 들어온다(넣는 순서와 무관하게 칸 순으로 붙는다).
    vector<GPUMeshBatch>    listBatch{ Util::makeBatch( 0u, 3u ), Util::makeBatch( 3u, 2u ) };
    const vector<uint32>    listOldOrder{ 4u, 1u, 7u, 2u, 5u };
    GPUBatchMembershipPatch patch;
    patch.reset( 10u );
    patch.markLeave( 1u );
    patch.addEnter( 1u, 9u );
    patch.addEnter( 1u, 0u );
    vector<uint32> listOrder;
    vector<uint32> listSlotToOrder( 10u, constant::invalid_index::kUint32 );
    patch.rewriteOrder( listBatch.data(), 2u, 2u, listOldOrder, listOrder, listSlotToOrder );

    const vector<uint32> listExpected{ 4u, 7u, 2u, 5u, 0u, 9u };
    SW_REQUIRE_TRUE( listOrder == listExpected );
    SW_EXPECT_EQUAL( 0u, listBatch[0]._instanceBase );
    SW_EXPECT_EQUAL( 2u, listBatch[0]._instanceCount );
    SW_EXPECT_EQUAL( 2u, listBatch[1]._instanceBase );
    SW_EXPECT_EQUAL( 4u, listBatch[1]._instanceCount );
    SW_EXPECT_EQUAL( 2u, listBatch[1]._visibleBase );
    SW_EXPECT_EQUAL( 4u, listBatch[1]._visibleCapacity );
    SW_EXPECT_EQUAL( 5u, listSlotToOrder[9] );
    SW_EXPECT_EQUAL( 1u, listSlotToOrder[7] );
    SW_EXPECT_EQUAL( constant::invalid_index::kUint32, listSlotToOrder[1] ); // 빠지기만 한 칸은 건드리지 않는다(부르는 쪽이 무효로 둔다)
}

/**
 * @brief [GPUBatchMembershipPatchTest] 키가 바뀌어 빠짐 · 들어옴이 둘 다인 칸은 옛 자리에서 빠지고 새 배치 끝에 한 번만 선다
 */
SW_TEST_CASE( GPUBatchMembershipPatchTest, KeyChangedSlotMovesToItsNewBatch )
{
    using Util = MembershipPatchTestUtil;
    vector<GPUMeshBatch>    listBatch{ Util::makeBatch( 0u, 2u ), Util::makeBatch( 2u, 1u ) };
    const vector<uint32>    listOldOrder{ 0u, 1u, 2u };
    GPUBatchMembershipPatch patch;
    patch.reset( 3u );
    patch.markLeave( 1u );
    patch.addEnter( 1u, 1u );
    SW_EXPECT_EQUAL( static_cast<uint32>( GPUBatchMembershipPatch::kLeave | GPUBatchMembershipPatch::kEnter ), static_cast<uint32>( patch.getFlags()[1] ) );
    vector<uint32> listOrder;
    vector<uint32> listSlotToOrder( 3u, 0u );
    patch.rewriteOrder( listBatch.data(), 2u, 2u, listOldOrder, listOrder, listSlotToOrder );

    const vector<uint32> listExpected{ 0u, 2u, 1u };
    SW_REQUIRE_TRUE( listOrder == listExpected );
    SW_EXPECT_EQUAL( 1u, listBatch[0]._instanceCount );
    SW_EXPECT_EQUAL( 2u, listBatch[1]._instanceCount );
    SW_EXPECT_EQUAL( 2u, listSlotToOrder[1] );
}

/**
 * @brief [GPUBatchMembershipPatchTest] LOD 그룹 머리에 들어오면 뒤의 단계 배치는 인스턴스 구간이 비고 가시 구간이 머리만큼 늘며, 그 뒤 배치의 가시 구간이 밀린다
 * @details 컬링은 단계 배치의 가시 구간에 그 단계로 고른 인스턴스를 쓴다 — 머리보다 짧으면 넘친 인스턴스가 버려진다.
 */
SW_TEST_CASE( GPUBatchMembershipPatchTest, LODLevelBatchesFollowTheirHead )
{
    using Util = MembershipPatchTestUtil;
    // 배치 0 = LOD 머리(칸 {0, 1}), 배치 1 = 단계 배치(인스턴스 없음, 가시 2), 배치 2 = 보통 배치(칸 {2}).
    vector<GPUMeshBatch> listBatch{ Util::makeBatch( 0u, 2u, 2u ), Util::makeBatch( 2u, 0u ), Util::makeBatch( 2u, 1u ) };
    listBatch[1]._visibleBase     = 2u;
    listBatch[1]._visibleCapacity = 2u;
    listBatch[2]._visibleBase     = 4u;
    const vector<uint32>    listOldOrder{ 0u, 1u, 2u };
    GPUBatchMembershipPatch patch;
    patch.reset( 4u );
    patch.addEnter( 0u, 3u );
    vector<uint32> listOrder;
    vector<uint32> listSlotToOrder( 4u, 0u );
    patch.rewriteOrder( listBatch.data(), 3u, 3u, listOldOrder, listOrder, listSlotToOrder );

    SW_EXPECT_EQUAL( 3u, listBatch[0]._instanceCount );
    SW_EXPECT_EQUAL( 3u, listBatch[1]._instanceBase );
    SW_EXPECT_EQUAL( 0u, listBatch[1]._instanceCount );
    SW_EXPECT_EQUAL( 3u, listBatch[1]._visibleBase );
    SW_EXPECT_EQUAL( 3u, listBatch[1]._visibleCapacity );
    SW_EXPECT_EQUAL( 3u, listBatch[2]._instanceBase );
    SW_EXPECT_EQUAL( 6u, listBatch[2]._visibleBase );
    SW_EXPECT_EQUAL( 1u, listBatch[2]._visibleCapacity );
}

/**
 * @brief [GPUBatchMembershipPatchTest] 처음 보는 키의 칸은 불투명 끝에 새로 붙인 배치로 들어가고 앞 배치의 구간 · 번호는 그대로다(다시 나누지 않는다)
 * @details 새 배치는 빈 구간으로 붙는다 — 새 배치 시작(`freshBatchBegin`)을 넘기지 않으면 빈 배치를 LOD 단계 배치로 읽어 가시 구간을 앞 머리만큼 잡는다(음성 대조).
 */
SW_TEST_CASE( GPUBatchMembershipPatchTest, FreshBatchAppendsWithoutShiftingOthers )
{
    using Util = MembershipPatchTestUtil;
    // 배치 0 = 칸 {0, 1}, 배치 1(새로 붙임, 빈 구간) ← 칸 1 이 키를 바꿔 옮기고 칸 2 가 새로 들어온다.
    const vector<uint32> listOldOrder{ 0u, 1u };
    auto                 run = [&listOldOrder]( uint32 freshBatchBegin, vector<GPUMeshBatch>& outListBatch, vector<uint32>& outListOrder )
    {
        outListBatch = { Util::makeBatch( 0u, 2u ), Util::makeBatch( 2u, 0u ) };
        GPUBatchMembershipPatch patch;
        patch.reset( 3u );
        patch.markLeave( 1u );
        patch.addEnter( 1u, 2u );
        patch.addEnter( 1u, 1u );
        vector<uint32> listSlotToOrder( 3u, 0u );
        patch.rewriteOrder( outListBatch.data(), 2u, freshBatchBegin, listOldOrder, outListOrder, listSlotToOrder );
    };
    vector<GPUMeshBatch> listBatch;
    vector<uint32>       listOrder;
    run( 1u, listBatch, listOrder );
    const vector<uint32> listExpected{ 0u, 1u, 2u };
    SW_REQUIRE_TRUE( listOrder == listExpected );
    SW_EXPECT_EQUAL( 0u, listBatch[0]._instanceBase );
    SW_EXPECT_EQUAL( 1u, listBatch[0]._instanceCount );
    SW_EXPECT_EQUAL( 1u, listBatch[1]._instanceBase );
    SW_EXPECT_EQUAL( 2u, listBatch[1]._instanceCount );
    SW_EXPECT_EQUAL( 1u, listBatch[1]._visibleBase );
    SW_EXPECT_EQUAL( 2u, listBatch[1]._visibleCapacity );

    // 음성 대조 — 새 배치라고 알리지 않으면 빈 구간을 LOD 단계로 읽어 가시 구간이 앞 머리(1)만큼이다.
    run( 2u, listBatch, listOrder );
    SW_EXPECT_EQUAL( 1u, listBatch[1]._visibleCapacity );
}
