// TextureBlockEncoder — 인코더가 쓴 블록을 명세대로 푼 값이 원본과 가까운지 본다. 디코더는 인코더가 쓰는 모드만 푼다. 실제 장치 대조는 Vulkan lavapipe(ETC2 · ASTC 지원)로 통합 뒤에 한다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/Texture/TextureBlockEncoder.h"
#include "Editor/Common/Asset/Texture/TextureImportConfig.h"
#include "Editor/Common/Asset/Texture/TextureImporter.h"

#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureFormatFamily.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TextureBlockEncoderTestUtil
    {
        /** @brief 4x4 RGBA 그라디언트입니다(블록 안에서 값이 고르게 변한다). */
        static vector<uint8> makeGradient()
        {
            vector<uint8> listByte( 16 * 4 );
            for ( uint32 y = 0; y < 4; ++y )
            {
                for ( uint32 x = 0; x < 4; ++x )
                {
                    uint8* pPixel = listByte.data() + ( y * 4 + x ) * 4;
                    pPixel[0]     = static_cast<uint8>( 40 + x * 40 );
                    pPixel[1]     = static_cast<uint8>( 200 - y * 40 );
                    pPixel[2]     = static_cast<uint8>( 100 + x * 10 + y * 10 );
                    pPixel[3]     = static_cast<uint8>( 255 - x * 30 );
                }
            }
            return listByte;
        }

        /** @brief 색이 한 직선 위에서 변하는 4x4 입니다. 고정 모드 인코더는 끝점 둘로 직선 하나만 나타낸다 — x · y 로 따로 변하는 색은 원래 멀어진다. */
        static vector<uint8> makeLineGradient()
        {
            vector<uint8> listByte( 16 * 4 );
            for ( uint32 index = 0; index < 16; ++index )
            {
                uint8* pPixel = listByte.data() + index * 4;
                pPixel[0]     = static_cast<uint8>( 40 + index * 10 );
                pPixel[1]     = static_cast<uint8>( 200 - index * 8 );
                pPixel[2]     = static_cast<uint8>( 100 + index * 5 );
                pPixel[3]     = static_cast<uint8>( 255 - index * 6 );
            }
            return listByte;
        }

        static uint32 readBits( const uint8* pBlock, uint32 bitOffset, uint32 bitCount )
        {
            uint32 value = 0;
            for ( uint32 index = 0; index < bitCount; ++index )
            {
                const uint32 bit = bitOffset + index;
                value |= static_cast<uint32>( ( pBlock[bit / 8] >> ( bit % 8 ) ) & 1u ) << index;
            }
            return value;
        }

        /** @brief 고정 모드 ASTC 블록의 화소 하나를 풉니다. */
        static int32 decodeASTC( const uint8* pBlock, uint32 x, uint32 y, uint32 channel )
        {
            constexpr int32 kArrWeight[4] = { 0, 21, 43, 64 };
            const uint32    index         = y * 4 + x;
            const uint32    weightIndex   = readBits( pBlock, 127 - index * 2, 1 ) | ( readBits( pBlock, 126 - index * 2, 1 ) << 1 );
            const int32     first         = static_cast<int32>( readBits( pBlock, 17 + channel * 16, 8 ) );
            const int32     second        = static_cast<int32>( readBits( pBlock, 25 + channel * 16, 8 ) );
            return ( first * ( 64 - kArrWeight[weightIndex] ) + second * kArrWeight[weightIndex] + 32 ) / 64;
        }

        /** @brief EAC 블록의 화소 하나를 8 비트 값으로 풉니다(명세의 식). */
        static int32 decodeEAC( const uint8* pBlock, uint32 x, uint32 y )
        {
            constexpr int32 kArrModifier[16][8] = {
                {-3, -6,  -9, -15, 2, 5, 8, 14},
                {-3, -7, -10, -13, 2, 6, 9, 12},
                {-2, -5,  -8, -13, 1, 4, 7, 12},
                {-2, -4,  -6, -13, 1, 3, 5, 12},
                {-3, -6,  -8, -12, 2, 5, 7, 11},
                {-3, -7,  -9, -11, 2, 6, 8, 10},
                {-4, -7,  -8, -11, 3, 6, 7, 10},
                {-3, -5,  -8, -11, 2, 4, 7, 10},
                {-2, -6,  -8, -10, 1, 5, 7,  9},
                {-2, -5,  -8, -10, 1, 4, 7,  9},
                {-2, -4,  -8, -10, 1, 3, 7,  9},
                {-2, -5,  -7, -10, 1, 4, 6,  9},
                {-3, -4,  -7, -10, 2, 3, 6,  9},
                {-1, -2,  -3, -10, 0, 1, 2,  9},
                {-4, -6,  -8,  -9, 3, 5, 7,  8},
                {-3, -5,  -7,  -9, 2, 4, 6,  8},
            };
            uint64 bits = 0;
            for ( uint32 index = 2; index < 8; ++index )
            {
                bits = ( bits << 8 ) | pBlock[index];
            }
            const uint32 pixelIndex = x * 4 + y;
            const uint32 modifier   = static_cast<uint32>( bits >> ( 45 - pixelIndex * 3 ) ) & 7u;
            const int32  multiplier = pBlock[1] >> 4;
            return MathUtil::clamp( pBlock[0] + kArrModifier[pBlock[1] & 15][modifier] * multiplier, 0, 255 );
        }

        /** @brief ETC2 개별 모드 블록의 화소 하나를 풉니다. */
        static int32 decodeETC( const uint8* pBlock, uint32 x, uint32 y, uint32 channel )
        {
            constexpr int32 kArrModifier[8][2] = {
                { 2,   8},
                { 5,  17},
                { 9,  29},
                {13,  42},
                {18,  60},
                {24,  80},
                {33, 106},
                {47, 183}
            };
            const bool   bSecond  = x >= 2;
            const int32  base     = ( bSecond ? pBlock[channel] & 15 : pBlock[channel] >> 4 ) * 17;
            const uint32 table    = bSecond ? ( pBlock[3] >> 2 ) & 7u : pBlock[3] >> 5;
            const uint32 bit      = x * 4 + y;
            const uint32 msb      = ( ( static_cast<uint32>( pBlock[4] ) << 8 | pBlock[5] ) >> bit ) & 1u;
            const uint32 lsb      = ( ( static_cast<uint32>( pBlock[6] ) << 8 | pBlock[7] ) >> bit ) & 1u;
            const int32  modifier = kArrModifier[table][lsb] * ( msb != 0 ? -1 : 1 );
            return MathUtil::clamp( base + modifier, 0, 255 );
        }
    };
} // namespace

