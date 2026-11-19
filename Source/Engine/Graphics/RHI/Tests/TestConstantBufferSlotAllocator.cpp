// ConstantBufferSlotAllocator — 작은 상수 버퍼를 64 KiB 페이지의 등급 칸으로 모은다: 등급 고르기, 페이지를 채운 뒤 새 페이지, 돌려준 칸 재사용, 빈 페이지 닫기와 번호 재사용(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/ConstantBufferSlotAllocator.h"

using namespace sw;

/**
 * @brief [ConstantBufferSlotAllocatorTest] 등급은 담는 가장 작은 2 의 거듭제곱(256 ~ 4096 B)이고, 0 · 4096 초과는 칸이 없다(전용 버퍼)
 */
SW_TEST_CASE( ConstantBufferSlotAllocatorTest, SizeClassIsTheSmallestFittingPowerOfTwo )
{
    SW_EXPECT_EQUAL( 256u, ConstantBufferSlotAllocator::findSlotBytes( 1 ) );
    SW_EXPECT_EQUAL( 256u, ConstantBufferSlotAllocator::findSlotBytes( 256 ) );
    SW_EXPECT_EQUAL( 512u, ConstantBufferSlotAllocator::findSlotBytes( 257 ) );
    SW_EXPECT_EQUAL( 4096u, ConstantBufferSlotAllocator::findSlotBytes( 4096 ) );
    SW_EXPECT_EQUAL( 0u, ConstantBufferSlotAllocator::findSlotBytes( 4097 ) );
    SW_EXPECT_EQUAL( 0u, ConstantBufferSlotAllocator::findSlotBytes( 0 ) );

    ConstantBufferSlotAllocator allocator;
    bool                        bNewPage = false;
    SW_EXPECT_FALSE( allocator.allocate( 8192, bNewPage ).isValid() );
    SW_EXPECT_FALSE( bNewPage );
}

/**
 * @brief [ConstantBufferSlotAllocatorTest] 머티리얼 300 개(각 200 B → 256 B 칸)는 페이지 둘(64 KiB 에 256 칸)이고, 칸은 256 B 정렬이며 겹치지 않는다.
 *        결함이면 인스턴스마다 백엔드 할당이 하나씩 남아 Vulkan 할당 한도에 닿는다(R-4)
 */
SW_TEST_CASE( ConstantBufferSlotAllocatorTest, PacksManySmallBuffersIntoFewPages )
{
    ConstantBufferSlotAllocator allocator;
    vector<ConstantBufferSlot>  listSlot;
    uint32                      newPageCount = 0;
    for ( uint32 index = 0; index < 300; ++index )
    {
        bool                     bNewPage = false;
        const ConstantBufferSlot slot     = allocator.allocate( 200, bNewPage );
        SW_REQUIRE_TRUE( slot.isValid() );
        SW_EXPECT_EQUAL( 0u, slot._offset % ConstantBufferSlotAllocator::kMinSlotBytes );
        newPageCount += bNewPage ? 1u : 0u;
        listSlot.push_back( slot );
    }
    SW_EXPECT_EQUAL( 2u, newPageCount );
    SW_EXPECT_EQUAL( 2u, allocator.getOpenPageCount() );
    SW_EXPECT_EQUAL( 0u, listSlot[0]._pageIndex );
    SW_EXPECT_EQUAL( 0u, listSlot[0]._offset );
    SW_EXPECT_EQUAL( 256u, listSlot[1]._offset );
    SW_EXPECT_EQUAL( 1u, listSlot[256]._pageIndex ); // 257 번째가 새 페이지의 첫 칸
}

/**
 * @brief [ConstantBufferSlotAllocatorTest] 돌려준 칸을 다음 할당이 다시 쓰고, 페이지의 칸이 모두 돌아오면 페이지를 닫으며(부르는 쪽이 RHI 버퍼를 놓는다) 그 번호를 다음 새 페이지가 쓴다
 */
SW_TEST_CASE( ConstantBufferSlotAllocatorTest, ReusesSlotsAndClosesEmptyPages )
{
    ConstantBufferSlotAllocator allocator;
    bool                        bNewPage = false;
    const ConstantBufferSlot    first    = allocator.allocate( 600, bNewPage ); // 1024 B 등급
    const ConstantBufferSlot    second   = allocator.allocate( 600, bNewPage );
    SW_EXPECT_FALSE( allocator.release( first ) );
    const ConstantBufferSlot again = allocator.allocate( 1000, bNewPage );
    SW_EXPECT_FALSE( bNewPage );
    SW_EXPECT_EQUAL( first._offset, again._offset );

    SW_EXPECT_FALSE( allocator.release( second ) );
    SW_EXPECT_TRUE( allocator.release( again ) ); // 마지막 칸 — 페이지를 닫는다
    SW_EXPECT_EQUAL( 0u, allocator.getOpenPageCount() );

    const ConstantBufferSlot other = allocator.allocate( 300, bNewPage ); // 다른 등급(512 B)도 닫힌 번호를 다시 쓴다
    SW_EXPECT_TRUE( bNewPage );
    SW_EXPECT_EQUAL( first._pageIndex, other._pageIndex );
    SW_EXPECT_EQUAL( 512u, other._sizeBytes );
}

