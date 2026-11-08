/**
 * @file TestTextureFormatSelection.cpp
 * @brief 대상별 텍스처 형식 표를 풀고 장치가 지원하는 첫 묶음을 고르는 규칙과, 표의 접미사가 곁 파일 규칙에 있는지 봅니다(`TextureFormatFamily`).
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Asset/Image/DDSFormat.h"
#include "Engine/Asset/Image/DDSLoader.h"
#include "Engine/Asset/Image/ImageFileWriter.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Graphics/Texture/TextureFormatFamily.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

using namespace sw;

namespace
{
    struct TextureFormatSelectionTestUtil
    {
        /** @brief 묶음 둘(BC 는 접미사 없음, ASTC)과 대상 하나의 표입니다. */
        static TextureFormatSettings makeSettings()
        {
            TextureFormatSettings        settings;
            TextureFormatFamilySettings& bc     = settings._listFamily.emplace_back();
            bc._name                            = "BC";
            bc._color                           = "BC7_UNORM";
            bc._normal                          = "BC5_UNORM";
            bc._mask                            = "BC4_UNORM";
            bc._hdr                             = "BC6H_UF16";
            TextureFormatFamilySettings& astc   = settings._listFamily.emplace_back();
            astc._name                          = "ASTC";
            astc._suffix                        = ".astc";
            astc._color                         = "ASTC_4x4_UNORM";
            astc._normal                        = "ASTC_4x4_UNORM";
            astc._mask                          = "ASTC_4x4_UNORM";
            astc._hdr                           = "R16G16B16A16_FLOAT";
            TextureFormatTargetSettings& target = settings._listTarget.emplace_back();
            target._name                        = "Web";
            target._listPlatform                = { "Web" };
            target._listFamily                  = { "BC", "ASTC" };
            return settings;
        }
    };
} // namespace

/**
 * @brief [TextureFormatSelectionTest] 장치가 지원하는 첫 묶음을 고른다 — BC 를 받는 장치는 BC(접미사 없음), 못 받는 장치는 ASTC, 둘 다 못 받으면 없다
 */
SW_TEST_CASE( TextureFormatSelectionTest, SelectsTheFirstSupportedFamily )
{
    const TextureFormatSettings settings = TextureFormatSelectionTestUtil::makeSettings();
    vector<TextureFormatFamily> listFamily;
    SW_REQUIRE_TRUE( TextureFormatFamily::resolveTarget( settings, "Web", engine::getSubsystem<TypeRegistry>(), listFamily ) );
    SW_REQUIRE_EQUAL( size_t( 2 ), listFamily.size() );
    SW_EXPECT_TRUE( listFamily[1].getFormat( TextureFormatUsage::HDR ) == RHIFormat::R16G16B16A16_FLOAT );
    SW_EXPECT_TRUE( listFamily[0].getFormat( TextureFormatUsage::Raw ) == RHIFormat::Unknown );

    test::FakeRHIDevice        device;
    const TextureFormatFamily* pFamily = TextureFormatFamily::selectFamily( listFamily, device );
    SW_REQUIRE_TRUE( pFamily != nullptr );
    SW_EXPECT_TRUE( pFamily->_suffix.empty() );

    // 한 용도라도 못 받으면 그 묶음을 쓰지 않는다(모바일 브라우저 — BC 없음).
    device._listUnsupportedFormat = { RHIFormat::BC5_UNORM };
    pFamily                       = TextureFormatFamily::selectFamily( listFamily, device );
    SW_REQUIRE_TRUE( pFamily != nullptr );
    SW_EXPECT_EQUAL( string( ".astc" ), pFamily->_suffix );

    device._listUnsupportedFormat.push_back( RHIFormat::ASTC_4x4_UNORM );
    SW_EXPECT_TRUE( TextureFormatFamily::selectFamily( listFamily, device ) == nullptr );
}

/**
 * @brief [TextureFormatSelectionTest] 실행은 빌드 플랫폼을 적은 대상 하나를 고르고, 없거나 둘이면 오류다(데스크톱으로 대신 고르지 않는다)
 */
SW_TEST_CASE( TextureFormatSelectionTest, PlatformPicksExactlyOneTarget )
{
    SW_EXPECT_LOG( "TextureFormats.PlatformTarget" );
    TextureFormatSettings settings = TextureFormatSelectionTestUtil::makeSettings();
    SW_EXPECT_EQUAL( string( "Web" ), TextureFormatFamily::findPlatformTarget( settings, "Web" ) );
    SW_EXPECT_TRUE( TextureFormatFamily::findPlatformTarget( settings, "Android" ).empty() );

    TextureFormatTargetSettings& other = settings._listTarget.emplace_back();
    other._name                        = "Other";
    other._listPlatform                = { "Web" };
    other._listFamily                  = { "BC" };
    SW_EXPECT_TRUE( TextureFormatFamily::findPlatformTarget( settings, "Web" ).empty() );
}

