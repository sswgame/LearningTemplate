// TextureBlockEncoder — astc-encoder · etcpak 이 쓴 블록을 같은 라이브러리의 디코더로 풀어 원본과 가까운지, 같은 입력에서 같은 바이트인지 본다(DDC 키).
// 실제 장치 대조는 Vulkan lavapipe(ETC2 · ASTC 지원)로 통합 뒤에 한다.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/Texture/TextureBlockEncoder.h"
#include "Editor/Common/Asset/Texture/TextureImportConfig.h"
#include "Editor/Common/Asset/Texture/TextureImporter.h"

#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHITextureMipLayout.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureFormatFamily.h"

#include <astcenc/astcenc.h>
#include <etcpak/Decode.hpp>

using namespace sw;
using namespace sw::editor;

namespace
{
    struct TextureBlockEncoderTestUtil
    {
        static constexpr uint32 kSize = 8;

        /** @brief 8x8 RGBA 그라디언트입니다(x · y 로 따로 변하는 매끈한 색 — 블록 압축이 잘 맞는 입력). */
        static vector<uint8> makeGradient()
        {
            vector<uint8> listByte( kSize * kSize * 4 );
            for ( uint32 y = 0; y < kSize; ++y )
            {
                for ( uint32 x = 0; x < kSize; ++x )
                {
                    uint8* pPixel = listByte.data() + ( y * kSize + x ) * 4;
                    pPixel[0]     = static_cast<uint8>( 40 + x * 20 );
                    pPixel[1]     = static_cast<uint8>( 200 - y * 20 );
                    pPixel[2]     = static_cast<uint8>( 100 + x * 5 + y * 5 );
                    pPixel[3]     = static_cast<uint8>( 255 - x * 15 );
                }
            }
            return listByte;
        }

