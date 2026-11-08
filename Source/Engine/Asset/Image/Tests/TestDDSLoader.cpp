#include "pch.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Serialization/LittleEndianUtil.h"
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
    vector<uint8> makeLegacyDDS( const LegacyPixelFormat& pixelFormat, size_t payloadSize, uint32 depth = 0, uint32 mipCount = 0, uint32 caps2 = 0 )
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
        LittleEndianUtil::appendUint32( bytes, 0 ); // caps
        LittleEndianUtil::appendUint32( bytes, caps2 );
        for ( uint32 index = 0; index < 3; ++index )
        {
            LittleEndianUtil::appendUint32( bytes, 0 ); // caps3 · caps4 · 예약
        }
        bytes.resize( bytes.size() + payloadSize, 0x5A );
        return bytes;
    }

    /** @brief DX10 머리 DDS 를 만듭니다(형식 @p dxgiFormat, 배열 @p arraySize, misc @p miscFlag). 머리 뒤에 @p payloadSize 바이트를 붙입니다. */
    vector<uint8> makeDx10DDS( uint32 dxgiFormat, size_t payloadSize, uint32 arraySize = 1, uint32 miscFlag = 0 )
    {
        vector<uint8> bytes = makeLegacyDDS( LegacyPixelFormat{ DDSFormat::kPixelFormatFourCCFlag, DDSFormat::kDx10FourCC, 0, 0, 0, 0, 0 }, 0 );
        LittleEndianUtil::appendUint32( bytes, dxgiFormat );
        LittleEndianUtil::appendUint32( bytes, 3 ); // D3D10_RESOURCE_DIMENSION_TEXTURE2D
        LittleEndianUtil::appendUint32( bytes, miscFlag );
        LittleEndianUtil::appendUint32( bytes, arraySize );
        LittleEndianUtil::appendUint32( bytes, 0 ); // misc2
        bytes.resize( bytes.size() + payloadSize, 0x5A );
        return bytes;
    }

    LegacyPixelFormat makeFourCC( uint32 fourCC )
    {
        return LegacyPixelFormat{ DDSFormat::kPixelFormatFourCCFlag, fourCC, 0, 0, 0, 0, 0 };
    }

    LegacyPixelFormat makeRGB( uint32 rMask, uint32 gMask, uint32 bMask, uint32 aMask )
    {
        return LegacyPixelFormat{ DDSFormat::kPixelFormatRGBFlag, 0, 32, rMask, gMask, bMask, aMask };
    }

    /** @brief 성공한 읽기로 채워 둔 출력입니다. 실패한 읽기가 이것을 비우는지 봅니다. */
    DDSImageData makeLoadedImage()
    {
        const vector<uint8> bytes = makeLegacyDDS( makeFourCC( FourCCUtil::make( "DXT1" ) ), 8 );
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
        size_t            _payloadSize; ///< 4 × 2 한 장의 바이트(BC 는 4×4 블록 하나)
    };
    const Case arrCase[] = {
        { makeFourCC( FourCCUtil::make( "DXT1" ) ), 71, true, false, 8 },
        { makeFourCC( FourCCUtil::make( "DXT5" ) ), 77, true, false, 16 },
        { makeFourCC( FourCCUtil::make( "ATI2" ) ), 83, true, false, 16 },
        { makeFourCC( 113 ), 10, false, false, 64 }, // D3DFMT_A16B16G16R16F
        { makeFourCC( 114 ), 41, false, false, 32 }, // D3DFMT_R32F
        { makeRGB( 0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u ), 28, false, false, 32 },
        { makeRGB( 0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0x00000000u ), 28, false, false, 32 },
        { makeRGB( 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u ), 87, false, true, 32 },
        { makeRGB( 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0x00000000u ), 88, false, true, 32 },
    };
    for ( const Case& testCase : arrCase )
    {
        const vector<uint8> bytes = makeLegacyDDS( testCase._pixelFormat, testCase._payloadSize );
        DDSImageData        image;
        SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( bytes.data(), bytes.size(), image ) );
        SW_EXPECT_EQUAL( testCase._dxgiFormat, image._dxgiFormat );
        SW_EXPECT_EQUAL( testCase._bCompressed, image._bCompressed == SW_TRUE );
        SW_EXPECT_EQUAL( testCase._bBgra, image._bIsBgra == SW_TRUE );
        SW_EXPECT_EQUAL( testCase._payloadSize, image._bytes.size() );
    }

    // 표를 돌려주는 함수는 읽기가 쓰는 표 그대로다
    SW_EXPECT_EQUAL( size_t( 15 ), DDSLoader::getLegacyFourCCTable().size() );
    SW_EXPECT_EQUAL( size_t( 4 ), DDSLoader::getLegacyRGBMaskTable().size() );
}

/**
 * @brief [DDSLoaderTest] 머리에 적은 깊이 · 밉 수는 그대로 읽고, 0 이면 1 로 본다
 * @details DXT1 4×2×3 밉 셋: 밉 0 은 블록 하나 × 깊이 3(24 B), 밉 1 · 2 는 블록 하나(8 B씩) — 40 B.
 */
