#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) Engine_CommandLine — 기본값·동의어·RHI 플래그
// ------------------------------------------------------------------------------
/**
 * @brief [CommandLineTest] Width 기본값 존재
 */

SW_TEST_CASE( CommandLineTest, WindowSizeHasNoCommandLineDefault )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();

    // 창 크기의 기본값은 EngineConfig 가 갖는다. 여기서 기본값을 돌려주면 호출부의
    //   h = config._window._height;  getArgument( HEIGHT, h );
    // 가 설정값을 항상 덮어쓴다 — 실제로 1280×720 설정이 무시되고 1280×1280 으로 떴다.
    int32 width{ -1 };
    SW_EXPECT_FALSE( cmdManager.getArgument( sw::CommandLineArgument::WIDTH, width ) );
    SW_EXPECT_EQUAL( -1, width );

    int32 height{ -1 };
    SW_EXPECT_FALSE( cmdManager.getArgument( sw::CommandLineArgument::HEIGHT, height ) );
    SW_EXPECT_EQUAL( -1, height );
}

/**
 * @brief [CommandLineTest] Width 인자 파싱
 */
SW_TEST_CASE( CommandLineTest, ParseWidthArgument )
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
 * @brief [CommandLineTest] 동의어 인자 파싱
 */
SW_TEST_CASE( CommandLineTest, ParseSynonymArgument )
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
 * @brief [CommandLineTest] 없는 인자는 false
 */
SW_TEST_CASE( CommandLineTest, GetArgumentNotFoundReturnsFalse )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();

    sw::string ip;
    bool       hasIP = cmdManager.getArgument( sw::CommandLineArgument::IP, ip );
    SW_EXPECT_FALSE( hasIP );
}

/**
 * @brief [CommandLineTest] 문자열 키와 UTF-16 파싱
 */
SW_TEST_CASE( CommandLineTest, StringKeyAndUtf16Parse )
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
 * @brief [CommandLineTest] RHI 백엔드 CLI 플래그와 동의어
 */
SW_TEST_CASE( CommandLineTest, RHIBackendCommandLineFlagsAndSynonyms )
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
 * @brief [CommandLineTest] 커스텀 인자 등록 및 복합 파싱 검증
 */
SW_TEST_CASE( CommandLineTest, ComplexPrefixAndCustomArguments )
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

/**
 * @brief [CommandLineTest] 주지 않은 단독 플래그를 "주었다" 로 읽지 않는다
 * @details 백엔드 플래그 넷과 ENABLE_EDITOR 만 `bUseDefaultValue` 가 켜져 있었다. 기본값이 `false`
 *          라 값 자체는 맞았지만 `getArgument` 가 **주지 않은 인자에도 true** 를 돌려줘, 반환값만으로는
 *          어느 백엔드를 고르라는 것인지 알 수 없었다 — `RHI.cpp` 가 `getArgument(...) && bFlag` 로
 *          한 번 더 묻는 것이 그 흔적이다. 같은 파일의 VSYNC 는 처음부터 꺼져 있었다.
 */
SW_TEST_CASE( CommandLineTest, UnprovidedFlagIsNotReadable )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();

    utf8* argv[] = {
        const_cast<utf8*>( "App.exe" ),
        const_cast<utf8*>( "dx12" ),
    };
    cmdManager.parse( 2, argv );

    bool bFlag{ true };
    SW_EXPECT_FALSE( cmdManager.getArgument( sw::CommandLineArgument::DIRECTX_11, bFlag ) );
    SW_EXPECT_FALSE( cmdManager.getArgument( sw::CommandLineArgument::VULKAN, bFlag ) );
    SW_EXPECT_FALSE( cmdManager.getArgument( sw::CommandLineArgument::OPENGL, bFlag ) );
    SW_EXPECT_FALSE( cmdManager.getArgument( sw::CommandLineArgument::ENABLE_EDITOR, bFlag ) );
    SW_EXPECT_FALSE( cmdManager.getArgument( sw::CommandLineArgument::VSYNC, bFlag ) );

    // 준 것만 읽힌다.
    bool bDx12{ false };
    SW_EXPECT_TRUE( cmdManager.getArgument( sw::CommandLineArgument::DIRECTX_12, bDx12 ) );
    SW_EXPECT_TRUE( bDx12 );

    // 단독 플래그에 한해 두 질문의 답이 같아졌다.
    SW_EXPECT_TRUE( cmdManager.isArgumentProvided( sw::CommandLineArgument::DIRECTX_12 ) );
    SW_EXPECT_FALSE( cmdManager.isArgumentProvided( sw::CommandLineArgument::DIRECTX_11 ) );
}

