#include "pch.h"

#include "Core/Container/LittleEndianUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/String/FourCcUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Image/DDSFormat.h"
#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Asset/Image/ImageFileWriter.h"

using namespace sw;

// DDSLoaderTest — DDS 머리 읽기: 쓰는 쪽(ImageFileWriter)과의 DX10 왕복, 옛 머리의 FourCC · D3DFMT 정수 · RGB 마스크가 가리키는 DXGI 형식,
// 0 인 깊이 · 밉 수, 실패(짧음 · 마법 수 · 머리 크기 · 모르는 형식 · DX10 머리 잘림 · 픽셀 없음)는 출력을 비운다. 케이스 폴더에 파일 하나를 쓴다(nogpu).

namespace
{
    constexpr uint32 kWidth  = 4;
    constexpr uint32 kHeight = 2;

    struct LegacyPixelFormat
    {
        uint32 _flags;
        uint32 _fourCC;
        uint32 _bitCount;
        uint32 _rMask;
        uint32 _gMask;
        uint32 _bMask;
        uint32 _aMask;
    };

    /** @brief 옛 머리(DX10 확장 없음) DDS 를 만듭니다. 머리 뒤에 @p payloadSize 바이트를 붙입니다. */
    vector<uint8> makeLegacyDDS( const LegacyPixelFormat& pixelFormat, size_t payloadSize, uint32 depth = 0, uint32 mipCount = 0 )
    {
        vector<uint8> bytes;
        LittleEndianUtil::appendUint32( bytes, DDSFormat::kMagic );
        LittleEndianUtil::appendUint32( bytes, DDSFormat::kHeaderSize );
        LittleEndianUtil::appendUint32( bytes, 0 ); // 플래그
        LittleEndianUtil::appendUint32( bytes, kHeight );
        LittleEndianUtil::appendUint32( bytes, kWidth );
        LittleEndianUtil::appendUint32( bytes, 0 ); // 행 바이트 수
        LittleEndianUtil::appendUint32( bytes, depth );
        LittleEndianUtil::appendUint32( bytes, mipCount );
        for ( uint32 index = 0; index < 11; ++index )
        {
            LittleEndianUtil::appendUint32( bytes, 0 );
        }
        LittleEndianUtil::appendUint32( bytes, DDSFormat::kPixelFormatSize );
        LittleEndianUtil::appendUint32( bytes, pixelFormat._flags );
        LittleEndianUtil::appendUint32( bytes, pixelFormat._fourCC );
        LittleEndianUtil::appendUint32( bytes, pixelFormat._bitCount );
        LittleEndianUtil::appendUint32( bytes, pixelFormat._rMask );
        LittleEndianUtil::appendUint32( bytes, pixelFormat._gMask );
        LittleEndianUtil::appendUint32( bytes, pixelFormat._bMask );
        LittleEndianUtil::appendUint32( bytes, pixelFormat._aMask );
        for ( uint32 index = 0; index < 5; ++index )
        {
            LittleEndianUtil::appendUint32( bytes, 0 ); // caps · caps2 · caps3 · caps4 · 예약
        }
        bytes.resize( bytes.size() + payloadSize, 0x5A );
        return bytes;
    }

    LegacyPixelFormat makeFourCc( uint32 fourCC )
    {
        return LegacyPixelFormat{ DDSFormat::kPixelFormatFourCcFlag, fourCC, 0, 0, 0, 0, 0 };
    }

    LegacyPixelFormat makeRGB( uint32 rMask, uint32 gMask, uint32 bMask, uint32 aMask )
    {
        return LegacyPixelFormat{ DDSFormat::kPixelFormatRGBFlag, 0, 32, rMask, gMask, bMask, aMask };
    }

    /** @brief 성공한 읽기로 채워 둔 출력입니다. 실패한 읽기가 이것을 비우는지 봅니다. */
    DDSImageData makeLoadedImage()
    {
        const vector<uint8> bytes = makeLegacyDDS( makeFourCc( FourCcUtil::make( "DXT1" ) ), 8 );
        DDSImageData        image;
        SW_EXPECT_TRUE( DDSLoader::loadFromMemory( bytes.data(), bytes.size(), image ) );
        return image;
    }
} // namespace