SW_TEST_CASE( DDSLoaderTest, KeepsDepthAndMipCount )
{
    const vector<uint8> bytes = makeLegacyDDS( makeFourCC( FourCCUtil::make( "DXT1" ) ), 40, 3, 3 );
    DDSImageData        image;
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( bytes.data(), bytes.size(), image ) );
    SW_EXPECT_EQUAL( 3u, image._depth );
    SW_EXPECT_EQUAL( 3u, image._mipCount );
    SW_EXPECT_EQUAL( 1u, image._layerCount );
}

/**
 * @brief [DDSLoaderTest] 픽셀 바이트 수는 너비 × 높이 × 형식 크기(밉 · 층 포함)와 정확히 같아야 한다 — 짧거나 길면 실패하고 출력을 비운다
 * @details 전에는 머리 뒤 바이트를 크기와 견주지 않아, 잘린 파일은 업로드가 버퍼 밖을 읽었고 밉 수 · 형식을 잘못 적은 파일도 그대로 읽혔다.
 */
SW_TEST_CASE( DDSLoaderTest, PayloadMustMatchTheFormatSize )
{
    SW_EXPECT_LOG( "DDS.PayloadSizeMismatch" );
    const LegacyPixelFormat rgba = makeRGB( 0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u );
    // RGBA8 4×2 = 32 B
    const size_t arrBadSize[] = { 31, 33, 8 };
    for ( const size_t badSize : arrBadSize )
    {
        const vector<uint8> bytes = makeLegacyDDS( rgba, badSize );
        DDSImageData        image = makeLoadedImage();
        SW_EXPECT_FALSE( DDSLoader::loadFromMemory( bytes.data(), bytes.size(), image ) );
        SW_EXPECT_FALSE( image.isValid() );
    }
    // 밉 둘(4×2 + 2×1) = 40 B
    DDSImageData        image;
    const vector<uint8> twoMips = makeLegacyDDS( rgba, 40, 0, 2 );
    SW_EXPECT_TRUE( DDSLoader::loadFromMemory( twoMips.data(), twoMips.size(), image ) );
    const vector<uint8> twoMipsShort = makeLegacyDDS( rgba, 32, 0, 2 );
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( twoMipsShort.data(), twoMipsShort.size(), image ) );
    // DX10 R16_FLOAT 4×2 = 16 B — 형식마다 화소 바이트가 다르다
    const vector<uint8> halfFloat = makeDx10DDS( 54, 16 );
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( halfFloat.data(), halfFloat.size(), image ) );
    SW_EXPECT_EQUAL( size_t( 16 ), image._bytes.size() );
}

/**
 * @brief [DDSLoaderTest] 배열 · 큐브 맵은 층 수를 세어 바이트를 견준다 — DX10 배열 × misc 큐브 면 여섯, 옛 머리 caps2 큐브 맵
 */
SW_TEST_CASE( DDSLoaderTest, ArraysAndCubeMapsCountTheirLayers )
{
    DDSImageData        image;
    const vector<uint8> array = makeDx10DDS( 28, size_t( 32 ) * 3, 3 );
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( array.data(), array.size(), image ) );
    SW_EXPECT_EQUAL( 3u, image._layerCount );
    const vector<uint8> cubeArray = makeDx10DDS( 28, size_t( 32 ) * 12, 2, DDSFormat::kDx10MiscTextureCubeFlag );
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( cubeArray.data(), cubeArray.size(), image ) );
    SW_EXPECT_EQUAL( 12u, image._layerCount );
    const vector<uint8> legacyCube = makeLegacyDDS( makeFourCC( FourCCUtil::make( "DXT1" ) ), size_t( 8 ) * 6, 0, 0, DDSFormat::kCaps2CubemapFlag );
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( legacyCube.data(), legacyCube.size(), image ) );
    SW_EXPECT_EQUAL( 6u, image._layerCount );
}

/**
 * @brief [DDSLoaderTest] 온전한 사슬보다 많은 밉 수 · 배열 크기 0 은 깨진 머리, 크기 표에 없는 DX10 형식은 모르는 형식이다
 */
SW_TEST_CASE( DDSLoaderTest, InconsistentHeadersFail )
{
    SW_EXPECT_LOG( "DDS.CorruptHeader" );
    SW_EXPECT_LOG( "DDS.UnsupportedFormat" );
    DDSImageData        image;
    const vector<uint8> tooManyMips = makeLegacyDDS( makeFourCC( FourCCUtil::make( "DXT1" ) ), size_t( 8 ) * 4, 0, 4 ); // 4×2 는 밉 셋까지
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( tooManyMips.data(), tooManyMips.size(), image ) );
    const vector<uint8> emptyArray = makeDx10DDS( 28, 32, 0 );
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( emptyArray.data(), emptyArray.size(), image ) );
    const vector<uint8> unknownDxgi = makeDx10DDS( 11, 64 ); // R16G16B16A16_UNORM — 크기 표에 없다
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( unknownDxgi.data(), unknownDxgi.size(), image ) );
    SW_EXPECT_FALSE( image.isValid() );
}

