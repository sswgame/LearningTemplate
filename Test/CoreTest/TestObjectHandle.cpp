#include "pch.h"

#include "Core/Container/HandleTable.h"
#include "Core/Container/ObjectHandle.h"

#include "TestFramework.h"

using namespace sw;

SW_TEST_CASE( ObjectHandle, PackedRoundTripAndInvalid )
{
	ObjectHandle invalid;
	SW_EXPECT_FALSE( invalid.isValid() );
	SW_EXPECT_EQUAL( 0u, invalid.packed() );

	const ObjectHandle handle = ObjectHandle::make( 7u, 3u );
	SW_EXPECT_TRUE( handle.isValid() );
	SW_EXPECT_EQUAL( 7u, handle.index() );
	SW_EXPECT_EQUAL( 3u, handle.generation() );
	SW_EXPECT_EQUAL( handle, ObjectHandle::fromPacked( handle.packed() ) );
	SW_EXPECT_NOT_EQUAL( handle, ObjectHandle::make( 7u, 4u ) );
}

SW_TEST_CASE( HandleTable, GenerationInvalidatesStaleHandles )
{
	HandleTable<uint32> table;
	const ObjectHandle	first = table.insert( 42u );
	SW_EXPECT_TRUE( first.isValid() );
	uint32* slot = table.get( first );
	SW_ASSERT_NOT_NULL( slot );
	SW_EXPECT_EQUAL( 42u, *slot );

	uint32 taken{ 0 };
	SW_EXPECT_TRUE( table.take( first, taken ) );
	SW_EXPECT_EQUAL( 42u, taken );
	SW_EXPECT_TRUE( table.get( first ) == nullptr );

	const ObjectHandle second = table.insert( 99u );
	SW_EXPECT_EQUAL( first.index(), second.index() );
	SW_EXPECT_NOT_EQUAL( first, second );
	SW_EXPECT_TRUE( table.get( first ) == nullptr );
	uint32* reused = table.get( second );
	SW_ASSERT_NOT_NULL( reused );
	SW_EXPECT_EQUAL( 99u, *reused );
}