/**
 * @brief [TextureFormatSelectionTest] 쿠킹은 모든 대상의 접미사 묶음을 겹치지 않게 한 번에 굽는다 — 임포트한 DDS 묶음(BC)은 빠진다
 */
SW_TEST_CASE( TextureFormatSelectionTest, CookFamiliesAreTheUnionOfSuffixedFamilies )
{
    TextureFormatSettings        settings = TextureFormatSelectionTestUtil::makeSettings();
    TextureFormatTargetSettings& mobile   = settings._listTarget.emplace_back();
    mobile._name                          = "Mobile";
    mobile._listPlatform                  = { "Android" };
    mobile._listFamily                    = { "ASTC" };
    vector<TextureFormatFamily> listFamily;
    SW_REQUIRE_TRUE( TextureFormatFamily::resolveCookFamilies( settings, engine::getSubsystem<TypeRegistry>(), listFamily ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), listFamily.size() );
    SW_EXPECT_EQUAL( string( ".astc" ), listFamily[0]._suffix );
}

/**
 * @brief [TextureFormatSelectionTest] 표에 없는 대상 · 묶음 · 형식 이름은 조용히 넘어가지 않고 오류다(반쯤 푼 목록을 남기지 않는다)
 */
SW_TEST_CASE( TextureFormatSelectionTest, UnknownNamesAreErrors )
{
    SW_EXPECT_LOG( "TextureFormats.UnknownTarget" );
    SW_EXPECT_LOG( "TextureFormats.UnknownFamily" );
    SW_EXPECT_LOG( "TextureFormats.UnknownFormat" );
    const TypeRegistry&         types = engine::getSubsystem<TypeRegistry>();
    vector<TextureFormatFamily> listFamily;

    TextureFormatSettings settings = TextureFormatSelectionTestUtil::makeSettings();
    SW_EXPECT_FALSE( TextureFormatFamily::resolveTarget( settings, "Console", types, listFamily ) );

    settings._listTarget[0]._listFamily = { "BC", "PVRTC" };
    SW_EXPECT_FALSE( TextureFormatFamily::resolveTarget( settings, "Web", types, listFamily ) );
    SW_EXPECT_TRUE( listFamily.empty() );

    settings                     = TextureFormatSelectionTestUtil::makeSettings();
    settings._listFamily[1]._hdr = "ASTC_HDR";
    SW_EXPECT_FALSE( TextureFormatFamily::resolveTarget( settings, "Web", types, listFamily ) );
    SW_EXPECT_TRUE( listFamily.empty() );
}

/**
 * @brief [TextureFormatSelectionTest] 엔진 설정 파일의 표를 푼다 — 데스크톱 대상은 BC 하나이고, 접미사가 있는 묶음은 모두 곁 파일 규칙이 있다(없으면 쿠킹한 변형이 팩에 담기지 않는다)
 */
SW_TEST_CASE( TextureFormatSelectionTest, CompanionsCoverEveryFamilySuffix )
{
    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( FileUtil::joinPath( ProcessRoots::get()._project.str(), "Config/Engine/EngineConfig.json" ), text ) );
    JSONDocument document;
    SW_REQUIRE_TRUE( document.parse( text, "EngineConfig.json" ) );
    const JSONValue table = document.getRoot().get( "_textureFormats" );

    const JSONValue families = table.get( "_listFamily" );
    SW_REQUIRE_TRUE( families.isArray() );
    vector<string> listCompanion;
    AssetCompanionRegistry::appendCompanionPaths( "engine/textures/a.dds", listCompanion );
    for ( size_t index = 0; index < families.size(); ++index )
    {
        const string suffix = families.at( index ).get( "_suffix" ).asString();
        if ( suffix.empty() )
            continue;
        const string variant = TextureFormatFamily::makeVariantPath( "engine/textures/a.dds", suffix );
        SW_EXPECT_TRUE_MSG( VectorUtil::contains( listCompanion, variant ), "no SW_ASSET_COMPANION for the format suffix " + suffix );
        // 변형 자신은 곁 파일이 없다(`a.astc.astc.dds` 를 찾지 않는다).
        vector<string> listNested;
        AssetCompanionRegistry::appendCompanionPaths( variant, listNested );
        SW_EXPECT_FALSE( VectorUtil::contains( listNested, TextureFormatFamily::makeVariantPath( variant, suffix ) ) );
    }
    SW_EXPECT_TRUE( TextureFormatFamily::makeVariantPath( "engine/textures/a.png", ".astc" ).empty() );
}