/**
 * @brief [DDSLoaderTest] 크기 표는 번호가 겹치지 않고, 옛 머리 표가 가리키는 형식은 모두 크기 표에 있으며, 블록은 1×1 이거나 4×4 다
 */
SW_TEST_CASE( DDSLoaderTest, FormatSizeTableCoversTheLegacyTables )
{
    const vector<DDSFormatSize> listSize    = DDSLoader::getFormatSizeTable();
    const auto                  countOfDxgi = [&listSize]( uint32 dxgiFormat )
    {
        uint32 count = 0;
        for ( const DDSFormatSize& row : listSize )
        {
            count += row._dxgiFormat == dxgiFormat ? 1u : 0u;
        }
        return count;
    };
    for ( const DDSFormatSize& row : listSize )
    {
        SW_EXPECT_TRUE_MSG( countOfDxgi( row._dxgiFormat ) == 1, "duplicate DXGI " + to_string( row._dxgiFormat ) );
        SW_EXPECT_TRUE( row._blockBytes > 0 );
        SW_EXPECT_TRUE( ( row._blockWidth == 1 && row._blockHeight == 1 ) || ( row._blockWidth == 4 && row._blockHeight == 4 ) );
    }
    for ( const DDSLegacyFourCC& row : DDSLoader::getLegacyFourCCTable() )
    {
        SW_EXPECT_TRUE_MSG( countOfDxgi( row._dxgiFormat ) == 1, "legacy FourCC maps to DXGI " + to_string( row._dxgiFormat ) + " which has no size" );
    }
    for ( const DDSLegacyRGBMask& row : DDSLoader::getLegacyRGBMaskTable() )
    {
        SW_EXPECT_TRUE_MSG( countOfDxgi( row._dxgiFormat ) == 1, "legacy RGB mask maps to DXGI " + to_string( row._dxgiFormat ) + " which has no size" );
    }
}

/**
 * @brief [DDSLoaderTest] 짧은 버퍼 · 틀린 마법 수 · 틀린 머리 크기 · DX10 머리 잘림 · 픽셀 없음은 실패이고, 앞서 채운 출력도 비운다
 */
SW_TEST_CASE( DDSLoaderTest, MalformedBuffersFailAndClearTheOutput )
{
    SW_EXPECT_LOG( "DDS.Truncated" );
    SW_EXPECT_LOG( "DDS.InvalidMagic" );
    SW_EXPECT_LOG( "DDS.CorruptHeader" );

    DDSImageData image = makeLoadedImage();
    SW_REQUIRE_TRUE( image.isValid() );

    const vector<uint8> tooShort( 16 );
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( tooShort.data(), tooShort.size(), image ) );
    SW_EXPECT_FALSE( image.isValid() );
    SW_EXPECT_EQUAL( 0u, image._width );
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( nullptr, 512, image ) );

    vector<uint8> badMagic = makeLegacyDDS( makeFourCC( FourCCUtil::make( "DXT1" ) ), 8 );
    badMagic[0]            = 'X';
    image                  = makeLoadedImage();
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( badMagic.data(), badMagic.size(), image ) );
    SW_EXPECT_FALSE( image.isValid() );

    vector<uint8> badHeaderSize = makeLegacyDDS( makeFourCC( FourCCUtil::make( "DXT1" ) ), 8 );
    badHeaderSize[4]            = 100; // 머리 크기 124 → 100
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( badHeaderSize.data(), badHeaderSize.size(), image ) );

    vector<uint8> zeroWidth = makeLegacyDDS( makeFourCC( FourCCUtil::make( "DXT1" ) ), 8 );
    zeroWidth[16]           = 0; // 너비(머리 다섯째 칸, 값 4)를 0 으로
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( zeroWidth.data(), zeroWidth.size(), image ) );

    const vector<uint8> truncatedDx10 = makeLegacyDDS( makeFourCC( DDSFormat::kDx10FourCC ), 0 );
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( truncatedDx10.data(), truncatedDx10.size(), image ) );

    const vector<uint8> headerOnly = makeLegacyDDS( makeFourCC( FourCCUtil::make( "DXT1" ) ), 0 );
    image                          = makeLoadedImage();
    SW_EXPECT_FALSE( DDSLoader::loadFromMemory( headerOnly.data(), headerOnly.size(), image ) );
    SW_EXPECT_FALSE( image.isValid() );
}

/**
 * @brief [DDSLoaderTest] 표에 없는 FourCC · RGB 마스크, 형식 플래그가 없는 머리는 알아보지 못한 형식으로 실패한다(형식 0 을 성공으로 내보내지 않는다)
 */
SW_TEST_CASE( DDSLoaderTest, UnknownFormatsFail )
{
    SW_EXPECT_LOG( "DDS.UnsupportedFormat" );

    const LegacyPixelFormat arrPixelFormat[] = {
        makeFourCC( FourCCUtil::make( "ABCD" ) ),
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
}
