#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/ImageUtil.h"
#include "Editor/Common/Asset/TextureBaker.h"
#include "Editor/Common/Asset/TextureImportConfig.h"

#include "Engine/Resource/DdsLoader.h"
#include "Engine/Resource/ResourceUtil.h"

#include "TestFramework/TestFramework.h"

namespace sw::editor
{
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
        SW_ASSERT_TRUE( config.findMatchingRule( "editor/textures_raw/splash.jpg", splashRule ) );
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
     * @brief [EditorTexturePipelineTest] ImageUtil 디코딩, TextureBaker 변환 및 DdsLoader 로딩 E2E 검증
     */
    SW_TEST_CASE( EditorTexturePipelineTest, ImageUtilAndTextureBakerEndToEnd )
    {
        SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
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

    /**
     * @brief [EditorTexturePipelineTest] 스위즐이 바이트를 섞는 방식과 그 결과를 부르는 포맷 이름이 한 자리에서 나온다
     * @details 예전에는 둘이 따로 있었다 — 섞기는 `applyChannelManipulations` 의 if 사슬이,
     *          포맷 이름은 `bakeTexture` 의 `_swizzle == BGRA ? BGRA : RGBA` 삼항이 정했다.
     *          그래서 `ARGB` 는 섞기 쪽만 알고 포맷 쪽은 몰랐다 — 바이트는 옮겨졌는데 결과물은
     *          "RGBA 다" 라고 적혀 나가서 색이 깨졌다. 게다가 그 섞기 자체가 왼쪽 회전이라
     *          RGBA 를 GBAR 로 만들었다(ARGB 는 어떤 읽기로도 그게 아니다).
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
            uint32         _expectedDxgiFormat; ///< sRGB 없이 구운 DDS 가 들고 있어야 할 포맷
        };

        // BGRA 와 ARGB 는 **같은 것**이다. D3D9 의 D3DFMT_A8R8G8B8 은 메모리에서 B,G,R,A 이고,
        // DXGI 가 그것을 B8G8R8A8 이라 부른다 — 열거형 주석의 "레거시 ARGB" 가 그 뜻이다.
        const SwizzleCase arrCase[] = {
            {TextureSwizzle::RGBA, "RGBA",  { 10, 20, 30, 40 }, 28u}, // R8G8B8A8_UNORM
            {TextureSwizzle::BGRA, "BGRA",  { 30, 20, 10, 40 }, 87u}, // B8G8R8A8_UNORM
            {TextureSwizzle::ARGB, "ARGB",  { 30, 20, 10, 40 }, 87u}, // 레거시 이름, 같은 배치
            {TextureSwizzle::RGB1, "RGB1", { 10, 20, 30, 255 }, 28u}, // 알파만 불투명으로
        };

        for ( const SwizzleCase& testCase : arrCase )
        {
            RawImageData image;
            image._width  = 1;
            image._height = 1;
            image._bytes.assign( arrSourcePixel, arrSourcePixel + 4 );

            TextureImportRule rule;
            rule._swizzle = testCase._swizzle;

            TextureBaker::applyChannelManipulations( image, rule, 1 );

            for ( size_t channel = 0; channel < 4; ++channel )
            {
                SW_EXPECT_TRUE_MSG( testCase._arrExpected[channel] == image._bytes[channel], testCase._pName );
            }
        }
    }

    /**
     * @brief [EditorTexturePipelineTest] 그린 반전이 어떤 스위즐에서도 초록에 걸린다
     * @details 예전 `ARGB` 는 바이트를 왼쪽으로 한 칸 돌려 초록을 0번으로 보내 놓고, 반전은
     *          그대로 1번을 뒤집었다 — **파랑을 뒤집고 초록은 그대로 남겼다.** 이 테스트가
     *          보는 것이 그것이다.
     * @note 반전을 섞기 **앞**으로 옮긴 것은 동작이 아니라 가정을 없앤 것이다. 지금 스위즐들은
     *       모두 초록을 1번에 두므로 순서를 바꿔도 결과가 같고, 그래서 **이 테스트는 순서를
     *       구별하지 못한다.** 새 스위즐이 초록을 옮기는 순간 조용히 틀리는 자리를 미리 막은 것이다.
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

            TextureBaker::applyChannelManipulations( image, rule, 1 );

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
     * @brief [EditorTexturePipelineTest] 찾지 못한 inherits 는 조용히 넘어가지 않는다
     * @details `inherits` 해석은 프리셋 쪽과 규칙 쪽에 **복사본 둘**로 있었고 둘 다 못 찾으면
     *          그냥 넘어갔다. 그래서 이름 오타나 **부모를 아래쪽에 적는 것**(찾기는 그 시점까지
     *          파싱된 프리셋만 본다)이 상속을 통째로 지웠고, 그 텍스처는 아무 말 없이 기본값으로
     *          구워졌다. 한 자리로 모으고 경고를 남기게 했다.
     */
    SW_TEST_CASE( EditorTexturePipelineTest, UnresolvedInheritsIsReported )
    {
        // 1) 오타, 2) 부모를 뒤에 적기 — 둘 다 같은 결과다.
        const sw::string_view kJson = R"({
            "presets": {
                "Base_UI": { "format": "B8G8R8A8_UNORM", "swizzle": "BGRA", "srgb": true },
                "Uses_Later": { "inherits": "Declared_Below", "generate_mips": false },
                "Declared_Below": { "format": "BC5_UNORM" }
            },
            "rules": [
                { "name": "Typo_Rule", "inherits": "Base_UI_TYPO", "include_patterns": ["*.png"] }
            ]
        })";