/**
 * @brief [DDSLoaderTest] 쓰는 쪽이 낸 DX10 머리 RGBA8 파일을 읽으면 크기 · 형식 28 · 픽셀이 그대로이고, 0 으로 쓴 깊이는 1 이다
 */
SW_TEST_CASE( DDSLoaderTest, ReadsWhatImageFileWriterWrites )
{
    vector<uint8> rgbaBytes( kWidth * kHeight * 4u );
    for ( size_t index = 0; index < rgbaBytes.size(); ++index )
    {
        rgbaBytes[index] = static_cast<uint8>( index * 9u );
    }
    const string path = test::makeScratchPath( "image.dds" );
    SW_REQUIRE_TRUE( ImageFileWriter::writeDDSRGBA8( path, rgbaBytes, kWidth, kHeight ) );
    vector<uint8> fileBytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( path, fileBytes ) );

    DDSImageData image;
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( fileBytes.data(), fileBytes.size(), image ) );
    SW_EXPECT_TRUE( image.isValid() );
    SW_EXPECT_EQUAL( kWidth, image._width );
    SW_EXPECT_EQUAL( kHeight, image._height );
    SW_EXPECT_EQUAL( 1u, image._depth );
    SW_EXPECT_EQUAL( 1u, image._mipCount );
    SW_EXPECT_EQUAL( 28u, image._dxgiFormat );
    SW_EXPECT_TRUE( image._bCompressed == SW_FALSE );
    SW_EXPECT_TRUE( image._bIsBgra == SW_FALSE );
    SW_REQUIRE_EQUAL( rgbaBytes.size(), image._bytes.size() );
    SW_EXPECT_TRUE( image._bytes == rgbaBytes );
}

/**
 * @brief [DDSLoaderTest] 옛 머리의 FourCC · D3DFMT 정수 · RGB 마스크는 표의 DXGI 형식이 되고, BC 형식은 압축 · B8G8R8 형식은 BGRA 로 표시된다
 */
SW_TEST_CASE( DDSLoaderTest, LegacyHeadersMapToDXGIFormats )
{
    struct Case
    {
        LegacyPixelFormat _pixelFormat;
        uint32            _dxgiFormat;
        bool              _bCompressed;
        bool              _bBgra;
    };
    const Case arrCase[] = {
        { makeFourCc( FourCcUtil::make( "DXT1" ) ), 71, true, false },
        { makeFourCc( FourCcUtil::make( "DXT5" ) ), 77, true, false },
        { makeFourCc( FourCcUtil::make( "ATI2" ) ), 83, true, false },
        { makeFourCc( 113 ), 10, false, false }, // D3DFMT_A16B16G16R16F
        { makeFourCc( 114 ), 41, false, false }, // D3DFMT_R32F
        { makeRGB( 0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u ), 28, false, false },
        { makeRGB( 0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0x00000000u ), 28, false, false },
        { makeRGB( 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u ), 87, false, true },
        { makeRGB( 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0x00000000u ), 88, false, true },
    };
    for ( const Case& testCase : arrCase )
    {
        const vector<uint8> bytes = makeLegacyDDS( testCase._pixelFormat, 16 );
        DDSImageData        image;
        SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( bytes.data(), bytes.size(), image ) );
        SW_EXPECT_EQUAL( testCase._dxgiFormat, image._dxgiFormat );
        SW_EXPECT_EQUAL( testCase._bCompressed, image._bCompressed == SW_TRUE );
        SW_EXPECT_EQUAL( testCase._bBgra, image._bIsBgra == SW_TRUE );
        SW_EXPECT_EQUAL( size_t( 16 ), image._bytes.size() );
    }

    // 표를 돌려주는 함수는 읽기가 쓰는 표 그대로다
    SW_EXPECT_EQUAL( size_t( 15 ), DDSLoader::getLegacyFourCcTable().size() );
    SW_EXPECT_EQUAL( size_t( 4 ), DDSLoader::getLegacyRGBMaskTable().size() );
}

