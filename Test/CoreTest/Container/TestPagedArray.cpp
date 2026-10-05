#include "pch.h"

#include "Core/Container/PagedArray.h"
#include "Core/Container/vector.h"
#include "Core/Memory/MemoryProfiler.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 만든 · 부순 횟수를 세는 원소입니다. */
    struct PagedArrayCountedElement
    {
        static inline int32 s_constructCount = 0;
        static inline int32 s_destructCount  = 0;

        PagedArrayCountedElement() { ++s_constructCount; }
        ~PagedArrayCountedElement() { ++s_destructCount; }

        uint64 _value{ 0 };
    };
} // namespace

/**
 * @brief [PagedArrayTest] 청크는 처음 닿을 때 생기고, 그 전에는 `find` 가 nullptr 을 돌려줍니다.
 */
SW_TEST_CASE( PagedArrayTest, FindIsNullUntilEnsured )
{
    PagedArray<uint64, 16, 4> pages;
    SW_EXPECT_EQUAL( 64ull, ( PagedArray<uint64, 16, 4>::kCapacity ) );
    SW_EXPECT_TRUE( pages.find( 0 ) == nullptr );
    SW_EXPECT_TRUE( pages.find( 63 ) == nullptr );

    uint64* pValue = pages.ensure( 20 );
    SW_ASSERT_TRUE( pValue != nullptr );
    SW_EXPECT_EQUAL( 0ull, *pValue ); // 새 청크는 값 초기화된다
    *pValue = 7;

    // 같은 청크(16..31)의 다른 칸은 이제 보이고, 다른 청크는 여전히 없다.
    SW_EXPECT_TRUE( pages.find( 16 ) != nullptr );
    SW_EXPECT_TRUE( pages.find( 31 ) != nullptr );
    SW_EXPECT_TRUE( pages.find( 15 ) == nullptr );
    SW_EXPECT_TRUE( pages.find( 32 ) == nullptr );
    SW_EXPECT_EQUAL( 7ull, *pages.find( 20 ) );
}

/**
 * @brief [PagedArrayTest] 범위 밖 인덱스는 `find` · `ensure` 모두 nullptr 입니다.
 */
SW_TEST_CASE( PagedArrayTest, OutOfRangeIsRejected )
{
    PagedArray<uint32, 8, 2> pages;
    SW_EXPECT_TRUE( ( PagedArray<uint32, 8, 2>::isInRange( 15 ) ) );
    SW_EXPECT_FALSE( ( PagedArray<uint32, 8, 2>::isInRange( 16 ) ) );
    SW_EXPECT_TRUE( pages.ensure( 16 ) == nullptr );
    SW_EXPECT_TRUE( pages.find( 16 ) == nullptr );
    SW_EXPECT_TRUE( pages.ensure( 15 ) != nullptr );
}

/**
 * @brief [PagedArrayTest] 청크를 더 붙여도 이미 돌려준 원소의 주소는 그대로입니다(이 컨테이너가 있는 이유).
 */
SW_TEST_CASE( PagedArrayTest, AddressesSurviveGrowth )
{
    PagedArray<uint32, 4, 64> pages;
    vector<uint32*>           listAddress;
    for ( uint32 index = 0; index < 256; ++index )
    {
        uint32* pValue = pages.ensure( index );
        SW_ASSERT_TRUE( pValue != nullptr );
        *pValue = index * 3;
        listAddress.push_back( pValue );
    }
    for ( uint32 index = 0; index < 256; ++index )
    {
        SW_EXPECT_TRUE( pages.find( index ) == listAddress[index] );
        SW_EXPECT_EQUAL( index * 3, *listAddress[index] );
    }
}

/**
 * @brief [PagedArrayTest] `forEachElement` 는 만들어진 청크만 돌고, `releaseChunks` 뒤에는 아무것도 없습니다.
 */
SW_TEST_CASE( PagedArrayTest, ForEachVisitsAllocatedChunksOnly )
{
    PagedArray<uint32, 8, 8> pages;
    *pages.ensure( 3 )  = 1;
    *pages.ensure( 42 ) = 1;

    uint32 visitCount = 0;
    uint32 sum        = 0;
    pages.forEachElement( [&visitCount, &sum]( uint32& value )
    {
        ++visitCount;
        sum += value;
    } );
    SW_EXPECT_EQUAL( 16u, visitCount ); // 청크 둘(0..7, 40..47) × 8 칸
    SW_EXPECT_EQUAL( 2u, sum );

    pages.releaseChunks();
    SW_EXPECT_TRUE( pages.find( 3 ) == nullptr );
    SW_EXPECT_TRUE( pages.find( 42 ) == nullptr );
    visitCount = 0;
    pages.forEachElement( [&visitCount]( uint32& )
    { ++visitCount; } );
    SW_EXPECT_EQUAL( 0u, visitCount );
}

/**
 * @brief [PagedArrayTest] 청크의 원소는 만들 때 하나씩 생성되고 `releaseChunks` 에서 하나씩 소멸합니다.
 */
SW_TEST_CASE( PagedArrayTest, ReleaseDestroysEveryElementOfEveryChunk )
{
    PagedArrayCountedElement::s_constructCount = 0;
    PagedArrayCountedElement::s_destructCount  = 0;
    {
        PagedArray<PagedArrayCountedElement, 8, 4> pages;
        SW_ASSERT_TRUE( pages.ensure( 3 ) != nullptr );
        SW_ASSERT_TRUE( pages.ensure( 17 ) != nullptr );
        SW_EXPECT_EQUAL( 16, PagedArrayCountedElement::s_constructCount );
        SW_EXPECT_EQUAL( 0, PagedArrayCountedElement::s_destructCount );
        pages.releaseChunks();
        SW_EXPECT_EQUAL( 16, PagedArrayCountedElement::s_destructCount );
        SW_EXPECT_TRUE( pages.find( 3 ) == nullptr );
    }
    SW_EXPECT_EQUAL( 16, PagedArrayCountedElement::s_destructCount );
}

/**
 * @brief [PagedArrayTest] 청크는 sw 할당자로 잡혀 그때의 메모리 태그로 세이고, 놓으면 빠집니다.
 * @details 청크를 CRT `new[]` 로 잡으면 태그 줄이 움직이지 않습니다(sw 할당자 밖 몫이 됩니다).
 */
SW_TEST_CASE( PagedArrayTest, ChunkIsCountedUnderTheCurrentMemoryTag )
{
    if constexpr ( kMemoryTagScopesEnabled == false )
        SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
    const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
    if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
        SW_TEST_SKIP( "no tracking memory profiler in this host" );

    constexpr uint64 kChunkBytes = 1024 * sizeof( uint64 );
    const uint64     before      = pProfiler->getStats( MemoryTag::Physics )._currentAllocatedBytes.load();
    uint64           held{ 0 };
    {
        PagedArray<uint64, 1024, 4> pages;
        {
            SW_MEMORY_SCOPE( Physics );
            SW_ASSERT_TRUE( pages.ensure( 5 ) != nullptr );
        }
        held = pProfiler->getStats( MemoryTag::Physics )._currentAllocatedBytes.load();
    }
    const uint64 after = pProfiler->getStats( MemoryTag::Physics )._currentAllocatedBytes.load();
    SW_EXPECT_TRUE_MSG( held >= before + kChunkBytes, ( string( "Physics bytes grew by " ) + to_string( held - before ) ).c_str() );
    SW_EXPECT_TRUE( after < before + kChunkBytes );
}