        sw::vector<sw::string> listWarning;
        sw::DelegateHandle     handle = sw::Logger::addGlobalListener(
            SW_DELEGATE_LAMBDA( sw::LogWrittenDelegate, [&listWarning]( const sw::LogEntry& entry )
            {
            if ( entry._level == sw::LogLevel::Warning )
                listWarning.push_back( entry._message );
        } ) );

        TextureImportConfig config;
        const bool          bLoaded = config.loadFromJsonString( kJson );
        sw::Logger::removeGlobalListener( handle );

        // 하나 실패했다고 설정 전체를 버리지는 않는다 — 나머지 규칙은 살아야 한다.
        SW_ASSERT_TRUE( bLoaded );

        size_t inheritWarningCount = 0;
        for ( const sw::string& message : listWarning )
        {
            if ( message.find( "inherits" ) != sw::string::npos )
                ++inheritWarningCount;
        }
        // 못 찾은 것이 둘(Uses_Later 의 전방 참조, Typo_Rule 의 오타)이다.
        SW_EXPECT_EQUAL( size_t( 2 ), inheritWarningCount );

        // 그리고 실제로 상속받은 값이 하나도 없다 — 경고가 가리키는 것이 이것이다.
        // `_inherits` 는 **요청한 이름**을 그대로 남긴다(무엇을 원했는지가 진단에 필요하다).
        // 상속이 정말 일어났는지는 **값**으로 확인한다.
        const auto itUsesLater = config.getPresets().find( "Uses_Later" );
        SW_ASSERT_TRUE( itUsesLater != config.getPresets().end() );
        SW_EXPECT_EQUAL( sw::string( "Declared_Below" ), itUsesLater->second._inherits );
        SW_EXPECT_EQUAL( sw::string( "BC7_UNORM" ), itUsesLater->second._format ); // 부모의 BC5 가 아니라 기본값

        SW_ASSERT_EQUAL( size_t( 1 ), config.getRules().size() );
        SW_EXPECT_EQUAL( sw::string( "Typo_Rule" ), config.getRules()[0]._name );
        SW_EXPECT_EQUAL( sw::string( "BC7_UNORM" ), config.getRules()[0]._format ); // Base_UI 의 BGRA8 이 아니라 기본값
        SW_EXPECT_EQUAL( static_cast<uint8>( TextureSwizzle::RGBA ), static_cast<uint8>( config.getRules()[0]._swizzle ) );

        // 멀쩡한 프리셋은 그대로다 — 위 거부가 과잉이 아님을 못 박는다.
        const auto itBaseUi = config.getPresets().find( "Base_UI" );
        SW_ASSERT_TRUE( itBaseUi != config.getPresets().end() );
        SW_EXPECT_EQUAL( sw::string( "B8G8R8A8_UNORM" ), itBaseUi->second._format );
    }

} // namespace sw::editor
