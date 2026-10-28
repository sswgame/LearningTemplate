#include "Core/Math/BitUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

SW_TEST_CASE( BitUtilTest, PackAndGetBytesAreInverse )
{
    const uint32 packed = BitUtil::packBytes( 0x12u, 0x34u, 0x56u, 0x78u );
    SW_EXPECT_EQUAL( 0x12345678u, packed );
    SW_EXPECT_EQUAL( 0x78u, BitUtil::getByte( packed, 0 ) );
    SW_EXPECT_EQUAL( 0x12u, BitUtil::getByte( packed, 3 ) );
}

SW_TEST_CASE( BitUtilTest, FloatBitsRoundTrip )
{
    SW_EXPECT_EQUAL( 0x3F800000u, BitUtil::floatToBits( 1.0f ) );
    SW_EXPECT_EQUAL( -2.5f, BitUtil::bitsToFloat( BitUtil::floatToBits( -2.5f ) ) );
}
