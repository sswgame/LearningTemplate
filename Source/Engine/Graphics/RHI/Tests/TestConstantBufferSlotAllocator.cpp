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