/**
 * @brief [TextureBlockEncoderTest] ASTC 4x4 블록은 고정 모드 비트를 쓰고, 끝점 합이 줄지 않아(파랑 수축 없음) 푼 값이 원본 근처다
 */
SW_TEST_CASE( TextureBlockEncoderTest, ASTCBlockDecodesNearTheSource )
{
    const vector<uint8> listSource = TextureBlockEncoderTestUtil::makeLineGradient();
    vector<uint8>       listBlock;
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::ASTC_4x4_UNORM, listSource.data(), 4, 4, listBlock ) );
    SW_REQUIRE_EQUAL( size_t( 16 ), listBlock.size() );
    SW_EXPECT_EQUAL( 0x042u, TextureBlockEncoderTestUtil::readBits( listBlock.data(), 0, 11 ) );
    SW_EXPECT_EQUAL( 12u, TextureBlockEncoderTestUtil::readBits( listBlock.data(), 13, 4 ) );
    uint32 firstSum  = 0;
    uint32 secondSum = 0;
    for ( uint32 channel = 0; channel < 3; ++channel )
    {
        firstSum += TextureBlockEncoderTestUtil::readBits( listBlock.data(), 17 + channel * 16, 8 );
        secondSum += TextureBlockEncoderTestUtil::readBits( listBlock.data(), 25 + channel * 16, 8 );
    }
    SW_EXPECT_TRUE( secondSum >= firstSum );
    for ( uint32 y = 0; y < 4; ++y )
    {
        for ( uint32 x = 0; x < 4; ++x )
        {
            for ( uint32 channel = 0; channel < 4; ++channel )
            {
                const int32 decoded = TextureBlockEncoderTestUtil::decodeASTC( listBlock.data(), x, y, channel );
                const int32 source  = listSource[( y * 4 + x ) * 4 + channel];
                SW_EXPECT_TRUE_MSG( MathUtil::abs( decoded - source ) <= 30, "ASTC pixel is far from the source" );
            }
        }
    }
}

