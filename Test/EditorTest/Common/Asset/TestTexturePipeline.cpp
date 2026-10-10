#include "pch.h"

#include "Core/Diagnostics/MemoryProfiler.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/ImageUtil.h"
#include "Editor/Common/Asset/TextureImportConfig.h"
#include "Editor/Common/Asset/TextureImporter.h"

#include "Engine/Graphics/Texture/Texture2D.h"
#include "Engine/Resource/Image/DDSLoader.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

#if defined( TileShape )
    #undef TileShape
#endif
#if defined( CursorShape )
    #undef CursorShape
#endif
#if defined( PixmapShape )
    #undef PixmapShape
#endif

#include <DirectXTex.h>

namespace sw::editor
{
    namespace
    {
        struct TestTexturePipelineInternal
        {
            /** @brief 압축 없는 32비트 TGA(왼쪽 위 원점)를 씁니다. 픽셀은 RGBA 순서로 받습니다. */
            [[nodiscard]] static bool writeTga( const string& path, uint16 width, uint16 height, const vector<uint8>& rgbaBytes )
            {
                constexpr size_t kHeaderSize = 18;
                vector<uint8>    bytes( kHeaderSize, 0 );
                bytes[2]  = 2; // 무압축 트루컬러
                bytes[12] = static_cast<uint8>( width & 0xFFu );
                bytes[13] = static_cast<uint8>( width >> 8 );
                bytes[14] = static_cast<uint8>( height & 0xFFu );
                bytes[15] = static_cast<uint8>( height >> 8 );
                bytes[16] = 32;   // 픽셀당 비트
                bytes[17] = 0x28; // 알파 8비트 · 왼쪽 위 원점
                for ( size_t offset = 0; offset + 3 < rgbaBytes.size(); offset += 4 )
                {
                    bytes.push_back( rgbaBytes[offset + 2] );
                    bytes.push_back( rgbaBytes[offset + 1] );
                    bytes.push_back( rgbaBytes[offset + 0] );
                    bytes.push_back( rgbaBytes[offset + 3] );
                }
                FileUtil::ensureParentDirectoryExists( path );
                return FileUtil::writeFile( path, bytes.data(), bytes.size() );
            }

            /** @brief 한 색으로 채운 RGBA 픽셀 버퍼입니다. */
            static vector<uint8> makeSolidRgba( uint32 width, uint32 height, uint8 red, uint8 green, uint8 blue, uint8 alpha )
            {
                vector<uint8> rgbaBytes;
                rgbaBytes.reserve( static_cast<size_t>( width ) * height * 4 );
                for ( uint32 pixelIndex = 0; pixelIndex < width * height; ++pixelIndex )
                {
                    rgbaBytes.push_back( red );
                    rgbaBytes.push_back( green );
                    rgbaBytes.push_back( blue );
                    rgbaBytes.push_back( alpha );
                }
                return rgbaBytes;
            }

            /** @brief 위치마다 값이 다른 RGBA 픽셀 버퍼입니다(밉 · 압축이 실제로 일하게 합니다). */
            static vector<uint8> makeGradientRgba( uint32 width, uint32 height )
            {
                vector<uint8> rgbaBytes;
                rgbaBytes.reserve( static_cast<size_t>( width ) * height * 4 );
                for ( uint32 row = 0; row < height; ++row )
                {
                    for ( uint32 column = 0; column < width; ++column )
                    {
                        rgbaBytes.push_back( static_cast<uint8>( column * 255u / ( width - 1u ) ) );
                        rgbaBytes.push_back( static_cast<uint8>( row * 255u / ( height - 1u ) ) );
                        rgbaBytes.push_back( static_cast<uint8>( ( column * 7u + row * 13u ) & 0xFFu ) );
                        rgbaBytes.push_back( 255 );
                    }
                }
                return rgbaBytes;
            }

            /** @brief 임포트된 DDS 를 DirectXTex 로 풀어 첫 픽셀을 RGBA 로 돌려줍니다(sRGB 형식은 sRGB 바이트 그대로). */
            static bool decodeFirstPixel( const string& ddsPath, uint8 ( &outArrRgba )[4] )
            {
                vector<uint8> bytes;
                if ( FileUtil::readFile( ddsPath, bytes ) == false )
                    return false;

                DirectX::ScratchImage loaded;
                if ( FAILED( DirectX::LoadFromDDSMemory( bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, loaded ) ) )
                    return false;

                const DXGI_FORMAT     targetFormat = DirectX::IsSRGB( loaded.GetMetadata().format ) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
                DirectX::ScratchImage decoded;
                if ( DirectX::IsCompressed( loaded.GetMetadata().format ) )
                {
                    if ( FAILED( DirectX::Decompress( *loaded.GetImage( 0, 0, 0 ), targetFormat, decoded ) ) )
                        return false;
                }
                else if ( FAILED( DirectX::Convert( *loaded.GetImage( 0, 0, 0 ), targetFormat, DirectX::TEX_FILTER_FORCE_NON_WIC, DirectX::TEX_THRESHOLD_DEFAULT, decoded ) ) )
                {
                    return false;
                }

                const uint8* pPixel = decoded.GetImage( 0, 0, 0 )->pixels;
                for ( size_t channel = 0; channel < 4; ++channel )
                {
                    outArrRgba[channel] = pPixel[channel];
                }
                return true;
            }

            /** @brief 두 바이트가 허용 오차 안인지 봅니다(BC 압축 오차). */
            static bool isNear( uint8 actual, uint8 expected, uint8 tolerance )
            {
                const int32 difference = static_cast<int32>( actual ) - static_cast<int32>( expected );
                return -static_cast<int32>( tolerance ) <= difference && difference <= static_cast<int32>( tolerance );
            }
        };
    } // namespace

