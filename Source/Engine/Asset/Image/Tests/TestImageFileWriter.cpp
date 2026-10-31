#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Image/ImageFileWriter.h"
#include "Engine/Compression/ZlibCompressionCodec.h"

using namespace sw;

namespace
{
    uint32 readUint32Be( const vector<uint8>& bytes, size_t offset )
    {
        return ( static_cast<uint32>( bytes[offset] ) << 24 ) | ( static_cast<uint32>( bytes[offset + 1] ) << 16 ) | ( static_cast<uint32>( bytes[offset + 2] ) << 8 ) |
               static_cast<uint32>( bytes[offset + 3] );
    }
} // namespace

// PNG 는 청크마다 CRC 가 맞고, IDAT 를 풀면 행마다 필터 0 + 원본 RGBA 다(zlib 코덱으로 압축한 스트림을 그대로 되돌린다).
SW_TEST_CASE( ImageFileWriterTest, PNGRoundTripsThroughZlib )
{
    constexpr uint32 kWidth  = 3;
    constexpr uint32 kHeight = 2;
    vector<uint8>    rgbaBytes( kWidth * kHeight * 4u );
    for ( size_t index = 0; index < rgbaBytes.size(); ++index )
        rgbaBytes[index] = static_cast<uint8>( index * 7u );

    vector<uint8> pngBytes;
    ImageFileWriter::encodePNGRGBA8( rgbaBytes, kWidth, kHeight, pngBytes );
    SW_REQUIRE_TRUE( pngBytes.size() > 8 );
    SW_EXPECT_TRUE( pngBytes[0] == 0x89 && pngBytes[1] == 'P' && pngBytes[2] == 'N' && pngBytes[3] == 'G' );

    vector<uint8> idatBytes;
    size_t        offset = 8;
    string        lastType;
    while ( offset + 12 <= pngBytes.size() )
    {
        const uint32 length = readUint32Be( pngBytes, offset );
        SW_REQUIRE_TRUE( offset + 12 + length <= pngBytes.size() );
        lastType.assign( reinterpret_cast<const char*>( pngBytes.data() + offset + 4 ), 4 );
        SW_EXPECT_EQUAL( StringUtil::computeCRC32( pngBytes.data() + offset + 4, length + 4 ), readUint32Be( pngBytes, offset + 8 + length ) );
        if ( lastType == "IHDR" )
        {
            SW_EXPECT_EQUAL( readUint32Be( pngBytes, offset + 8 ), kWidth );
            SW_EXPECT_EQUAL( readUint32Be( pngBytes, offset + 12 ), kHeight );
        }
        if ( lastType == "IDAT" )
            idatBytes.insert( idatBytes.end(), pngBytes.begin() + static_cast<ptrdiff_t>( offset + 8 ), pngBytes.begin() + static_cast<ptrdiff_t>( offset + 8 + length ) );
        offset += 12 + length;
    }
    SW_EXPECT_TRUE( lastType == "IEND" );
    SW_EXPECT_EQUAL( offset, pngBytes.size() );

    ZlibCompressionCodec codec;
    const size_t         rowBytes = kWidth * 4u;
    vector<uint8>        rawBytes( ( rowBytes + 1 ) * kHeight );
    size_t               rawSize{ 0 };
    SW_REQUIRE_TRUE( codec.decompress( idatBytes.data(), idatBytes.size(), rawBytes.data(), rawBytes.size(), rawSize ) );
    SW_REQUIRE_EQUAL( rawSize, rawBytes.size() );
    for ( uint32 row = 0; row < kHeight; ++row )
    {
        const uint8* pRaw = rawBytes.data() + row * ( rowBytes + 1 );
        SW_EXPECT_EQUAL( pRaw[0], uint8{ 0 } );
        SW_EXPECT_TRUE( std::equal( pRaw + 1, pRaw + 1 + rowBytes, rgbaBytes.begin() + static_cast<ptrdiff_t>( row * rowBytes ) ) );
    }
}
