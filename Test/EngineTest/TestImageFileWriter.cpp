#include "pch.h"

#include "Core/String/StringUtil.h"

#include "Engine/Resource/DdsLoader.h"
#include "Engine/Resource/ImageFileWriter.h"

#include "TestFramework/TestFramework.h"

// 그림 파일 쓰기 — 초상화(썸네일)를 DDS(엔진이 다시 읽는다) · PNG(사람이 본다)로 쓴다.

using namespace sw;

namespace
{
    struct ImageFileWriterTestInternal
    {
        /** @brief 3×2 RGBA8 — 픽셀마다 다른 값이라 행 · 열이 뒤바뀌면 드러난다. */
        static vector<uint8> makeImage()
        {
            vector<uint8> bytes;
            for ( uint32 index = 0; index < 6; ++index )
            {
                bytes.push_back( static_cast<uint8>( 10 + index ) );
                bytes.push_back( static_cast<uint8>( 100 + index ) );
                bytes.push_back( static_cast<uint8>( 200 + index ) );
                bytes.push_back( 255 );
            }
            return bytes;
        }

        static uint32 readUint32Be( const uint8* pBytes ) { return ( uint32{ pBytes[0] } << 24 ) | ( uint32{ pBytes[1] } << 16 ) | ( uint32{ pBytes[2] } << 8 ) | pBytes[3]; }
    };
} // namespace

/**
 * @brief [ImageFileWriterTest] PNG 는 서명 · 청크 CRC 가 맞고, 저장 블록을 풀면 행마다 필터 0 + 원래 픽셀이며, Adler-32 가 맞는다
 */
SW_TEST_CASE( ImageFileWriterTest, PngRoundTripsThroughStoredBlocks )
{
    // CRC 함수가 PNG 의 CRC-32 와 같은 것인지 알려진 값으로 먼저 본다(IEND 청크의 CRC 는 늘 AE426082).
    SW_ASSERT_EQUAL( 0xAE426082u, StringUtil::computeCrc32( "IEND", 4 ) );

    const vector<uint8> image = ImageFileWriterTestInternal::makeImage();
    vector<uint8>       png;
    ImageFileWriter::encodePngRgba8( image, 3, 2, png );
    SW_ASSERT_TRUE( png.size() > 8 + 25 + 12 );
    static constexpr uint8 kArrSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    SW_EXPECT_TRUE( Memory::compare( png.data(), kArrSignature, 8 ) == 0 );

    vector<uint8> zlib;
    size_t        offset   = 8;
    uint32        chunkCnt = 0;
    while ( offset + 12 <= png.size() )
    {
        const uint32 length = ImageFileWriterTestInternal::readUint32Be( png.data() + offset );
        const uint8* pType  = png.data() + offset + 4;
        SW_ASSERT_TRUE( offset + 12 + length <= png.size() );
        const uint32 crc = ImageFileWriterTestInternal::readUint32Be( png.data() + offset + 8 + length );
        SW_EXPECT_EQUAL( StringUtil::computeCrc32( pType, length + 4 ), crc );
        if ( Memory::compare( pType, "IHDR", 4 ) == 0 )
        {
            SW_EXPECT_EQUAL( 3u, ImageFileWriterTestInternal::readUint32Be( pType + 4 ) );
            SW_EXPECT_EQUAL( 2u, ImageFileWriterTestInternal::readUint32Be( pType + 8 ) );
            SW_EXPECT_EQUAL( 8u, uint32{ pType[12] } ); // 비트
            SW_EXPECT_EQUAL( 6u, uint32{ pType[13] } ); // RGBA
        }
        if ( Memory::compare( pType, "IDAT", 4 ) == 0 )
            zlib.insert( zlib.end(), pType + 4, pType + 4 + length );
        offset += 12 + length;
        ++chunkCnt;
    }
    SW_EXPECT_EQUAL( 3u, chunkCnt );
    SW_ASSERT_TRUE( zlib.size() > 2 + 5 + 4 );
    SW_EXPECT_EQUAL( 0x78u, uint32{ zlib[0] } );

    // 저장 블록 하나: BFINAL=1, LEN, NLEN, 원본.
    const uint32 storedLength = uint32{ zlib[3] } | ( uint32{ zlib[4] } << 8 );
    const uint32 storedInvert = uint32{ zlib[5] } | ( uint32{ zlib[6] } << 8 );
    SW_EXPECT_EQUAL( 1u, uint32{ zlib[2] } );
    SW_EXPECT_EQUAL( 2u * ( 1u + 3u * 4u ), storedLength );
    SW_EXPECT_EQUAL( 0xFFFFu, storedLength ^ storedInvert );
    const uint8* pRaw = zlib.data() + 7;
    for ( uint32 row = 0; row < 2; ++row )
    {
        SW_EXPECT_EQUAL( 0u, uint32{ pRaw[row * 13] } ); // 필터 0
        SW_EXPECT_TRUE( Memory::compare( pRaw + row * 13 + 1, image.data() + row * 12, 12 ) == 0 );
    }
    uint32 adlerA = 1;
    uint32 adlerB = 0;
    for ( uint32 index = 0; index < storedLength; ++index )
    {
        adlerA = ( adlerA + pRaw[index] ) % 65521u;
        adlerB = ( adlerB + adlerA ) % 65521u;
    }
    SW_EXPECT_EQUAL( ( adlerB << 16 ) | adlerA, ImageFileWriterTestInternal::readUint32Be( pRaw + storedLength ) );
}

/**
 * @brief [ImageFileWriterTest] DDS 는 엔진의 DDS 읽기(`DdsLoader`)가 같은 크기 · RGBA8 포맷 · 같은 픽셀로 다시 읽는다
 */
SW_TEST_CASE( ImageFileWriterTest, DdsIsReadBackByTheEngineLoader )
{
    const vector<uint8> image = ImageFileWriterTestInternal::makeImage();
    const string        path  = test::makeTempPath( "portrait.dds" );
    SW_ASSERT_TRUE( ImageFileWriter::writeDdsRgba8( path, image, 3, 2 ) );
    DdsImageData loaded;
    SW_ASSERT_TRUE( DdsLoader::loadFromFile( path, loaded ) );
    SW_EXPECT_EQUAL( 3u, loaded._width );
    SW_EXPECT_EQUAL( 2u, loaded._height );
    SW_EXPECT_EQUAL( 28u, loaded._dxgiFormat ); // DXGI_FORMAT_R8G8B8A8_UNORM
    SW_ASSERT_TRUE( loaded._bytes.size() >= image.size() );
    SW_EXPECT_TRUE( Memory::compare( loaded.getPixels(), image.data(), image.size() ) == 0 );
}