    /**
     * @brief [EditorTexturePipelineTest] TextureImportConfig 파싱, 프리셋 상속 및 규칙 매칭 검증
     */
    SW_TEST_CASE( EditorTexturePipelineTest, ImportConfigParsingAndInheritance )
    {
        const sw::string_view kJson = R"({
            "presets": {
                "Base_Default": {
                    "format": "BC7_UNORM",
                    "generate_mips": true,
                    "srgb": true,
                    "invert_green": false,
                    "swizzle": "RGBA"
                },
                "Base_NormalMap": {
                    "format": "BC5_UNORM",
                    "generate_mips": true,
                    "srgb": false,
                    "invert_green": true,
                    "swizzle": "RGBA"
                },
                "Base_UI": {
                    "format": "B8G8R8A8_UNORM",
                    "generate_mips": false,
                    "srgb": true,
                    "invert_green": false,
                    "swizzle": "BGRA"
                }
            },
            "rules": [
                {
                    "name": "Editor_Splash",
                    "inherits": "Base_UI",
                    "include_patterns": ["*splash*"],
                    "format": "B8G8R8A8_UNORM",
                    "generate_mips": false,
                    "swizzle": "BGRA"
                },
                {
                    "name": "Normal_Maps",
                    "inherits": "Base_NormalMap",
                    "include_patterns": ["*_n.*", "*_normal.*"],
                    "exclude_patterns": ["*preview*"]
                },
                {
                    "name": "UI_Textures",
                    "inherits": "Base_UI",
                    "include_paths": ["ui/", "gui/"]
                },
                {
                    "name": "Fallback_Default",
                    "inherits": "Base_Default"
                }
            ]
        })";

        TextureImportConfig config;
        SW_ASSERT_TRUE( config.loadFromJsonString( kJson ) );
        SW_EXPECT_EQUAL( 3u, static_cast<uint32>( config.getPresets().size() ) );
        SW_EXPECT_EQUAL( 4u, static_cast<uint32>( config.getRules().size() ) );

        // 1. Splash matching
        TextureImportRule splashRule;
        SW_ASSERT_TRUE( config.findMatchingRule( "editor/textures_raw/splash.png", splashRule ) );
        SW_EXPECT_EQUAL( string( "Editor_Splash" ), splashRule._name );
        SW_EXPECT_EQUAL( string( "B8G8R8A8_UNORM" ), splashRule._format );
        SW_EXPECT_EQUAL( static_cast<uint8>( TextureSwizzle::BGRA ), static_cast<uint8>( splashRule._swizzle ) );
        SW_EXPECT_EQUAL( SW_FALSE, splashRule._bGenerateMips );

        // 2. Normal map matching
        TextureImportRule normalRule;
        SW_ASSERT_TRUE( config.findMatchingRule( "characters/hero_n.png", normalRule ) );
        SW_EXPECT_EQUAL( string( "Normal_Maps" ), normalRule._name );
        SW_EXPECT_EQUAL( string( "BC5_UNORM" ), normalRule._format );
        SW_EXPECT_EQUAL( SW_TRUE, normalRule._bInvertGreen );
        SW_EXPECT_EQUAL( SW_FALSE, normalRule._bSrgb );

        // 3. Normal map preview exclusion (falls back to Fallback_Default)
        TextureImportRule previewRule;
        SW_ASSERT_TRUE( config.findMatchingRule( "characters/hero_preview_n.png", previewRule ) );
        SW_EXPECT_EQUAL( string( "Fallback_Default" ), previewRule._name );
        SW_EXPECT_EQUAL( string( "BC7_UNORM" ), previewRule._format );
        SW_EXPECT_EQUAL( SW_TRUE, previewRule._bSrgb );

        // 4. UI path matching
        TextureImportRule uiRule;
        SW_ASSERT_TRUE( config.findMatchingRule( "ui/hud/crosshair.png", uiRule ) );
        SW_EXPECT_EQUAL( string( "UI_Textures" ), uiRule._name );
        SW_EXPECT_EQUAL( string( "B8G8R8A8_UNORM" ), uiRule._format );
        SW_EXPECT_EQUAL( static_cast<uint8>( TextureSwizzle::BGRA ), static_cast<uint8>( uiRule._swizzle ) );
    }

    /**
     * @brief [EditorTexturePipelineTest] ImageUtil 디코딩, TextureImporter 변환 및 DDSLoader 로딩 E2E 검증
     */
    SW_TEST_CASE( EditorTexturePipelineTest, ImageUtilAndTextureImporterEndToEnd )
    {
        SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
        const string rawSplashPath = sw::ResourceUtil::getResourcePath( "textures_raw/splash.png" );
        if ( rawSplashPath.empty() )
        {
            // If textures_raw is not yet set in Resource priority, test direct path
            const string directPath = "Resource/editor/textures_raw/splash.png";
            if ( FileUtil::exists( directPath ) == false )
                return;
        }

        const string srcPath = rawSplashPath.empty() ? "Resource/editor/textures_raw/splash.png" : rawSplashPath;

        // 1. Decode raw image via ImageUtil
        RawImageData rawImage;
        SW_ASSERT_TRUE( ImageUtil::loadImage( srcPath, rawImage ) );
        SW_EXPECT_TRUE( rawImage.isValid() );
        SW_EXPECT_EQUAL( 1376, rawImage._width );
        SW_EXPECT_EQUAL( 768, rawImage._height );
        SW_EXPECT_EQUAL( 4, rawImage._channels );

        // 2. Import to DDS via TextureImporter
        TextureImportRule rule;
        rule._name          = "Test_Splash";
        rule._format        = "B8G8R8A8_UNORM";
        rule._swizzle       = TextureSwizzle::BGRA;
        rule._bGenerateMips = SW_FALSE;
        rule._bSrgb         = SW_TRUE;

        const string        tempOutDDS = test::makeTempPath( "test_output_splash.dds" );
        TextureImportResult importResult;
        SW_ASSERT_TRUE( TextureImporter::importTexture( srcPath, tempOutDDS, rule, &importResult ) );
        SW_EXPECT_TRUE( importResult._bSuccess );
        SW_EXPECT_EQUAL( 1376u, importResult._width );
        SW_EXPECT_EQUAL( 768u, importResult._height );
        SW_EXPECT_EQUAL( 1u, importResult._mipCount );
        SW_EXPECT_TRUE( FileUtil::exists( tempOutDDS ) );

        // 3. Verify generated DDS with Engine DDSLoader
        DDSImageData ddsData;
        SW_ASSERT_TRUE( DDSLoader::loadFromFile( tempOutDDS, ddsData ) );
        SW_EXPECT_TRUE( ddsData.isValid() );
        SW_EXPECT_EQUAL( 1376u, ddsData._width );
        SW_EXPECT_EQUAL( 768u, ddsData._height );
        SW_EXPECT_EQUAL( 91u, ddsData._dxgiFormat ); // B8G8R8A8_UNORM_SRGB
        SW_EXPECT_TRUE( ddsData._bIsBgra );
        SW_EXPECT_EQUAL( static_cast<size_t>( 1376 * 768 * 4 ), ddsData._bytes.size() );

        // 4. Cleanup
        SW_EXPECT_TRUE( FileUtil::removeFile( tempOutDDS ) );
    }

    /**
     * @brief [EditorTexturePipelineTest] 스위즐이 바이트를 섞는 방식과 그 결과를 부르는 포맷 이름이 한 자리에서 나온다
     * @details 섞기와 포맷 이름을 따로 정하면 한쪽만 아는 스위즐이 생긴다 — 바이트는 옮겨졌는데 결과물은
     *          "RGBA 다" 라고 적혀 나가 색이 깨진다. `ARGB` 섞기가 왼쪽 회전이면 RGBA 를 GBAR 로 만든다
     *          (ARGB 는 어떤 읽기로도 그게 아니다).
     */
    SW_TEST_CASE( EditorTexturePipelineTest, SwizzleLayoutAndFormatAgree )
    {
        // stb 는 언제나 RGBA 로 디코딩한다 — 섞기의 입력은 항상 이 순서다.
        const uint8 arrSourcePixel[4] = { 10, 20, 30, 40 };

        struct SwizzleCase
        {
            TextureSwizzle _swizzle;
            const utf8*    _pName;
            uint8          _arrExpected[4];
            const utf8*    _pFormat;            ///< 이 배치를 부르는 가져오기 규칙의 포맷 이름
            uint32         _expectedDxgiFormat; ///< sRGB 없이 임포트된 DDS 가 들고 있어야 할 포맷
        };

        // BGRA 와 ARGB 는 **같은 것**이다. D3D9 의 D3DFMT_A8R8G8B8 은 메모리에서 B,G,R,A 이고,
        // DXGI 가 그것을 B8G8R8A8 이라 부른다 — 열거형 주석의 "레거시 ARGB" 가 그 뜻이다.
        const SwizzleCase arrCase[] = {
            {TextureSwizzle::RGBA, "RGBA",  { 10, 20, 30, 40 }, "R8G8B8A8_UNORM", 28u},
            {TextureSwizzle::BGRA, "BGRA",  { 30, 20, 10, 40 }, "B8G8R8A8_UNORM", 87u},
            {TextureSwizzle::ARGB, "ARGB",  { 30, 20, 10, 40 }, "B8G8R8A8_UNORM", 87u}, // 레거시 이름, 같은 배치
            {TextureSwizzle::RGB1, "RGB1", { 10, 20, 30, 255 }, "R8G8B8A8_UNORM", 28u}, // 알파만 불투명으로
        };

        for ( const SwizzleCase& testCase : arrCase )
        {
            RawImageData image;
            image._width  = 1;
            image._height = 1;
            image._bytes.assign( arrSourcePixel, arrSourcePixel + 4 );

            TextureImportRule rule;
            rule._swizzle = testCase._swizzle;

            TextureImporter::applyChannelManipulations( image, rule, 1 );

            for ( size_t channel = 0; channel < 4; ++channel )
            {
                SW_EXPECT_TRUE_MSG( testCase._arrExpected[channel] == image._bytes[channel], testCase._pName );
            }

            // 그 배치를 부르는 포맷으로 구우면 DDS 는 그 포맷을 달고 바이트는 섞인 그대로다. 배치와 포맷 이름이 어긋나면
            // 임포트의 포맷 변환이 바이트를 한 번 더 섞는다.
            const string sourcePath = test::makeTempPath( string( testCase._pName ) + ".tga" );
            SW_ASSERT_TRUE( TestTexturePipelineInternal::writeTga( sourcePath, 1, 1, vector<uint8>( arrSourcePixel, arrSourcePixel + 4 ) ) );
            rule._format         = testCase._pFormat;
            rule._bSrgb          = SW_FALSE;
            rule._bGenerateMips  = SW_FALSE;
            const string ddsPath = test::makeTempPath( string( testCase._pName ) + ".dds" );
            SW_ASSERT_TRUE_MSG( TextureImporter::importTexture( sourcePath, ddsPath, rule ), testCase._pName );

            DDSImageData dds;
            SW_ASSERT_TRUE_MSG( DDSLoader::loadFromFile( ddsPath, dds ), testCase._pName );
            SW_EXPECT_TRUE_MSG( testCase._expectedDxgiFormat == dds._dxgiFormat, testCase._pName );
            SW_ASSERT_TRUE_MSG( dds._bytes.size() == 4, testCase._pName );
            for ( size_t channel = 0; channel < 4; ++channel )
            {
                SW_EXPECT_TRUE_MSG( testCase._arrExpected[channel] == dds._bytes[channel], testCase._pName );
            }
        }
    }

    /**
     * @brief [EditorTexturePipelineTest] 그린 반전이 어떤 스위즐에서도 초록에 걸린다
     * @details 스위즐이 초록을 0번으로 보낸 뒤 반전이 그대로 1번을 뒤집으면 **파랑을 뒤집고 초록은 그대로 남긴다.**
     *          이 테스트가 보는 것이 그것이다.
     * @note 반전은 섞기 **앞**에 한다. 지금 스위즐들은 모두 초록을 1번에 두므로 순서를 바꿔도 결과가 같고,
     *       그래서 **이 테스트는 순서를 구별하지 못한다.** 새 스위즐이 초록을 옮기면 순서가 결과를 가른다.
     */
    SW_TEST_CASE( EditorTexturePipelineTest, InvertGreenHitsGreenUnderEverySwizzle )
    {
        const uint8 arrSourcePixel[4] = { 10, 20, 30, 40 };

        const TextureSwizzle arrSwizzle[] = {
            TextureSwizzle::RGBA, TextureSwizzle::BGRA, TextureSwizzle::ARGB, TextureSwizzle::RGB1 };

        for ( const TextureSwizzle swizzle : arrSwizzle )
        {
            RawImageData image;
            image._width  = 1;
            image._height = 1;
            image._bytes.assign( arrSourcePixel, arrSourcePixel + 4 );

            TextureImportRule rule;
            rule._swizzle      = swizzle;
            rule._bInvertGreen = SW_TRUE;

            TextureImporter::applyChannelManipulations( image, rule, 1 );

            // 초록(20)은 어디에 놓이든 235 가 되어야 하고, 나머지 채널은 그대로다.
            bool bFoundInvertedGreen = false;
            for ( size_t channel = 0; channel < 4; ++channel )
            {
                if ( image._bytes[channel] == uint8( 235 ) )
                    bFoundInvertedGreen = true;
                SW_EXPECT_TRUE( image._bytes[channel] != uint8( 20 ) );
            }
            SW_EXPECT_TRUE( bFoundInvertedGreen );
        }
    }

    /**
     * @brief [EditorTexturePipelineTest] 찾지 못한 inherits 는 로드 오류다 — 반쯤 읽은 표를 남기지 않는다
     * @details 이름 오타나 **부모를 아래쪽에 적는 것**(찾기는 그 시점까지 파싱된 프리셋만 본다)이 상속을 통째로 지운 채 기본값으로 임포트되면
     *          아무도 알 수 없다. 프리셋 쪽 · 규칙 쪽 모두 같다.
     */
    SW_TEST_CASE( EditorTexturePipelineTest, UnresolvedInheritsIsALoadError )
    {
        SW_TEST_DEFENSIVE_SCOPE( "inherits 가 없는 프리셋을 가리키는 설정을 일부러 읽는다" );
        const sw::string_view arrJson[] = {
            // 부모를 뒤에 적기
            R"({ "presets": { "Uses_Later": { "inherits": "Declared_Below" }, "Declared_Below": { "format": "BC5_UNORM" } } })",
            // 규칙의 오타
            R"({ "presets": { "Base_UI": { "format": "B8G8R8A8_UNORM" } }, "rules": [ { "name": "Typo_Rule", "inherits": "Base_UI_TYPO" } ] })",
        };
        for ( const sw::string_view json : arrJson )
        {
            test::ScopedLogCollector logs;
            TextureImportConfig      config;
            SW_EXPECT_FALSE_MSG( config.loadFromJsonString( json ), sw::string( json ) );
            SW_EXPECT_TRUE( config.getPresets().empty() );
            SW_EXPECT_TRUE( config.getRules().empty() );
            SW_EXPECT_TRUE_MSG( logs.countContaining( "inherits" ) >= 1u, sw::string( json ) + logs.joined() );
        }
    }

    /**
     * @brief [EditorTexturePipelineTest] 모르는 키 · 모르는 swizzle · 객체 아닌 규칙 · 뿌리의 모르는 키는 로드 오류다(기본값으로 조용히 가지 않는다)
     * @details 객체 아닌 원소를 규칙으로 받으면 조건 없는 규칙이 되어 뒤 규칙을 모두 가리고, 모르는 swizzle 을 RGBA 로 읽으면 채널이 뒤바뀐 채 임포트된다.
     */
    SW_TEST_CASE( EditorTexturePipelineTest, InvalidRuleIsALoadError )
    {
        SW_TEST_DEFENSIVE_SCOPE( "틀린 임포트 설정을 일부러 읽는다" );
        const sw::string_view arrJson[] = {
            R"({ "rules": [ { "fromat": "BC7_UNORM" } ] })",
            R"({ "rules": [ { "swizzle": "XYZW" } ] })",
            R"({ "rules": [ 3 ] })",
            R"({ "rulez": [] })",
            R"({ "presets": { "Base": { "srgbb": true } } })",
        };
        for ( const sw::string_view json : arrJson )
        {
            TextureImportConfig config;
            SW_EXPECT_FALSE_MSG( config.loadFromJsonString( json ), sw::string( json ) );
            SW_EXPECT_TRUE( config.getRules().empty() );
            SW_EXPECT_TRUE( config.getPresets().empty() );
        }
    }

    /**
     * @brief [EditorTexturePipelineTest] 중간의 조건 없는 규칙은 뒤 규칙을 가린다 — 로드는 되고, 진단이 그 자리를 짚는다
     * @details 매칭은 "첫 승" 이라 무엇에나 맞는 규칙이 중간에 있으면 그 뒤 규칙은 절대 선택되지 않는다. 형식은 맞으니 오류가 아니라 경고다.
     */
    SW_TEST_CASE( EditorTexturePipelineTest, MidListCatchAllRuleShadowingIsReported )
    {
        const sw::string_view kJson = R"({
            "rules": [
                { "name": "Normal_Maps", "include_patterns": ["*_n.*"] },
                { "name": "Catch_All" },
                { "name": "UI_Textures", "include_paths": ["ui/"] }
            ]
        })";

        test::ScopedLogSuppressor suppressor;
        TextureImportConfig       config;
        SW_ASSERT_TRUE( config.loadFromJsonString( kJson ) );
        SW_ASSERT_EQUAL( size_t( 3 ), config.getRules().size() );
        SW_EXPECT_TRUE( TextureImportConfig::isCatchAllRule( config.getRules()[1] ) );
        SW_EXPECT_EQUAL( size_t( 1 ), config.findShadowingRuleIndex() );

        TextureImportRule matched;
        SW_ASSERT_TRUE( config.findMatchingRule( "ui/button.png", matched ) );
        SW_EXPECT_EQUAL( sw::string( "Catch_All" ), matched._name );
    }

    /**
     * @brief [EditorTexturePipelineTest] 맨 끝의 조건 없는 규칙은 **정상**이다 — 경고하지 않는다
     * @details `Fallback_Default` 처럼 마지막에 두는 캐치올은 이 설정의 정상적인 쓰임이다. 진단이
     *          "조건이 없다" 만 보고 짖으면 멀쩡한 설정마다 경고가 떠서 아무도 안 읽게 된다 —
     *          그래서 **"조건이 없는데 뒤에 뭔가 더 있다"** 일 때만 짚는다. 그 경계를 여기서 지킨다.
     */
    SW_TEST_CASE( EditorTexturePipelineTest, TrailingCatchAllRuleIsNotReported )
    {
        const sw::string_view kJson = R"({
            "rules": [
                { "name": "Normal_Maps", "include_patterns": ["*_n.*"] },
                { "name": "Fallback_Default" }
            ]
        })";

        TextureImportConfig config;
        SW_ASSERT_TRUE( config.loadFromJsonString( kJson ) );
        SW_ASSERT_EQUAL( size_t( 2 ), config.getRules().size() );

        // 캐치올이지만 맨 끝이라 가리는 것이 없다.
        SW_EXPECT_TRUE( TextureImportConfig::isCatchAllRule( config.getRules()[1] ) );
        SW_EXPECT_EQUAL( config.getRules().size(), config.findShadowingRuleIndex() );

        // 앞 규칙은 살아 있고, 걸리지 않는 것은 폴백으로 간다 — 이것이 의도된 쓰임이다.
        TextureImportRule normalRule;
        SW_ASSERT_TRUE( config.findMatchingRule( "characters/hero_n.png", normalRule ) );
        SW_EXPECT_EQUAL( sw::string( "Normal_Maps" ), normalRule._name );

        TextureImportRule otherRule;
        SW_ASSERT_TRUE( config.findMatchingRule( "characters/hero_albedo.png", otherRule ) );
        SW_EXPECT_EQUAL( sw::string( "Fallback_Default" ), otherRule._name );
    }

    /**
     * @brief [EditorTexturePipelineTest] sRGB 규칙은 원본 바이트에 sRGB 형식 이름만 붙인다 — 감마를 한 번 더 씌우지 않는다
     * @details 원본 이미지(PNG · JPG)의 바이트는 이미 sRGB 로 인코딩돼 있다. 그것을 UNORM 으로 읽어 UNORM_SRGB 로 "변환" 하면
     *          DirectXTex 가 선형 → sRGB 곡선을 한 번 더 적용해 화면이 밝게 뜬다. 무압축(B8G8R8A8)과 BC7 둘 다 본다.
     */
    SW_TEST_CASE( EditorTexturePipelineTest, SrgbRuleLabelsBytesWithoutReencodingThem )
    {
        const string sourcePath = test::makeTempPath( "srgb_source.tga" );
        SW_ASSERT_TRUE( TestTexturePipelineInternal::writeTga( sourcePath, 4, 4, TestTexturePipelineInternal::makeSolidRgba( 4, 4, 128, 64, 200, 255 ) ) );

        TextureImportRule rule;
        rule._format        = "B8G8R8A8_UNORM";
        rule._swizzle       = TextureSwizzle::BGRA;
        rule._bGenerateMips = SW_FALSE;
        rule._bSrgb         = SW_TRUE;

        const string bgraPath = test::makeTempPath( "srgb_bgra.dds" );
        SW_ASSERT_TRUE( TextureImporter::importTexture( sourcePath, bgraPath, rule ) );

        DDSImageData bgraImage;
        SW_ASSERT_TRUE( DDSLoader::loadFromFile( bgraPath, bgraImage ) );
        SW_EXPECT_EQUAL( 91u, bgraImage._dxgiFormat ); // B8G8R8A8_UNORM_SRGB
        SW_ASSERT_TRUE( bgraImage._bytes.size() >= 4 );
        SW_EXPECT_EQUAL( 200, static_cast<int32>( bgraImage._bytes[0] ) );
        SW_EXPECT_EQUAL( 64, static_cast<int32>( bgraImage._bytes[1] ) );
        SW_EXPECT_EQUAL( 128, static_cast<int32>( bgraImage._bytes[2] ) );
        SW_EXPECT_EQUAL( 255, static_cast<int32>( bgraImage._bytes[3] ) );

        rule._format  = "BC7_UNORM";
        rule._swizzle = TextureSwizzle::RGBA;

        const string bc7Path = test::makeTempPath( "srgb_bc7.dds" );
        SW_ASSERT_TRUE( TextureImporter::importTexture( sourcePath, bc7Path, rule ) );

        uint8 arrRgba[4] = {};
        SW_ASSERT_TRUE( TestTexturePipelineInternal::decodeFirstPixel( bc7Path, arrRgba ) );
        SW_EXPECT_TRUE_MSG( TestTexturePipelineInternal::isNear( arrRgba[0], 128, 2 ), "BC7 R 이 원본(128)에서 벗어났습니다 — sRGB 곡선이 한 번 더 씌워졌습니다" );
        SW_EXPECT_TRUE( TestTexturePipelineInternal::isNear( arrRgba[1], 64, 2 ) );
        SW_EXPECT_TRUE( TestTexturePipelineInternal::isNear( arrRgba[2], 200, 2 ) );
    }

    /**
     * @brief [EditorTexturePipelineTest] 같은 원본 · 같은 규칙은 같은 바이트를 낸다(밉 · BC7 포함)
     * @details 임포트된 DDS 를 커밋하고 그 해시를 스탬프에 적으므로 임포트가 흔들리면 스탬프가 매번 어긋난다. 밉은 WIC 를 쓰지 않는
     *          필터로 만들어 Windows · Linux 에서도 같은 결과를 낸다.
     */
    SW_TEST_CASE( EditorTexturePipelineTest, ImportingTheSameSourceTwiceGivesTheSameBytes )
    {
        const string sourcePath = test::makeTempPath( "determinism_source.tga" );
        SW_ASSERT_TRUE( TestTexturePipelineInternal::writeTga( sourcePath, 8, 8, TestTexturePipelineInternal::makeGradientRgba( 8, 8 ) ) );

        TextureImportRule   rule; // 기본 규칙: BC7 sRGB + 밉
        const string        firstPath  = test::makeTempPath( "determinism_first.dds" );
        const string        secondPath = test::makeTempPath( "determinism_second.dds" );
        TextureImportResult result;
        SW_ASSERT_TRUE( TextureImporter::importTexture( sourcePath, firstPath, rule, &result ) );
        SW_ASSERT_TRUE( TextureImporter::importTexture( sourcePath, secondPath, rule ) );
        SW_EXPECT_EQUAL( 4u, result._mipCount ); // 8 → 1 (Debug 의 BC7 은 블록당 수백 ms 라 작게 둔다)

        vector<uint8> firstBytes;
        vector<uint8> secondBytes;
        SW_ASSERT_TRUE( FileUtil::readFile( firstPath, firstBytes ) );
        SW_ASSERT_TRUE( FileUtil::readFile( secondPath, secondBytes ) );
        SW_EXPECT_TRUE( firstBytes.empty() == false );
        SW_EXPECT_TRUE( firstBytes == secondBytes );
    }

    // ------------------------------------------------------------------------------
    // TextureImportStampTest — 원본(textures_raw)과 임포트된 DDS(textures)가 맞는지 내용 해시로 본다
    // ------------------------------------------------------------------------------

    /**
     * @brief [TextureImportStampTest] 저장소의 모든 원본 텍스처가 커밋된 DDS 와 스탬프로 맞는다
     * @details 깨끗한 클론 · CI 에서 "원본을 고치고 임포트하지 않았다" 를 잡는 자리다. 지면 `App --import-textures` 로 임포트하고 DDS 와
     *          `textures_raw/import.stamp` 를 함께 커밋한다.
     */
    SW_TEST_CASE( TextureImportStampTest, RepositoryRawTexturesMatchTheirDDS )
    {
        SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
        TextureImportConfig config;
        SW_ASSERT_TRUE( config.loadFromFile( TextureImporter::makeDefaultImportConfigPath() ) );

        const AssetImportSummary summary = TextureImporter::importAllTextures( sw::ResourceUtil::getRootFolderPath(), config, AssetImportMode::CheckOnly );
        string                   problemText;
        for ( const string& problem : summary._listProblem )
        {
            problemText += problem;
            problemText += "\n";
        }
        SW_EXPECT_TRUE( summary._sourceCount > 0 ); // editor/textures_raw/splash.png
        SW_EXPECT_TRUE_MSG( summary.isClean(), problemText.c_str() );
        SW_EXPECT_EQUAL( 0u, summary._importedCount );
    }

    /**
     * @brief [TextureImportStampTest] 원본 경로는 같은 도메인의 `textures/` 아래 `.dds` 로 대응한다
     */
    SW_TEST_CASE( TextureImportStampTest, RawPathMapsToTheTexturesFolder )
    {
        SW_EXPECT_STREQ( "editor/textures/splash.dds", TextureImporter::makeImportedTexturePath( "editor/textures_raw/splash.png" ).c_str() );
        SW_EXPECT_STREQ( "D:/r/game/x/textures/ui/hud/icon.dds", TextureImporter::makeImportedTexturePath( "D:\\r\\game\\x\\textures_raw\\ui\\hud\\icon.png" ).c_str() );
        SW_EXPECT_TRUE( TextureImporter::makeImportedTexturePath( "engine/textures/white.png" ).empty() );
        SW_EXPECT_TRUE( TextureImporter::makeImportedTexturePath( "engine/my_textures_raw_backup/a.png" ).empty() );
    }

    /**
     * @brief [TextureImportStampTest] 임포트하지 않은 원본 · 고친 원본 · 바꾼 규칙 · 손댄 DDS · 사라진 원본을 모두 어긋남으로 잡고, 임포트가 그것을 닫는다
     */
    SW_TEST_CASE( TextureImportStampTest, CheckReportsEveryKindOfDriftAndImportClosesIt )
    {
        const string resourceRoot = test::makeTempDirectory( "import_stamp_resource" );
        const string sourcePath   = FileUtil::joinPath( resourceRoot, "game/probe/textures_raw/ui/icon.tga" );
        const string ddsPath      = FileUtil::joinPath( resourceRoot, "game/probe/textures/ui/icon.dds" );
        const string stampPath    = FileUtil::joinPath( resourceRoot, "game/probe/textures_raw/import.stamp" );
        SW_ASSERT_TRUE( TestTexturePipelineInternal::writeTga( sourcePath, 4, 4, TestTexturePipelineInternal::makeSolidRgba( 4, 4, 10, 20, 30, 255 ) ) );

        TextureImportConfig uiConfig;
        SW_ASSERT_TRUE( uiConfig.loadFromJsonString( R"({ "rules": [ { "name": "UI", "format": "B8G8R8A8_UNORM", "swizzle": "BGRA", "generate_mips": false } ] })" ) );

        // 1) 한 번도 임포트하지 않았다 — 보고만 하고 아무것도 쓰지 않는다.
        AssetImportSummary summary = TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::CheckOnly );
        SW_EXPECT_EQUAL( 1u, summary._sourceCount );
        SW_EXPECT_EQUAL( size_t( 1 ), summary._listProblem.size() );
        SW_EXPECT_FALSE( FileUtil::exists( ddsPath ) );
        SW_EXPECT_FALSE( FileUtil::exists( stampPath ) );

        // 2) 임포트한다 → DDS 와 스탬프가 생기고, 다시 보면 맞는다. 맞는 것은 다시 임포트하지 않는다.
        summary = TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::ImportStale );
        SW_EXPECT_TRUE( summary.isClean() );
        SW_EXPECT_EQUAL( 1u, summary._importedCount );
        SW_EXPECT_TRUE( FileUtil::exists( ddsPath ) );
        SW_EXPECT_TRUE( FileUtil::exists( stampPath ) );
        SW_EXPECT_TRUE( TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::CheckOnly ).isClean() );
        SW_EXPECT_EQUAL( 0u, TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::ImportStale )._importedCount );

        // 3) 원본을 고쳤다.
        SW_ASSERT_TRUE( TestTexturePipelineInternal::writeTga( sourcePath, 4, 4, TestTexturePipelineInternal::makeSolidRgba( 4, 4, 11, 20, 30, 255 ) ) );
        SW_EXPECT_EQUAL( size_t( 1 ), TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::CheckOnly )._listProblem.size() );
        SW_EXPECT_EQUAL( 1u, TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::ImportStale )._importedCount );

        // 4) 규칙만 바꿨다(원본은 그대로).
        TextureImportConfig srgbConfig;
        SW_ASSERT_TRUE( srgbConfig.loadFromJsonString( R"({ "rules": [ { "name": "UI", "format": "B8G8R8A8_UNORM", "swizzle": "BGRA", "generate_mips": false, "srgb": false } ] })" ) );
        SW_EXPECT_EQUAL( size_t( 1 ), TextureImporter::importAllTextures( resourceRoot, srgbConfig, AssetImportMode::CheckOnly )._listProblem.size() );
        SW_EXPECT_TRUE( TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::CheckOnly ).isClean() );

        // 5) DDS 를 손으로 바꿨다.
        vector<uint8> ddsBytes;
        SW_ASSERT_TRUE( FileUtil::readFile( ddsPath, ddsBytes ) );
        ddsBytes.back() = static_cast<uint8>( ddsBytes.back() ^ 0xFFu );
        SW_ASSERT_TRUE( FileUtil::writeFile( ddsPath, ddsBytes.data(), ddsBytes.size() ) );
        SW_EXPECT_EQUAL( size_t( 1 ), TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::CheckOnly )._listProblem.size() );
        SW_EXPECT_EQUAL( 1u, TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::ImportStale )._importedCount );

        // 6) 원본이 사라졌다 — 스탬프 줄이 남은 것이 어긋남이고, 임포트는 그 줄을 지운다(DDS 는 사람이 정리한다).
        SW_ASSERT_TRUE( FileUtil::removeFile( sourcePath ) );
        SW_EXPECT_EQUAL( size_t( 1 ), TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::CheckOnly )._listProblem.size() );
        summary = TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::ImportStale );
        SW_EXPECT_TRUE( summary.isClean() );
        SW_EXPECT_EQUAL( 0u, summary._sourceCount );
        SW_EXPECT_TRUE( TextureImporter::importAllTextures( resourceRoot, uiConfig, AssetImportMode::CheckOnly ).isClean() );
        SW_EXPECT_TRUE( FileUtil::exists( ddsPath ) );
    }

    /**
     * @brief [TextureImportStampTest] HDR 원본에 8 비트 포맷 규칙이 걸리면 잘라 임포트하지 않고 실패로 보고한다
     */
    SW_TEST_CASE( TextureImportStampTest, HdrSourceIsReportedNotTruncated )
    {
        const string resourceRoot = test::makeTempDirectory( "import_stamp_hdr" );
        const string sourcePath   = FileUtil::joinPath( resourceRoot, "engine/textures_raw/sky.hdr" );
        FileUtil::ensureParentDirectoryExists( sourcePath );
        SW_ASSERT_TRUE( FileUtil::writeTextFile( sourcePath, "#?RADIANCE\n" ) );

        TextureImportConfig config;
        SW_ASSERT_TRUE( config.loadFromJsonString( R"({ "rules": [ { "name": "Any" } ] })" ) );
        const AssetImportSummary summary = TextureImporter::importAllTextures( resourceRoot, config, AssetImportMode::ImportStale );
        SW_EXPECT_EQUAL( 1u, summary._sourceCount );
        SW_EXPECT_EQUAL( 0u, summary._importedCount );
        SW_EXPECT_EQUAL( size_t( 1 ), summary._listProblem.size() );
        SW_EXPECT_FALSE( FileUtil::exists( FileUtil::joinPath( resourceRoot, "engine/textures/sky.dds" ) ) );
    }

    /**
     * @brief [EditorTexturePipelineTest] `.hdr` 원본은 1 을 넘는 값을 지닌 채 BC6H 로 임포트된다 — 8 비트로 잘리지 않는다
     * @details 4x4 Radiance 파일(무압축 RGBE 줄)을 쓰고 bc6h 규칙으로 임포트한 뒤 DirectXTex 로 풀어 픽셀이 4.0 근처인지 본다. 8 비트 포맷 규칙은 실패여야 한다.
     */
    SW_TEST_CASE( EditorTexturePipelineTest, HdrSourceKeepsValuesAboveOne )
    {
        // RGBE: 값 = 가수 / 256 × 2^(지수 − 128). (128, 128, 128, 131) = 0.5 × 8 = 4.0.
        const string  sourcePath = test::makeTempPath( "bright.hdr" );
        const string  header     = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 4\n";
        vector<uint8> bytes( header.begin(), header.end() );
        for ( uint32 pixel = 0; pixel < 16; ++pixel )
        {
            const uint8 arrRgbe[4] = { 128, 128, 128, 131 };
            bytes.insert( bytes.end(), arrRgbe, arrRgbe + 4 );
        }
        SW_ASSERT_TRUE( FileUtil::writeFile( sourcePath, bytes.data(), bytes.size() ) );

        TextureImportRule rule;
        rule._format         = "bc6h";
        rule._bSrgb          = SW_FALSE;
        rule._bGenerateMips  = SW_FALSE;
        const string ddsPath = test::makeTempPath( "bright.dds" );
        SW_ASSERT_TRUE( TextureImporter::importTexture( sourcePath, ddsPath, rule ) );

        DDSImageData dds;
        SW_ASSERT_TRUE( DDSLoader::loadFromFile( ddsPath, dds ) );
        SW_EXPECT_EQUAL( static_cast<uint32>( DXGI_FORMAT_BC6H_UF16 ), dds._dxgiFormat );
        SW_EXPECT_TRUE( Texture2D::toRhiFormatFromDxgi( dds._dxgiFormat ) == RHIFormat::BC6H_UF16 );
        SW_ASSERT_TRUE( dds._bytes.size() >= 16 );
        // 풀어서 값을 본다 — BC6H 블록 하나(4x4)의 첫 픽셀.
        DirectX::Image block{};
        block.width      = 4;
        block.height     = 4;
        block.format     = DXGI_FORMAT_BC6H_UF16;
        block.rowPitch   = 16;
        block.slicePitch = 16;
        block.pixels     = dds._bytes.data();
        DirectX::ScratchImage decoded;
        SW_ASSERT_TRUE( SUCCEEDED( DirectX::Decompress( block, DXGI_FORMAT_R32G32B32A32_FLOAT, decoded ) ) );
        const float32* pPixel = reinterpret_cast<const float32*>( decoded.GetPixels() );
        SW_EXPECT_NEAR_EQUAL( 4.0f, pPixel[0], 0.1f );
        SW_EXPECT_NEAR_EQUAL( 4.0f, pPixel[1], 0.1f );
        SW_EXPECT_NEAR_EQUAL( 4.0f, pPixel[2], 0.1f );

        // 무압축 HDR(rgba16f)도 같은 갈래다.
        rule._format             = "rgba16f";
        const string halfDDSPath = test::makeTempPath( "bright_rgba16f.dds" );
        SW_ASSERT_TRUE( TextureImporter::importTexture( sourcePath, halfDDSPath, rule ) );
        DDSImageData halfDDS;
        SW_ASSERT_TRUE( DDSLoader::loadFromFile( halfDDSPath, halfDDS ) );
        SW_EXPECT_TRUE( Texture2D::toRhiFormatFromDxgi( halfDDS._dxgiFormat ) == RHIFormat::R16G16B16A16_FLOAT );

        // 8 비트 포맷 규칙은 거절한다(자르지 않는다).
        rule._format = "bc7";
        SW_EXPECT_FALSE( TextureImporter::importTexture( sourcePath, test::makeTempPath( "bright_bc7.dds" ), rule ) );
    }

    /**
     * @brief [EditorTexturePipelineTest] stb_image 의 디코드 버퍼는 sw 할당자로 잡혀 그때의 메모리 태그로 세인다
     * @details 64x64 RGBA TGA 를 읽으면 stb 가 16 KB 디코드 버퍼를 잡고(`STBI_MALLOC`) 결과를 `RawImageData` 로 16 KB 더 복사한다. stb 가 CRT malloc 을
     *          쓰면 태그 줄에는 복사본 몫만 늘어난다.
     */
    SW_TEST_CASE( EditorTexturePipelineTest, ImageDecodeBufferIsTagged )
    {
        if constexpr ( kMemoryTagScopesEnabled == false )
            SW_TEST_SKIP( "memory tag scopes are compiled out in this configuration" );
        const MemoryProfiler* pProfiler = MemoryProfiler::getActive();
        if ( pProfiler == nullptr || pProfiler->isTrackingEnabled() == false )
            SW_TEST_SKIP( "no tracking memory profiler in this host" );

        constexpr uint32 kSide       = 64;
        constexpr size_t kPixelBytes = static_cast<size_t>( kSide ) * kSide * 4;
        // 무압축 트루컬러 TGA: 18 바이트 머리 + BGRA 픽셀(위에서 아래로).
        vector<uint8> tgaBytes( 18 + kPixelBytes, uint8{ 0x7F } );
        Memory::set( tgaBytes.data(), 0, 18 );
        tgaBytes[2]  = 2;
        tgaBytes[12] = static_cast<uint8>( kSide );
        tgaBytes[14] = static_cast<uint8>( kSide );
        tgaBytes[16] = 32;
        tgaBytes[17] = 0x28;

        const uint64 totalBefore = pProfiler->getStats( MemoryTag::Animation )._totalAllocatedBytes.load();
        RawImageData image;
        {
            SW_MEMORY_SCOPE( Animation );
            SW_ASSERT_TRUE( ImageUtil::loadImageFromMemory( tgaBytes.data(), tgaBytes.size(), image ) );
        }
        const uint64 totalGrowth = pProfiler->getStats( MemoryTag::Animation )._totalAllocatedBytes.load() - totalBefore;
        SW_EXPECT_EQUAL( static_cast<int32>( kSide ), image._width );
        SW_EXPECT_TRUE_MSG( totalGrowth >= 2 * kPixelBytes, ( string( "Animation bytes allocated while decoding: " ) + to_string( totalGrowth ) ).c_str() );
    }

} // namespace sw::editor