/**
 * @brief [ConstantBufferSlotAllocatorTest] 레이아웃이 커지면 칸을 큰 등급으로 옮기고(옛 칸은 놓는다), 같은 등급 안의 변화는 칸 그대로다(감사 R-4 ③ — 예전 "커지면 다시 만든다" 자리)
 */
SW_TEST_CASE( ConstantBufferSlotAllocatorTest, ReallocateMovesToTheFittingClass )
{
    ConstantBufferSlotAllocator allocator;
    bool                        bNewPage = false;
    const ConstantBufferSlot    small    = allocator.allocate( 200, bNewPage ); // 256 B 등급
    const ConstantBufferSlot    neighbor = allocator.allocate( 200, bNewPage ); // 같은 페이지를 붙들어 둔다

    bool                     bOldPageClosed = false;
    const ConstantBufferSlot same           = allocator.reallocate( small, 256, bNewPage, bOldPageClosed );
    SW_EXPECT_FALSE( bNewPage );
    SW_EXPECT_FALSE( bOldPageClosed );
    SW_EXPECT_EQUAL( small._pageIndex, same._pageIndex );
    SW_EXPECT_EQUAL( small._offset, same._offset );

    const ConstantBufferSlot grown = allocator.reallocate( small, 1500, bNewPage, bOldPageClosed ); // 2048 B 등급 — 새 페이지
    SW_REQUIRE_TRUE( grown.isValid() );
    SW_EXPECT_EQUAL( 2048u, grown._sizeBytes );
    SW_EXPECT_TRUE( bNewPage );
    SW_EXPECT_FALSE( bOldPageClosed ); // 이웃이 남아 256 B 페이지는 열려 있다
    SW_EXPECT_TRUE( grown._pageIndex != small._pageIndex );
    SW_EXPECT_EQUAL( 2u, allocator.getOpenPageCount() );

    // 옛 칸은 돌아왔다 — 다음 256 B 할당이 그 자리를 다시 쓴다.
    const ConstantBufferSlot reused = allocator.allocate( 100, bNewPage );
    SW_EXPECT_FALSE( bNewPage );
    SW_EXPECT_EQUAL( small._offset, reused._offset );
    (void)neighbor;
}

/**
 * @brief [ConstantBufferSlotAllocatorTest] 옮기며 옛 페이지의 마지막 칸을 놓으면 옛 페이지가 닫히고(부르는 쪽이 RHI 버퍼를 반납), 새 칸은 닫힌 번호를 받지 않는다.
 *        등급보다 커지면 무효 칸(전용 버퍼)이고 옛 칸은 놓인다
 */
SW_TEST_CASE( ConstantBufferSlotAllocatorTest, ReallocateReturnsTheEmptiedPage )
{
    ConstantBufferSlotAllocator allocator;
    bool                        bNewPage       = false;
    bool                        bOldPageClosed = false;
    const ConstantBufferSlot    only           = allocator.allocate( 300, bNewPage ); // 512 B 등급, 페이지 0 의 유일한 칸

    const ConstantBufferSlot grown = allocator.reallocate( only, 700, bNewPage, bOldPageClosed ); // 1024 B 등급
    SW_REQUIRE_TRUE( grown.isValid() );
    SW_EXPECT_TRUE( bNewPage );
    SW_EXPECT_TRUE( bOldPageClosed );
    SW_EXPECT_TRUE( grown._pageIndex != only._pageIndex ); // 닫힌 번호를 새 칸이 받으면 부르는 쪽이 새 버퍼를 놓아 버린다
    SW_EXPECT_EQUAL( 1u, allocator.getOpenPageCount() );

    const ConstantBufferSlot oversized = allocator.reallocate( grown, 8192, bNewPage, bOldPageClosed );
    SW_EXPECT_FALSE( oversized.isValid() );
    SW_EXPECT_FALSE( bNewPage );
    SW_EXPECT_TRUE( bOldPageClosed );
    SW_EXPECT_EQUAL( 0u, allocator.getOpenPageCount() );
}
