// IDAllocator — 0 을 내주지 않고 1 부터 단조로 늘며, enum class id 로도 쓰고, 저장한 다음 값으로 되돌리는지.
#include "pch.h"

#include "Core/Container/IDAllocator.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    enum class TestTicketID : uint32
    {
    };
} // namespace

SW_TEST_CASE( IDAllocatorTest, StartsAtOneAndNeverRepeats )
{
    IDAllocator<uint32> allocator;
    const uint32        first  = allocator.allocate();
    const uint32        second = allocator.allocate();
    SW_EXPECT_EQUAL( 1u, first );
    SW_EXPECT_EQUAL( 2u, second );
    SW_EXPECT_EQUAL( 3u, allocator.getNextValue() );
}

SW_TEST_CASE( IDAllocatorTest, TypedIDRestoresSavedNextValue )
{
    IDAllocator<TestTicketID> allocator;
    SW_EXPECT_TRUE( allocator.allocate() != IDAllocator<TestTicketID>::kInvalid );
    const uint32 savedNext = allocator.getNextValue();

    IDAllocator<TestTicketID> reloaded;
    reloaded.restore( savedNext );
    SW_EXPECT_EQUAL( savedNext, static_cast<uint32>( reloaded.allocate() ) );
}
