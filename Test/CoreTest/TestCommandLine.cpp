#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Engine_CommandLine — 기본값·동의어·RHI 플래그
// ------------------------------------------------------------------------------
/**
 * @brief [Engine_CommandLine] Width 기본값 존재
 */

SW_TEST_CASE( Engine_CommandLine, DefaultValueForWidthExists )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();

    int32 width{ 0 };
    bool  hasWidth = cmdManager.getArgument( sw::CommandLineArgument::WIDTH, width );
    SW_EXPECT_TRUE( hasWidth );
    SW_EXPECT_EQUAL( 1280, width );
}

/**
 * @brief [Engine_CommandLine] Width 인자 파싱
 */
SW_TEST_CASE( Engine_CommandLine, ParseWidthArgument )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();

    utf8* argv[] = {
        const_cast<utf8*>( "TestApp.exe" ),
        const_cast<utf8*>( "WIDTH=1920" ),
    };
    cmdManager.parse( 2, argv );

    int32 width{ 0 };
    bool  hasWidth = cmdManager.getArgument( sw::CommandLineArgument::WIDTH, width );
    SW_EXPECT_TRUE( hasWidth );
    SW_EXPECT_EQUAL( 1920, width );
}

/**
 * @brief [Engine_CommandLine] 동의어 인자 파싱
 */
SW_TEST_CASE( Engine_CommandLine, ParseSynonymArgument )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();

    utf8* argv[] = {
        const_cast<utf8*>( "TestApp.exe" ),
        const_cast<utf8*>( "W=800" ),
    };
    cmdManager.parse( 2, argv );

    int32 width{ 0 };
    bool  hasWidth = cmdManager.getArgument( sw::CommandLineArgument::WIDTH, width );
    SW_EXPECT_TRUE( hasWidth );
    SW_EXPECT_EQUAL( 800, width );
}

/**
 * @brief [Engine_CommandLine] 없는 인자는 false
 */
SW_TEST_CASE( Engine_CommandLine, GetArgumentNotFoundReturnsFalse )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();

    sw::string ip;
    bool       hasIP = cmdManager.getArgument( sw::CommandLineArgument::IP, ip );
    SW_EXPECT_FALSE( hasIP );
}

/**
 * @brief [Engine_CommandLine] 문자열 키와 UTF-16 파싱
 */
SW_TEST_CASE( Engine_CommandLine, StringKeyAndUtf16Parse )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();

    utf16* wargv[] = {
        const_cast<utf16*>( L"TestApp.exe" ),
        const_cast<utf16*>( L"WIDTH=2560" ),
    };
    cmdManager.parse( 2, wargv );

    int32 width{ 0 };
    bool  hasWidth = cmdManager.getArgument( std::string_view( "WIDTH" ), width );
    SW_EXPECT_TRUE( hasWidth );
    SW_EXPECT_EQUAL( 2560, width );
}

/**
 * @brief [Engine_CommandLine] RHI 백엔드 CLI 플래그와 동의어
 */
SW_TEST_CASE( Engine_CommandLine, RHIBackendCommandLineFlagsAndSynonyms )
{
    {
        sw::CommandLineManager cmdManager;
        cmdManager.initialize();
        utf8* argv[] = {
            const_cast<utf8*>( "TestApp.exe" ),
            const_cast<utf8*>( "VULKAN" ),
        };
        cmdManager.parse( 2, argv );

        bool bVk{ false };
        SW_EXPECT_TRUE( cmdManager.getArgument( sw::CommandLineArgument::VULKAN, bVk ) );
        SW_EXPECT_TRUE( bVk );
    }

    {
        sw::CommandLineManager cmdManager;
        cmdManager.initialize();
        utf8* argv[] = {
            const_cast<utf8*>( "TestApp.exe" ),
            const_cast<utf8*>( "dx11" ),
        };
        cmdManager.parse( 2, argv );

        bool bDx11{ false };
        SW_EXPECT_TRUE( cmdManager.getArgument( sw::CommandLineArgument::DIRECTX_11, bDx11 ) );
        SW_EXPECT_TRUE( bDx11 );
    }

    {
        sw::CommandLineManager cmdManager;
        cmdManager.initialize();
        utf8* argv[] = {
            const_cast<utf8*>( "TestApp.exe" ),
            const_cast<utf8*>( "gl" ),
        };
        cmdManager.parse( 2, argv );

        bool bGl{ false };
        SW_EXPECT_TRUE( cmdManager.getArgument( sw::CommandLineArgument::OPENGL, bGl ) );
        SW_EXPECT_TRUE( bGl );
    }
}

/**
 * @brief [Engine_CommandLine] 커스텀 인자 등록 및 복합 파싱 검증
 */
SW_TEST_CASE( Engine_CommandLine, ComplexPrefixAndCustomArguments )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();

    // 커스텀 인자 등록
    cmdManager.addArgument<sw::string>( { "custom_level", "CL" }, true, sw::string( "DefaultLevel" ), false );
    cmdManager.addArgument<bool>( { "enable_profiler" }, false, false, false );

    utf8* argv[] = {
        const_cast<utf8*>( "App.exe" ),
        const_cast<utf8*>( "HEIGHT=1080" ),
        const_cast<utf8*>( "custom_level=DesertStage" ),
        const_cast<utf8*>( "enable_profiler" ),
    };
    cmdManager.parse( 4, argv );

    int32 height{ 0 };
    SW_EXPECT_TRUE( cmdManager.getArgument( sw::CommandLineArgument::HEIGHT, height ) );
    SW_EXPECT_EQUAL( 1080, height );

    sw::string customLevel;
    SW_EXPECT_TRUE( cmdManager.getArgument( std::string_view( "custom_level" ), customLevel ) );
    SW_EXPECT_STREQ( "DesertStage", customLevel.c_str() );

    bool enableProfiler{ false };
    SW_EXPECT_TRUE( cmdManager.getArgument( std::string_view( "enable_profiler" ), enableProfiler ) );
    SW_EXPECT_TRUE( enableProfiler );
}