        /** @brief astcenc 로 ASTC 4x4 블록을 RGBA8 로 풉니다. 실패하면 빈 벡터입니다. */
        static vector<uint8> decodeASTC( const vector<uint8>& listBlock, uint32 width, uint32 height )
        {
            vector<uint8>    listPixel( static_cast<size_t>( width ) * height * 4 );
            astcenc_config   config{};
            astcenc_context* pContext = nullptr;
            if ( astcenc_config_init( ASTCENC_PRF_LDR, 4, 4, 1, ASTCENC_PRE_FASTEST, ASTCENC_FLG_DECOMPRESS_ONLY, &config ) != ASTCENC_SUCCESS ||
                 astcenc_context_alloc( &config, 1, &pContext, nullptr ) != ASTCENC_SUCCESS )
                return {};
            void*         pSlice = listPixel.data();
            astcenc_image image{};
            image.dim_x     = width;
            image.dim_y     = height;
            image.dim_z     = 1;
            image.data_type = ASTCENC_TYPE_U8;
            image.data      = &pSlice;
            const astcenc_swizzle swizzle{ ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
            const astcenc_error   status = astcenc_decompress_image( pContext, listBlock.data(), listBlock.size(), &image, &swizzle, 0 );
            astcenc_context_free( pContext );
            return status == ASTCENC_SUCCESS ? listPixel : vector<uint8>{};
        }

        /** @brief etcpak 으로 ETC2 RGBA8 · EAC R11 · RG11 블록을 RGBA8 로 풉니다(크기는 4 의 배수). */
        static vector<uint8> decodeETC( RHIFormat format, const vector<uint8>& listBlock, uint32 width, uint32 height )
        {
            vector<uint64> listWord( listBlock.size() / sizeof( uint64 ) );
            Memory::copy( listWord.data(), listBlock.data(), listBlock.size() );
            vector<uint32> listPixel( static_cast<size_t>( width ) * height );
            if ( format == RHIFormat::ETC2_RGBA8_UNORM )
                DecodeRGBA( listWord.data(), listPixel.data(), static_cast<int32_t>( width ), static_cast<int32_t>( height ) );
            else if ( format == RHIFormat::EAC_RG11_UNORM )
                DecodeRG( listWord.data(), listPixel.data(), static_cast<int32_t>( width ), static_cast<int32_t>( height ) );
            else
                DecodeR( listWord.data(), listPixel.data(), static_cast<int32_t>( width ), static_cast<int32_t>( height ) );
            vector<uint8> listByte( listPixel.size() * 4 );
            for ( size_t index = 0; index < listPixel.size(); ++index )
            {
                for ( uint32 channel = 0; channel < 4; ++channel )
                {
                    listByte[index * 4 + channel] = static_cast<uint8>( listPixel[index] >> ( channel * 8 ) ); // 디코더는 R 이 아래 바이트다
                }
            }
            return listByte;
        }

        /** @brief 두 RGBA8 그림의 채널 @p firstChannel ~ @p lastChannel 에서 가장 큰 차이입니다. */
        static int32 measureMaxError( const vector<uint8>& listSource, const vector<uint8>& listDecoded, uint32 firstChannel, uint32 lastChannel )
        {
            int32 maxError = 0;
            for ( size_t pixel = 0; pixel * 4 < listSource.size(); ++pixel )
            {
                for ( uint32 channel = firstChannel; channel <= lastChannel; ++channel )
                {
                    maxError = MathUtil::max( maxError, MathUtil::abs( static_cast<int32>( listSource[pixel * 4 + channel] ) - static_cast<int32>( listDecoded[pixel * 4 + channel] ) ) );
                }
            }
            return maxError;
        }

        /** @brief 채널 @p firstChannel ..@p lastChannel 의 PSNR(dB)입니다. 블록 압축 품질은 화소 하나의 최대 오차가 아니라 평균 제곱 오차로 잰다(astcenc · etcpak 의 품질 보고와 같은 척도). */
        static float64 measurePSNR( const vector<uint8>& listSource, const vector<uint8>& listDecoded, uint32 firstChannel, uint32 lastChannel )
        {
            float64 sumSquared = 0.0;
            size_t  count      = 0;
            for ( size_t pixel = 0; pixel * 4 < listSource.size(); ++pixel )
            {
                for ( uint32 channel = firstChannel; channel <= lastChannel; ++channel )
                {
                    const float64 difference = static_cast<float64>( listSource[pixel * 4 + channel] ) - static_cast<float64>( listDecoded[pixel * 4 + channel] );
                    sumSquared += difference * difference;
                    ++count;
                }
            }
            const float64 meanSquared = sumSquared / static_cast<float64>( count );
            return meanSquared <= 0.0 ? 99.0 : 10.0 * std::log10( 255.0 * 255.0 / meanSquared );
        }
    };
} // namespace

/**
 * @brief [TextureBlockEncoderTest] ASTC 4x4 는 astcenc 로 풀어 원본 근처이고, 같은 입력은 같은 바이트다(쿠킹 결과가 DDC 키다)
 */
SW_TEST_CASE( TextureBlockEncoderTest, ASTCRoundTripsNearTheSourceDeterministically )
{
    using Util                     = TextureBlockEncoderTestUtil;
    const vector<uint8> listSource = Util::makeGradient();
    vector<uint8>       listBlock;
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::ASTC_4x4_UNORM, listSource.data(), Util::kSize, Util::kSize, listBlock ) );
    SW_REQUIRE_EQUAL( size_t( 4 * 16 ), listBlock.size() );
    const vector<uint8> listDecoded = Util::decodeASTC( listBlock, Util::kSize, Util::kSize );
    SW_REQUIRE_EQUAL( listSource.size(), listDecoded.size() );
    SW_EXPECT_TRUE_MSG( Util::measurePSNR( listSource, listDecoded, 0, 3 ) >= 36.0, "ASTC is far from the source" ); // -medium 은 이 그라디언트에서 약 38 dB

    vector<uint8> listAgain;
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::ASTC_4x4_UNORM, listSource.data(), Util::kSize, Util::kSize, listAgain ) );
    SW_EXPECT_TRUE( listAgain == listBlock );
}