/**
 * @brief [CommandLineTest] "실제로 적혔는가" 와 "값을 읽을 수 있는가" 는 다르다
 * @details `getArgument` 는 기본값을 가진 인자라면 **안 적어도 true** 를 돌려준다. 그래서 그것만으로는
 *          "설정 파일 기본값보다 커맨드라인이 우선" 을 판단할 수 없다. 실제로 `EngineConfig` 의
 *          `_defaultRHI` 가 `-gv_rhiBackend` 를 조용히 덮고 있었다 — 커맨드라인이 아무 일도 안 하는
 *          것처럼 보였고 로그도 남지 않아, **네 백엔드를 검증했다고 믿은 것이 전부 한 백엔드**였다.
 */
SW_TEST_CASE( CommandLineTest, ProvidedIsNotTheSameAsReadable )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();
    cmdManager.addArgument<int32>( { "given_value" }, true, 7, true );
    cmdManager.addArgument<int32>( { "omitted_value" }, true, 7, true );

    utf8* argv[] = {
        const_cast<utf8*>( "App.exe" ),
        const_cast<utf8*>( "given_value=3" ),
    };
    cmdManager.parse( 2, argv );

    // 둘 다 읽히지만(기본값이 있으므로), 적힌 것은 하나뿐이다.
    int32 given{ 0 };
    int32 omitted{ 0 };
    SW_EXPECT_TRUE( cmdManager.getArgument( std::string_view( "given_value" ), given ) );
    SW_EXPECT_TRUE( cmdManager.getArgument( std::string_view( "omitted_value" ), omitted ) );
    SW_EXPECT_EQUAL( 3, given );
    SW_EXPECT_EQUAL( 7, omitted );

    SW_EXPECT_TRUE_MSG( cmdManager.isArgumentProvided( "given_value" ), "적은 인자를 안 적었다고 한다" );
    SW_EXPECT_TRUE_MSG( cmdManager.isArgumentProvided( "omitted_value" ) == false,
                        "안 적은 인자를 적었다고 한다 — 설정 기본값이 커맨드라인을 덮는 판단이 여기서 갈린다" );
    SW_EXPECT_TRUE_MSG( cmdManager.isArgumentProvided( "no_such_argument" ) == false, "없는 인자를 적었다고 한다" );
}

/**
 * @brief [CommandLineTest] 열거형 조회와 문자열 조회가 같은 인자를 가리킨다
 * @details 열거형 조회는 이름을 만들지 않고 `_listArgument` 를 열거값으로 바로 인덱싱한다.
 *          그 근거는 ArgumentList.xxx 한 줄이 열거 멤버와 원소를 같은 순서로 만든다는 것뿐이므로,
 *          두 경로가 같은 답을 내는지 여기서 못박는다. `Count` 는 어느 인자도 아니다.
 */
SW_TEST_CASE( CommandLineTest, EnumLookupMatchesStringLookup )
{
    sw::CommandLineManager cmdManager;
    cmdManager.initialize();

    utf8* argv[] = {
        const_cast<utf8*>( "App.exe" ),
        const_cast<utf8*>( "-HEIGHT=1440" ),
        const_cast<utf8*>( "-lang=ko" ),
    };
    cmdManager.parse( 3, argv );

    int32 heightByEnum{ 0 };
    int32 heightByName{ 0 };
    SW_EXPECT_TRUE( cmdManager.getArgument( sw::CommandLineArgument::HEIGHT, heightByEnum ) );
    SW_EXPECT_TRUE( cmdManager.getArgument( std::string_view( "HEIGHT" ), heightByName ) );
    SW_EXPECT_EQUAL( 1440, heightByEnum );
    SW_EXPECT_EQUAL( heightByName, heightByEnum );

    sw::string langByEnum;
    sw::string langByName;
    SW_EXPECT_TRUE( cmdManager.getArgument( sw::CommandLineArgument::LANGUAGE, langByEnum ) );
    SW_EXPECT_TRUE( cmdManager.getArgument( std::string_view( "lang" ), langByName ) );
    SW_EXPECT_STREQ( "ko", langByEnum.c_str() );
    SW_EXPECT_STREQ( langByName.c_str(), langByEnum.c_str() );

    SW_EXPECT_TRUE( cmdManager.isArgumentProvided( sw::CommandLineArgument::HEIGHT ) );
    SW_EXPECT_TRUE( cmdManager.isArgumentProvided( sw::CommandLineArgument::WIDTH ) == false );

    // Count 는 인자가 아니다 — 인덱싱이 범위를 넘지 않고 false 로 돌아와야 한다.
    int32 none{ -1 };
    SW_EXPECT_FALSE( cmdManager.getArgument( sw::CommandLineArgument::Count, none ) );
    SW_EXPECT_TRUE( cmdManager.isArgumentProvided( sw::CommandLineArgument::Count ) == false );
}
