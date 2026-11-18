// resolveRHIDiagnosticSwitch — EngineConfig `_rhi` 의 진단 칸(`_validationLayer` · `_dred`)이 기본값이고 시작 인자(`-rhi-validation` · `-rhi-dred`, `=0` 은 끔)가 이긴다. 값이 없으면 짐작하지 않고 오류다.
#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/RHIDiagnosticSwitch.h"

using namespace sw;

namespace
{
    /** @brief @p pArgument 하나(없으면 nullptr)를 받은 명령줄 표를 @p outCommandLine 에 채웁니다. */
    void parseDiagnosticCommandLine( CommandLineManager& outCommandLine, const utf8* pArgument )
    {
        const optional<AbsolutePath> baseDirectory = AbsolutePath::tryMake( test::makeScratchPath( "base" ) );
        SW_ASSERT( baseDirectory.has_value() );
        outCommandLine.initialize();
        utf8* argv[] = { const_cast<utf8*>( "TestApp.exe" ), const_cast<utf8*>( pArgument ) };
        outCommandLine.parse( pArgument != nullptr ? 2 : 1, argv, *baseDirectory );
        SW_EXPECT_TRUE( outCommandLine.getParseErrors().empty() );
    }

    /** @brief 검증 층 칸을 @p configValue · @p build 로 풉니다(값이 있다고 단언한다). */
    bool resolveValidation( RHIDiagnosticSwitch configValue, const CommandLineManager* pCommandLine, RHIDiagnosticBuild build )
    {
        const expected<bool> result = resolveRHIDiagnosticSwitch( configValue, CommandLineArgument::RHI_VALIDATION, pCommandLine, build );
        SW_EXPECT_TRUE( result.has_value() );
        return result.has_value() && *result;
    }
} // namespace

/** @brief [RHIDiagnosticSwitchTest] 인자가 없으면 설정값이다 — DebugBuildOnly 는 Debug 만, NonShippingOnly 는 Debug · Release 만, On · Off 는 구성과 상관없다. */
SW_TEST_CASE( RHIDiagnosticSwitchTest, ConfigValueDecidesWithoutArgument )
{
    CommandLineManager commandLine;
    parseDiagnosticCommandLine( commandLine, nullptr );

    SW_EXPECT_TRUE( resolveValidation( RHIDiagnosticSwitch::DebugBuildOnly, &commandLine, RHIDiagnosticBuild::Debug ) );
    SW_EXPECT_FALSE( resolveValidation( RHIDiagnosticSwitch::DebugBuildOnly, &commandLine, RHIDiagnosticBuild::Release ) );
    SW_EXPECT_TRUE( resolveValidation( RHIDiagnosticSwitch::NonShippingOnly, &commandLine, RHIDiagnosticBuild::Release ) );
    SW_EXPECT_FALSE( resolveValidation( RHIDiagnosticSwitch::NonShippingOnly, &commandLine, RHIDiagnosticBuild::Shipping ) );
    SW_EXPECT_TRUE( resolveValidation( RHIDiagnosticSwitch::On, &commandLine, RHIDiagnosticBuild::Shipping ) );
    SW_EXPECT_FALSE( resolveValidation( RHIDiagnosticSwitch::Off, &commandLine, RHIDiagnosticBuild::Debug ) );
    // 엔진 서비스가 없는 실행(명령줄 표 없음)도 설정값을 따른다.
    SW_EXPECT_FALSE( resolveValidation( RHIDiagnosticSwitch::DebugBuildOnly, nullptr, RHIDiagnosticBuild::Release ) );
}

