// resolveRHIValidationLayer — EngineConfig `_rhi._validationLayer` 가 기본값이고 시작 인자 `-rhi-validation`(`=0`)이 이긴다. 값이 없으면 짐작하지 않고 오류다.
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIValidationLayer.h"

using namespace sw;

namespace
{
    /** @brief @p pArgument 하나(없으면 nullptr)를 받은 명령줄 표를 @p outCommandLine 에 채웁니다. */
    void parseValidationCommandLine( CommandLineManager& outCommandLine, const utf8* pArgument )
    {
        const optional<AbsolutePath> baseDirectory = AbsolutePath::tryMake( test::makeScratchPath( "base" ) );
        SW_ASSERT( baseDirectory.has_value() );
        outCommandLine.initialize();
        utf8* argv[] = { const_cast<utf8*>( "TestApp.exe" ), const_cast<utf8*>( pArgument ) };
        outCommandLine.parse( pArgument != nullptr ? 2 : 1, argv, *baseDirectory );
        SW_EXPECT_TRUE( outCommandLine.getParseErrors().empty() );
    }
} // namespace

/** @brief [RHIValidationLayerTest] 인자가 없으면 설정값이다 — DebugBuildOnly 는 Debug 만 켜고, On · Off 는 구성과 상관없다. */
SW_TEST_CASE( RHIValidationLayerTest, ConfigModeDecidesWithoutArgument )
{
    CommandLineManager commandLine;
    parseValidationCommandLine( commandLine, nullptr );

    SW_EXPECT_TRUE( *resolveRHIValidationLayer( RHIValidationLayerMode::DebugBuildOnly, &commandLine, true ) );
    SW_EXPECT_FALSE( *resolveRHIValidationLayer( RHIValidationLayerMode::DebugBuildOnly, &commandLine, false ) );
    SW_EXPECT_TRUE( *resolveRHIValidationLayer( RHIValidationLayerMode::On, &commandLine, false ) );
    SW_EXPECT_FALSE( *resolveRHIValidationLayer( RHIValidationLayerMode::Off, &commandLine, true ) );
    // 엔진 서비스가 없는 실행(명령줄 표 없음)도 설정값을 따른다.
    SW_EXPECT_FALSE( *resolveRHIValidationLayer( RHIValidationLayerMode::DebugBuildOnly, nullptr, false ) );
}

/** @brief [RHIValidationLayerTest] `-rhi-validation` 은 Release 기본(끔)을 켜고, `-rhi-validation=0` 은 Debug 기본(켬)을 끈다. */
SW_TEST_CASE( RHIValidationLayerTest, ArgumentOverridesConfig )
{
    CommandLineManager commandLineOn;
    parseValidationCommandLine( commandLineOn, "-rhi-validation" );
    SW_EXPECT_TRUE( *resolveRHIValidationLayer( RHIValidationLayerMode::DebugBuildOnly, &commandLineOn, false ) );
    SW_EXPECT_TRUE( *resolveRHIValidationLayer( RHIValidationLayerMode::Off, &commandLineOn, false ) );

    CommandLineManager commandLineOff;
    parseValidationCommandLine( commandLineOff, "-rhi-validation=0" );
    SW_EXPECT_FALSE( *resolveRHIValidationLayer( RHIValidationLayerMode::DebugBuildOnly, &commandLineOff, true ) );
    SW_EXPECT_FALSE( *resolveRHIValidationLayer( RHIValidationLayerMode::On, &commandLineOff, true ) );
}

/** @brief [RHIValidationLayerTest] 파일에 값이 없으면(None) 인자가 있어도 오류다 — 기본값을 짐작하지 않는다. */
SW_TEST_CASE( RHIValidationLayerTest, MissingConfigValueIsAnError )
{
    CommandLineManager commandLine;
    parseValidationCommandLine( commandLine, "-rhi-validation" );
    const expected<bool> result = resolveRHIValidationLayer( RHIValidationLayerMode::None, &commandLine, true );
    SW_EXPECT_FALSE( result.has_value() );
    SW_EXPECT_TRUE( result.error().kind() == ErrorKind::InvalidArgument );
}