/**
 * @brief [TextureBlockEncoderTest] ETC2 RGBA8 은 EAC 알파 8 바이트 + 색 8 바이트이고, EAC R11 · RG11 은 R · RG 채널을 푼 값이 원본 근처다
 */
SW_TEST_CASE( TextureBlockEncoderTest, ETC2AndEACBlocksDecodeNearTheSource )
{
    const vector<uint8> listSource = TextureBlockEncoderTestUtil::makeGradient();
    vector<uint8>       listBlock;
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::ETC2_RGBA8_UNORM, listSource.data(), 4, 4, listBlock ) );
    SW_REQUIRE_EQUAL( size_t( 16 ), listBlock.size() );
    for ( uint32 y = 0; y < 4; ++y )
    {
        for ( uint32 x = 0; x < 4; ++x )
        {
            const uint8* pSource = listSource.data() + ( y * 4 + x ) * 4;
            SW_EXPECT_TRUE( MathUtil::abs( TextureBlockEncoderTestUtil::decodeEAC( listBlock.data(), x, y ) - pSource[3] ) <= 20 );
            for ( uint32 channel = 0; channel < 3; ++channel )
            {
                SW_EXPECT_TRUE( MathUtil::abs( TextureBlockEncoderTestUtil::decodeETC( listBlock.data() + 8, x, y, channel ) - pSource[channel] ) <= 70 );
            }
        }
    }

    listBlock.clear();
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::EAC_RG11_UNORM, listSource.data(), 4, 4, listBlock ) );
    SW_REQUIRE_EQUAL( size_t( 16 ), listBlock.size() );
    SW_EXPECT_TRUE( MathUtil::abs( TextureBlockEncoderTestUtil::decodeEAC( listBlock.data(), 1, 2 ) - listSource[( 2 * 4 + 1 ) * 4 + 0] ) <= 20 );
    SW_EXPECT_TRUE( MathUtil::abs( TextureBlockEncoderTestUtil::decodeEAC( listBlock.data() + 8, 1, 2 ) - listSource[( 2 * 4 + 1 ) * 4 + 1] ) <= 20 );

    listBlock.clear();
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::EAC_R11_UNORM, listSource.data(), 4, 4, listBlock ) );
    SW_EXPECT_EQUAL( size_t( 8 ), listBlock.size() );
}

/**
 * @brief [TextureBlockEncoderTest] 4 로 나누어떨어지지 않는 밉도 `computeRHITextureMipLayout` 의 블록 수만큼 쓴다. 인코더가 모르는 형식은 오류다
 */
SW_TEST_CASE( TextureBlockEncoderTest, MipSizesMatchTheRHILayout )
{
    SW_EXPECT_LOG( "TextureBlockEncoder.UnknownFormat" );
    const vector<uint8> listSource( static_cast<size_t>( 6 ) * 5 * 4, 0x80 );
    vector<uint8>       listBlock;
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::ETC2_RGBA8_UNORM, listSource.data(), 6, 5, listBlock ) );
    RHITextureMipSpan span{};
    SW_REQUIRE_TRUE( computeRHITextureMipLayout( RHIFormat::ETC2_RGBA8_UNORM, 6, 5, 0, span ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( span._sizeBytes ), listBlock.size() );
    SW_EXPECT_FALSE( TextureBlockEncoder::encodeMip( RHIFormat::BC7_UNORM, listSource.data(), 6, 5, listBlock ) );
}