/** @brief [RHIDiagnosticSwitchTest] `-rhi-validation` 은 Release 기본(끔)을 켜고, `-rhi-validation=0` 은 Debug 기본(켬)을 끈다. */
SW_TEST_CASE( RHIDiagnosticSwitchTest, ArgumentOverridesConfig )
{
    CommandLineManager commandLineOn;
    parseDiagnosticCommandLine( commandLineOn, "-rhi-validation" );
    SW_EXPECT_TRUE( resolveValidation( RHIDiagnosticSwitch::DebugBuildOnly, &commandLineOn, RHIDiagnosticBuild::Release ) );
    SW_EXPECT_TRUE( resolveValidation( RHIDiagnosticSwitch::Off, &commandLineOn, RHIDiagnosticBuild::Release ) );

    CommandLineManager commandLineOff;
    parseDiagnosticCommandLine( commandLineOff, "-rhi-validation=0" );
    SW_EXPECT_FALSE( resolveValidation( RHIDiagnosticSwitch::DebugBuildOnly, &commandLineOff, RHIDiagnosticBuild::Debug ) );
    SW_EXPECT_FALSE( resolveValidation( RHIDiagnosticSwitch::On, &commandLineOff, RHIDiagnosticBuild::Debug ) );
}

/** @brief [RHIDiagnosticSwitchTest] 진단마다 제 인자만 본다 — `-rhi-dred` 는 배포 구성의 DRED 를 켜고 검증 층은 건드리지 않는다. */
SW_TEST_CASE( RHIDiagnosticSwitchTest, EachSwitchReadsItsOwnArgument )
{
    CommandLineManager commandLine;
    parseDiagnosticCommandLine( commandLine, "-rhi-dred" );
    const expected<bool> dred = resolveRHIDiagnosticSwitch( RHIDiagnosticSwitch::NonShippingOnly, CommandLineArgument::RHI_DRED, &commandLine, RHIDiagnosticBuild::Shipping );
    SW_EXPECT_TRUE( dred.has_value() && *dred );
    SW_EXPECT_FALSE( resolveValidation( RHIDiagnosticSwitch::DebugBuildOnly, &commandLine, RHIDiagnosticBuild::Release ) );
}

/** @brief [RHIDiagnosticSwitchTest] `-rhi-breadcrumbs=0` 은 개발 구성 기본(켬)의 GPU 브레드크럼을 끄고, `-rhi-breadcrumbs` 는 배포 구성에서 켠다 — 검증 층은 건드리지 않는다. */
SW_TEST_CASE( RHIDiagnosticSwitchTest, BreadcrumbsArgumentDecidesAtStartup )
{
    CommandLineManager commandLineOff;
    parseDiagnosticCommandLine( commandLineOff, "-rhi-breadcrumbs=0" );
    const expected<bool> off = resolveRHIDiagnosticSwitch( RHIDiagnosticSwitch::NonShippingOnly, CommandLineArgument::RHI_BREADCRUMBS, &commandLineOff, RHIDiagnosticBuild::Release );
    SW_EXPECT_TRUE( off.has_value() && *off == false );

    CommandLineManager commandLineOn;
    parseDiagnosticCommandLine( commandLineOn, "-rhi-breadcrumbs" );
    const expected<bool> on = resolveRHIDiagnosticSwitch( RHIDiagnosticSwitch::NonShippingOnly, CommandLineArgument::RHI_BREADCRUMBS, &commandLineOn, RHIDiagnosticBuild::Shipping );
    SW_EXPECT_TRUE( on.has_value() && *on );
    SW_EXPECT_TRUE( resolveValidation( RHIDiagnosticSwitch::DebugBuildOnly, &commandLineOn, RHIDiagnosticBuild::Debug ) );
}

/** @brief [RHIDiagnosticSwitchTest] 파일에 값이 없으면(None) 인자가 있어도 오류다 — 기본값을 짐작하지 않고, 오류 대상은 인자 철자다. */
SW_TEST_CASE( RHIDiagnosticSwitchTest, MissingConfigValueIsAnError )
{
    CommandLineManager commandLine;
    parseDiagnosticCommandLine( commandLine, "-rhi-dred" );
    const expected<bool> result = resolveRHIDiagnosticSwitch( RHIDiagnosticSwitch::None, CommandLineArgument::RHI_DRED, &commandLine, RHIDiagnosticBuild::Debug );
    SW_EXPECT_FALSE( result.has_value() );
    SW_EXPECT_TRUE( result.error().kind() == ErrorKind::InvalidArgument );
    SW_EXPECT_TRUE( result.error().detail() == "rhi-dred" );
}
