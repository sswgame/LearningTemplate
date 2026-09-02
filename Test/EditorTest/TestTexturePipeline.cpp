#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Editor/Common/Asset/ImageUtil.h"
#include "Editor/Common/Asset/TextureBaker.h"
#include "Editor/Common/Asset/TextureImportConfig.h"
#include "Editor/Common/Asset/TextureWatcher.h"

#include "Engine/Resource/DdsLoader.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

namespace sw::editor
{
    /**
     * @brief [Editor_TexturePipeline] TextureImportConfig 파싱, 프리셋 상속 및 규칙 매칭 검증
     */
    SW_TEST_CASE( Editor_TexturePipeline, ImportConfigParsingAndInheritance )
    {
        const string_view kJson = R"({
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
        SW_ASSERT_TRUE( config.matchRule( "editor/textures_raw/splash.jpg", splashRule ) );
        SW_EXPECT_EQUAL( string( "Editor_Splash" ), splashRule._name );
        SW_EXPECT_EQUAL( string( "B8G8R8A8_UNORM" ), splashRule._format );
        SW_EXPECT_EQUAL( static_cast<uint8>( TextureSwizzle::BGRA ), static_cast<uint8>( splashRule._swizzle ) );
        SW_EXPECT_EQUAL( SW_FALSE, splashRule._bGenerateMips );

        // 2. Normal map matching
        TextureImportRule normalRule;
        SW_ASSERT_TRUE( config.matchRule( "characters/hero_n.png", normalRule ) );
        SW_EXPECT_EQUAL( string( "Normal_Maps" ), normalRule._name );
        SW_EXPECT_EQUAL( string( "BC5_UNORM" ), normalRule._format );
        SW_EXPECT_EQUAL( SW_TRUE, normalRule._bInvertGreen );
        SW_EXPECT_EQUAL( SW_FALSE, normalRule._bSrgb );

        // 3. Normal map preview exclusion (falls back to Fallback_Default)
        TextureImportRule previewRule;
        SW_ASSERT_TRUE( config.matchRule( "characters/hero_preview_n.png", previewRule ) );
        SW_EXPECT_EQUAL( string( "Fallback_Default" ), previewRule._name );
        SW_EXPECT_EQUAL( string( "BC7_UNORM" ), previewRule._format );
        SW_EXPECT_EQUAL( SW_TRUE, previewRule._bSrgb );

        // 4. UI path matching
        TextureImportRule uiRule;
        SW_ASSERT_TRUE( config.matchRule( "ui/hud/crosshair.png", uiRule ) );
        SW_EXPECT_EQUAL( string( "UI_Textures" ), uiRule._name );
        SW_EXPECT_EQUAL( string( "B8G8R8A8_UNORM" ), uiRule._format );
        SW_EXPECT_EQUAL( static_cast<uint8>( TextureSwizzle::BGRA ), static_cast<uint8>( uiRule._swizzle ) );
    }

    /**
     * @brief [Editor_TexturePipeline] ImageUtil 디코딩, TextureBaker 변환 및 DdsLoader 로딩 E2E 검증
     */
    SW_TEST_CASE( Editor_TexturePipeline, ImageUtilAndTextureBakerEndToEnd )
    {
        sw::ResourceUtil::initialize();
        const string rawSplashPath = sw::ResourceUtil::getResourcePath( "textures_raw/splash.jpg" );
        if ( rawSplashPath.empty() )
        {
            // If textures_raw is not yet set in Resource priority, test direct path
            const string directPath = "Resource/editor/textures_raw/splash.jpg";
            if ( FileUtil::fileExists( directPath ) == false )
                return;
        }

        const string srcPath = rawSplashPath.empty() ? "Resource/editor/textures_raw/splash.jpg" : rawSplashPath;

        // 1. Decode raw image via ImageUtil
        RawImageData rawImage;
        SW_ASSERT_TRUE( ImageUtil::loadImage( srcPath, rawImage ) );
        SW_EXPECT_TRUE( rawImage.isValid() );
        SW_EXPECT_EQUAL( 1376, rawImage._width );
        SW_EXPECT_EQUAL( 768, rawImage._height );
        SW_EXPECT_EQUAL( 4, rawImage._channels );

        // 2. Bake to DDS via TextureBaker
        TextureImportRule rule;
        rule._name          = "Test_Splash";
        rule._format        = "B8G8R8A8_UNORM";
        rule._swizzle       = TextureSwizzle::BGRA;
        rule._bGenerateMips = SW_FALSE;
        rule._bSrgb         = SW_TRUE;

        const string      tempOutDds = FileUtil::joinPath( FileUtil::getTempDirectory(), "test_output_splash.dds" );
        TextureBakeResult bakeResult;
        SW_ASSERT_TRUE( TextureBaker::bakeTexture( srcPath, tempOutDds, rule, &bakeResult ) );
        SW_EXPECT_TRUE( bakeResult._bSuccess );
        SW_EXPECT_EQUAL( 1376u, bakeResult._width );
        SW_EXPECT_EQUAL( 768u, bakeResult._height );
        SW_EXPECT_EQUAL( 1u, bakeResult._mipCount );
        SW_EXPECT_TRUE( FileUtil::fileExists( tempOutDds ) );

        // 3. Verify generated DDS with Engine DdsLoader
        DdsImageData ddsData;
        SW_ASSERT_TRUE( DdsLoader::loadFromFile( tempOutDds, ddsData ) );
        SW_EXPECT_TRUE( ddsData.isValid() );
        SW_EXPECT_EQUAL( 1376u, ddsData._width );
        SW_EXPECT_EQUAL( 768u, ddsData._height );
        SW_EXPECT_EQUAL( 91u, ddsData._dxgiFormat ); // B8G8R8A8_UNORM_SRGB
        SW_EXPECT_TRUE( ddsData._bIsBgra );
        SW_EXPECT_EQUAL( static_cast<size_t>( 1376 * 768 * 4 ), ddsData._bytes.size() );

        // 4. Cleanup
        FileUtil::removeFile( tempOutDds );
    }
} // namespace sw::editor