/**
 * @brief [DDSLoaderTest] 머리에 적은 깊이 · 밉 수는 그대로 읽고, 0 이면 1 로 본다
 */
SW_TEST_CASE( DDSLoaderTest, KeepsDepthAndMipCount )
{
    const vector<uint8> bytes = makeLegacyDDS( makeFourCc( FourCcUtil::make( "DXT1" ) ), 8, 3, 5 );
    DDSImageData        image;
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( bytes.data(), bytes.size(), image ) );
    SW_EXPECT_EQUAL( 3u, image._depth );
    SW_EXPECT_EQUAL( 5u, image._mipCount );
}

/**
 * @brief [DDSLoaderTest] 짧은 버퍼 · 틀린 마법 수 · 틀린 머리 크기 · DX10 머리 잘림 · 픽셀 없음은 실패이고, 앞서 채운 출력도 비운다
 */
SW_TEST_CASE( DDSLoaderTest, MalformedBuffersFailAndClearTheOutput )
{
    DDSImageData image = makeLoadedImage();
    SW_REQUIRE_TRUE( image.isValid() );

    const vector<uint8> tooShort( 16 );
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( tooShort.data(), tooShort.size(), image ) );
    SW_EXPECT_FALSE( image.isValid() );
    SW_EXPECT_EQUAL( 0u, image._width );
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( nullptr, 512, image ) );

    vector<uint8> badMagic = makeLegacyDDS( makeFourCc( FourCcUtil::make( "DXT1" ) ), 8 );
    badMagic[0]            = 'X';
    image                  = makeLoadedImage();
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( badMagic.data(), badMagic.size(), image ) );
    SW_EXPECT_FALSE( image.isValid() );

    vector<uint8> badHeaderSize = makeLegacyDDS( makeFourCc( FourCcUtil::make( "DXT1" ) ), 8 );
    badHeaderSize[4]            = 100; // 머리 크기 124 → 100
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( badHeaderSize.data(), badHeaderSize.size(), image ) );

    const vector<uint8> truncatedDx10 = makeLegacyDDS( makeFourCc( DDSFormat::kDx10FourCc ), 0 );
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( truncatedDx10.data(), truncatedDx10.size(), image ) );

    const vector<uint8> headerOnly = makeLegacyDDS( makeFourCc( FourCcUtil::make( "DXT1" ) ), 0 );
    image                          = makeLoadedImage();
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( headerOnly.data(), headerOnly.size(), image ) );
    SW_EXPECT_FALSE( image.isValid() );

    SW_EXPECT_LOG( "DDS.Truncated" );
    SW_EXPECT_LOG( "DDS.InvalidMagic" );
    SW_EXPECT_LOG( "DDS.CorruptHeader" );
}

/**
 * @brief [DDSLoaderTest] 표에 없는 FourCC · RGB 마스크, 형식 플래그가 없는 머리는 알아보지 못한 형식으로 실패한다(형식 0 을 성공으로 내보내지 않는다)
 */
SW_TEST_CASE( DDSLoaderTest, UnknownFormatsFail )
{
    const LegacyPixelFormat arrPixelFormat[] = {
        makeFourCc( FourCcUtil::make( "ABCD" ) ),
        makeRGB( 0x0000FFFFu, 0x00FF0000u, 0xFF000000u, 0x00000000u ),
        LegacyPixelFormat{ 0, 0, 0, 0, 0, 0, 0 },
    };
    for ( const LegacyPixelFormat& pixelFormat : arrPixelFormat )
    {
        const vector<uint8> bytes = makeLegacyDDS( pixelFormat, 16 );
        DDSImageData        image = makeLoadedImage();
        SW_EXPECT_FALSE( DDSLoader::loadFromMemory( bytes.data(), bytes.size(), image ) );
        SW_EXPECT_FALSE( image.isValid() );
    }
    SW_EXPECT_LOG( "DDS.UnsupportedFormat" );
}