/**
 * @brief [TextureBlockEncoderTest] ETC2 RGBA8(EAC 알파 + 색) · EAC RG11 · R11 은 etcpak 으로 풀어 원본 근처이고, 같은 입력은 같은 바이트다
 */
SW_TEST_CASE( TextureBlockEncoderTest, ETC2AndEACRoundTripNearTheSourceDeterministically )
{
    using Util                     = TextureBlockEncoderTestUtil;
    const vector<uint8> listSource = Util::makeGradient();
    vector<uint8>       listBlock;
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::ETC2_RGBA8_UNORM, listSource.data(), Util::kSize, Util::kSize, listBlock ) );
    SW_REQUIRE_EQUAL( size_t( 4 * 16 ), listBlock.size() );
    vector<uint8> listDecoded = Util::decodeETC( RHIFormat::ETC2_RGBA8_UNORM, listBlock, Util::kSize, Util::kSize );
    SW_EXPECT_TRUE_MSG( Util::measurePSNR( listSource, listDecoded, 0, 2 ) >= 40.0, "ETC2 color is far from the source" ); // 모든 모드(planar 포함) 약 47 dB, 빠른 휴리스틱은 26 dB 라 여기서 진다
    SW_EXPECT_TRUE_MSG( Util::measureMaxError( listSource, listDecoded, 3, 3 ) <= 8, "ETC2 alpha is far from the source" );
    vector<uint8> listAgain;
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::ETC2_RGBA8_UNORM, listSource.data(), Util::kSize, Util::kSize, listAgain ) );
    SW_EXPECT_TRUE( listAgain == listBlock );

    listBlock.clear();
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::EAC_RG11_UNORM, listSource.data(), Util::kSize, Util::kSize, listBlock ) );
    SW_REQUIRE_EQUAL( size_t( 4 * 16 ), listBlock.size() );
    listDecoded = Util::decodeETC( RHIFormat::EAC_RG11_UNORM, listBlock, Util::kSize, Util::kSize );
    SW_EXPECT_TRUE_MSG( Util::measureMaxError( listSource, listDecoded, 0, 1 ) <= 8, "EAC RG11 is far from the source" );

    listBlock.clear();
    SW_REQUIRE_TRUE( TextureBlockEncoder::encodeMip( RHIFormat::EAC_R11_UNORM, listSource.data(), Util::kSize, Util::kSize, listBlock ) );
    SW_REQUIRE_EQUAL( size_t( 4 * 8 ), listBlock.size() );
    listDecoded = Util::decodeETC( RHIFormat::EAC_R11_UNORM, listBlock, Util::kSize, Util::kSize );
    SW_EXPECT_TRUE_MSG( Util::measureMaxError( listSource, listDecoded, 0, 0 ) <= 8, "EAC R11 is far from the source" );
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
    SW_EXPECT_TRUE( Texture2D::toRHIFormatFromDXGI( image._dxgiFormat ) == RHIFormat::ASTC_4x4_UNORM );
    SW_EXPECT_EQUAL( 8u, image._width );
    SW_EXPECT_EQUAL( 8u, image._height );
    SW_EXPECT_EQUAL( 4u, image._mipCount );

    TextureImportRule rawRule;
    rawRule._format = TextureImportFormat::R8G8B8A8;
    SW_REQUIRE_TRUE( TextureImporter::cookTextureVariant( sourcePath, outputPath, rawRule, family ) );
    output.clear();
    SW_REQUIRE_TRUE( FileUtil::readFile( outputPath, output ) );
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( output.data(), output.size(), image ) );
    SW_EXPECT_TRUE( Texture2D::toRHIFormatFromDXGI( image._dxgiFormat ) == RHIFormat::R8G8B8A8_UNORM );

    SW_EXPECT_TRUE( TextureImportFormatUtil::getUsage( TextureImportFormat::BC5 ) == TextureFormatUsage::Normal );
    SW_EXPECT_TRUE( TextureImportFormatUtil::getUsage( TextureImportFormat::BC6H ) == TextureFormatUsage::HDR );
    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}