/**
 * @brief [TextureBlockEncoderTest] 원본 + 규칙에서 형식 변형을 굽는다 — 로더가 읽고 형식이 묶음의 것이며, 4 배수로 다시 표본한 밉 사슬이다. 비압축 규칙은 규칙의 형식 그대로다
 */
SW_TEST_CASE( TextureBlockEncoderTest, CookedVariantLoadsWithTheFamilyFormat )
{
    // 6x5 비압축 32 비트 TGA(머리 18 바이트 — 형식 2, 아래에서 위로).
    constexpr uint32 kWidth  = 6;
    constexpr uint32 kHeight = 5;
    vector<uint8>    bytes( 18 + kWidth * kHeight * 4, 0x80 );
    for ( uint32 index = 0; index < 18; ++index )
    {
        bytes[index] = 0;
    }
    bytes[2]  = 2;
    bytes[12] = static_cast<uint8>( kWidth );
    bytes[14] = static_cast<uint8>( kHeight );
    bytes[16] = 32;
    bytes[17] = 8;

    const string root       = test::makeScratchPath( "TextureVariantCook" );
    const string sourcePath = FileUtil::joinPath( root, "textures_raw/odd.tga" );
    const string outputPath = FileUtil::joinPath( root, "textures/odd.astc.dds" );
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( sourcePath ) && FileUtil::writeFile( sourcePath, bytes.data(), bytes.size() ) );

    TextureFormatFamily family;
    family._name   = "ASTC";
    family._suffix = ".astc";
    family._color  = RHIFormat::ASTC_4x4_UNORM;
    family._normal = RHIFormat::ASTC_4x4_UNORM;
    family._mask   = RHIFormat::ASTC_4x4_UNORM;
    family._hdr    = RHIFormat::R16G16B16A16_FLOAT;

    SW_REQUIRE_TRUE( TextureImporter::cookTextureVariant( sourcePath, outputPath, TextureImportRule{}, family ) ); // 기본 규칙은 BC7 · 밉 — 용도는 색
    vector<uint8> output;
    SW_REQUIRE_TRUE( FileUtil::readFile( outputPath, output ) );
    DDSImageData image;
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( output.data(), output.size(), image ) );
    SW_EXPECT_TRUE( Texture2D::toRHIFormatFromDxgi( image._dxgiFormat ) == RHIFormat::ASTC_4x4_UNORM );
    SW_EXPECT_EQUAL( 8u, image._width );
    SW_EXPECT_EQUAL( 8u, image._height );
    SW_EXPECT_EQUAL( 4u, image._mipCount );

    TextureImportRule rawRule;
    rawRule._format = TextureImportFormat::R8G8B8A8;
    SW_REQUIRE_TRUE( TextureImporter::cookTextureVariant( sourcePath, outputPath, rawRule, family ) );
    output.clear();
    SW_REQUIRE_TRUE( FileUtil::readFile( outputPath, output ) );
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( output.data(), output.size(), image ) );
    SW_EXPECT_TRUE( Texture2D::toRHIFormatFromDxgi( image._dxgiFormat ) == RHIFormat::R8G8B8A8_UNORM );

    SW_EXPECT_TRUE( TextureImportFormatUtil::getUsage( TextureImportFormat::BC5 ) == TextureFormatUsage::Normal );
    SW_EXPECT_TRUE( TextureImportFormatUtil::getUsage( TextureImportFormat::BC6H ) == TextureFormatUsage::HDR );
    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}