/**
 * @brief [TextureFormatSelectionTest] 쿠킹 변형 DDS(DX10 머리의 엔진 번호)를 로더가 읽고 `Texture2D` 가 그 RHIFormat 으로 받는다 — 밉 사슬 바이트 셈이 블록 크기 표와 맞는다
 */
SW_TEST_CASE( TextureFormatSelectionTest, CookedVariantDDSRoundTrips )
{
    constexpr uint32    kWidth  = 8;
    constexpr uint32    kHeight = 8;
    const vector<uint8> payload( ( 4 + 1 + 1 + 1 ) * 16, 0x5A ); // 8x8 · 4x4 · 2x2 · 1x1 = 블록 4 + 1 + 1 + 1
    const string        root = test::makeScratchPath( "TextureVariantDDS" );
    const string        path = FileUtil::joinPath( root, "a.astc.dds" );
    SW_REQUIRE_TRUE( FileUtil::ensureParentDirectoryExists( path ) );
    SW_REQUIRE_TRUE( ImageFileWriter::writeDDS( path, payload, kWidth, kHeight, 4, DDSFormat::kDXGIASTC4x4Unorm, 32 ) );
    vector<uint8> bytes;
    SW_REQUIRE_TRUE( FileUtil::readFile( path, bytes ) );

    DDSImageData image;
    SW_REQUIRE_TRUE( DDSLoader::loadFromMemory( bytes.data(), bytes.size(), image ) );
    SW_EXPECT_EQUAL( 4u, image._mipCount );
    SW_EXPECT_TRUE( image._bCompressed != SW_FALSE );
    SW_EXPECT_TRUE( Texture2D::toRHIFormatFromDXGI( image._dxgiFormat ) == RHIFormat::ASTC_4x4_UNORM );
    SW_EXPECT_TRUE( Texture2D::toRHIFormatFromDXGI( DDSFormat::kEngineDXGIETC2RGBA8 ) == RHIFormat::ETC2_RGBA8_UNORM );
    SW_EXPECT_TRUE( Texture2D::toRHIFormatFromDXGI( DDSFormat::kEngineDXGIEACR11 ) == RHIFormat::EAC_R11_UNORM );
    SW_EXPECT_TRUE( Texture2D::toRHIFormatFromDXGI( DDSFormat::kEngineDXGIEACRG11 ) == RHIFormat::EAC_RG11_UNORM );
    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [TextureFormatSelectionTest] 세 채널 실수 이미지는 텍셀마다 알파 1 을 붙여 네 채널로 펴고, 다른 형식은 펴지 않는다
 */
SW_TEST_CASE( TextureFormatSelectionTest, ExpandsThreeChannelFloatToFour )
{
    constexpr uint32 kDXGIR32G32B32 = 6;
    const float32    arrSource[]    = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f };
    DDSImageData     image;
    image._width      = 2;
    image._height     = 1;
    image._dxgiFormat = kDXGIR32G32B32;
    image._bytes.resize( sizeof( arrSource ) );
    Memory::copy( image._bytes.data(), arrSource, sizeof( arrSource ) );

    DDSImageData expanded;
    SW_REQUIRE_TRUE( Texture2D::expandToFourChannels( image, expanded ) );
    SW_EXPECT_TRUE( Texture2D::toRHIFormatFromDXGI( expanded._dxgiFormat ) == RHIFormat::R32G32B32A32_FLOAT );
    SW_REQUIRE_EQUAL( size_t{ 32 }, expanded._bytes.size() );
    float32 arrResult[8]{};
    Memory::copy( arrResult, expanded._bytes.data(), sizeof( arrResult ) );
    const float32 arrExpected[] = { 1.0f, 2.0f, 3.0f, 1.0f, 4.0f, 5.0f, 6.0f, 1.0f };
    for ( size_t index = 0; index < 8; ++index )
    {
        SW_EXPECT_EQUAL( arrExpected[index], arrResult[index] );
    }

    DDSImageData notFloat = image;
    notFloat._dxgiFormat  = 28; // R8G8B8A8_UNORM
    SW_EXPECT_FALSE( Texture2D::expandToFourChannels( notFloat, expanded ) );
}
