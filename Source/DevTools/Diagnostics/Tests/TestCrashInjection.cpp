// 크래시 보고를 끝까지 태운다 — 이 실행 파일을 자식 역할로 띄워 `CrashInjector` 로 죽게 하고 보고가 남았는지 본다(보고 조각은 Core 의 `TestCrashReport.cpp`).
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "DevTools/Diagnostics/CrashInjection.h"

using namespace sw;

// 진짜 크래시 — 이 실행 파일을 자식 역할로 띄워 죽게 하고 보고가 남았는지 본다. 위 케이스처럼 조각을 직접 부르면 "그 방식으로 죽으면 핸들러에 들어오기는
// 하는가 · 들어와서 쓸 스택이 남아 있는가" 를 볼 수 없다(스택 오버플로의 0 바이트 덤프, abort · 순수 가상 호출의 필터 미진입이 그렇게 샌다).
// 보고는 부모가 준 케이스 쓰기 폴더에만 쓴다 — 자식은 그 폴더를 쓰는 제 처리기를 앱의 처리기 위에 설치하고 죽는다(나중에 설치한 처리기가 보고한다).
// 새니타이저는 치명 시그널을 제가 쥐고 보고한다 — 그 구성에서는 짓지 않는다.
#if !defined( SW_SANITIZER_ADDRESS ) && !defined( SW_SANITIZER_THREAD )
namespace
{
    /** @brief 자식 시한(초) — Debug 의 자식 App 기동(몇 초)에 크래시 보고 몫을 더한다. 케이스 시한은 이보다 길게 둔다. */
    constexpr float32 kCrashChildTimeoutSeconds = 120.0f;
    /**
     * @brief 막힌 보고(`StderrHeld`)에 주는 시한(초) — 진척 없이 버틸 시간입니다. stderr 는 영영 막혀 있으니 시한은 늘 닿는다.
     * @details 짧아도 부하 아래에서 덤프 · 스택 파일이 남아야 한다 — 시한은 전체 길이가 아니라 진척이 멈춘 시간이다(`CrashHandler::setReportDeadline`).
     *          예전에는 전체 길이라 테스트 여섯이 함께 돌 때 2 초가 덤프보다 먼저 닿아 파일이 없었다.
     */
    constexpr uint32 kStuckReportDeadlineSeconds = 2;

    /** @brief @p folder 에서 이름이 @p suffix 로 끝나는 첫 파일입니다. 없으면 빈 글입니다. */
    string findReportFile( const string& folder, string_view suffix )
    {
        vector<string> listFile;
        if ( FileUtil::collectFiles( folder, "", listFile, false ) == false )
            return {};
        for ( const string& filePath : listFile )
        {
            if ( StringUtil::endsWith( filePath, suffix ) )
                return filePath;
        }
        return {};
    }

    /** @brief 자식 인자 `<종류 번호>|<보고 폴더>[|<보고 시한 초>]` 를 만듭니다(`|` 는 Windows 경로에 들 수 없다). */
    string makeCrashChildArgument( CrashInjectionKind kind, const string& folder, uint32 deadlineSeconds = 0 )
    {
        StringBuilder<constant::buffer::kMax256> argumentBuilder;
        argumentBuilder.appendFormat( "%#|%#", static_cast<int32>( kind ), folder );
        string argument{ argumentBuilder.view() };
        if ( deadlineSeconds > 0 )
        {
            StringBuilder<constant::buffer::kMax256> argumentPart;
            argumentPart.appendFormat( "|%#", deadlineSeconds );
            argument += argumentPart.c_str();
        }
        return argument;
    }

    /** @brief 자식이 죽은 뒤 보고 폴더에 스택 파일(과 Windows 는 비지 않은 미니덤프)이 있어야 합니다. */
    void expectCrashFiles( const string& folder, const utf8* pWhat )
    {
        const string stackPath = findReportFile( folder, "stack.txt" );
        string       stackText;
        SW_EXPECT_TRUE_MSG( stackPath.empty() == false && FileUtil::readTextFile( stackPath, stackText ) && stackText.find( "CRASH" ) != string::npos, pWhat );
    #if defined( SW_PLATFORM_WINDOWS )
        const string dumpPath = findReportFile( folder, ".dmp" );
        uint64       dumpSize = 0;
        SW_EXPECT_TRUE_MSG( dumpPath.empty() == false && FileUtil::tryGetFileSize( dumpPath, dumpSize ) && dumpSize > 0, pWhat );
    #endif
    }

    /** @brief 자식이 죽을 방식 하나 — `EveryCrashKindLeavesAReport/<번호>` 의 매개변수다. */
    struct CrashCase
    {
        CrashInjectionKind _kind;
        const utf8*        _pName;
    };
    constexpr CrashCase kArrCrashCase[] = {
        {    CrashInjectionKind::AccessViolation,      "access-violation"},
        {      CrashInjectionKind::StackOverflow,        "stack-overflow"},
        {CrashInjectionKind::WorkerStackOverflow, "worker-stack-overflow"},
        {              CrashInjectionKind::Abort,                 "abort"},
        {    CrashInjectionKind::PureVirtualCall,     "pure-virtual-call"},
    };
} // namespace

/**
 * @brief 자식 역할: 인자(`<종류>|<보고 폴더>[|<보고 시한>]`)가 시키는 방식으로 죽는다. 돌아오면 실패다.
 */
SW_TEST_CHILD_ROLE( CrashInjectionTest, ChildCrashesAsRequested )
{
    const string              argument = test::getChildRoleArgument();
    const vector<string_view> listPart = StringUtil::split( argument, "|" );
    int32                     kindCode = 0;
    SW_REQUIRE_TRUE_MSG( listPart.size() >= 2 && StringUtil::parseInt( listPart[0], kindCode ), argument );
    CrashHandler handler;
    handler.setReportFolder( listPart[1] );
    if ( listPart.size() >= 3 )
    {
        uint64 deadlineSeconds = 0;
        SW_REQUIRE_TRUE_MSG( StringUtil::parseUint64( listPart[2], deadlineSeconds ) && deadlineSeconds <= 600, argument );
        handler.setReportDeadline( static_cast<uint32>( deadlineSeconds ) );
    }
    handler.install();
    #if defined( SW_PLATFORM_WINDOWS )
    // 오류 보고 창(WER)이 떠서 부모가 시한까지 기다리지 않게 한다 — 핸들러가 처리하면 원래 뜨지 않는다(처리하지 못했을 때를 위한 것).
    ::SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );
    #endif
    CrashInjector::crash( static_cast<CrashInjectionKind>( kindCode ) );
    SW_EXPECT_TRUE_MSG( false, "CrashInjector::crash returned instead of crashing" );
}

/**
 * @brief 죽는 방식마다(접근 위반 · 스택 오버플로 · 작업 스레드 스택 오버플로 · abort · 순수 가상 호출) 보고가 남는다 — 방식마다 자식 하나.
 * @details 스택 오버플로: 넘친 스택에서 덤프를 쓰면 다시 넘쳐 덤프가 0 바이트다(Windows), 작업 스레드에도 대체 스택이 있어야 한다(POSIX).
 *          abort · 순수 가상 호출: CRT 가 `__fastfail` 로 끝내면 필터에 아예 들어오지 않는다(Windows).
 */
SW_TEST_CASE_EACH( CrashInjectionTest, EveryCrashKindLeavesAReport, kArrCrashCase, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const string folder = test::makeScratchPath( param._pName );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );

    const test::ChildRunResult child =
        test::runChildRole( "CrashInjectionTest.ChildCrashesAsRequested", makeCrashChildArgument( param._kind, folder ), kCrashChildTimeoutSeconds );
    SW_REQUIRE_TRUE_MSG( child._bLaunched && child._bTimedOut == false, param._pName );
    SW_EXPECT_TRUE_MSG( ( child._exitCode.has_value() && *child._exitCode != 0 ), param._pName + string( " - last output:" ) + child.getOutputTail() );
    expectCrashFiles( folder, param._pName );
}

/**
 * @brief 보고가 끝날 수 없어도(다른 스레드가 stderr 를 쥔 채 놓지 않는다) 프로세스는 보고 시한에 끝나고 스택 파일은 남는다.
 * @details 보고는 죽어 가는 프로세스 안에서 돈다 — 크래시가 남긴 락을 기다리면 영영 끝나지 않는다. 스택 파일을 stderr 보다 먼저 쓰고, 시한이 지났다는 줄은
 *          stderr(`fputs`)가 아닌 길로 쓴다. 보고 시한 2 초는 자식 시한보다 한참 짧다 — 자식 시한에 죽으면 보고 시한이 듣지 않은 것이다.
 */
SW_TEST_CASE( CrashInjectionTest, StuckReportStillEndsAndKeepsTheStackFile, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const string folder = test::makeScratchPath( "stuck" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );

    const test::ChildRunResult child =
        test::runChildRole( "CrashInjectionTest.ChildCrashesAsRequested", makeCrashChildArgument( CrashInjectionKind::StderrHeld, folder, kStuckReportDeadlineSeconds ), kCrashChildTimeoutSeconds );
    SW_REQUIRE_TRUE( child._bLaunched && child._bTimedOut == false );
    SW_EXPECT_TRUE( ( child._exitCode.has_value() && *child._exitCode != 0 ) );
    SW_EXPECT_TRUE_MSG( child._output.find( "made no progress before its deadline" ) != string::npos, "no line saying the report hit its deadline - last output:" + child.getOutputTail() );
    expectCrashFiles( folder, "stuck" );
}

    #if defined( SW_DO_CHECKS )
/**
 * @brief 실패한 `SW_ASSERT` 는 멈추기 전에 식 · 자리를 남기고 크래시 보고도 남는다(단언은 검사 구성(배포가 아닌 구성)에만 있다).
 * @details 아무것도 찍지 않고 멈추면 디버거 없이 돌 때 "EXCEPTION_BREAKPOINT"(리눅스 SIGILL)와 스택만 남아 어느 식이 어긋났는지 모른다.
 */
SW_TEST_CASE( CrashInjectionTest, FailedAssertNamesItsExpressionBeforeStopping, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const string folder = test::makeScratchPath( "assert" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );

    const test::ChildRunResult child =
        test::runChildRole( "CrashInjectionTest.ChildCrashesAsRequested", makeCrashChildArgument( CrashInjectionKind::AssertFailure, folder ), kCrashChildTimeoutSeconds );
    SW_REQUIRE_TRUE( child._bLaunched && child._bTimedOut == false );
    SW_EXPECT_TRUE( ( child._exitCode.has_value() && *child._exitCode != 0 ) );
    const bool bNamesExpression = child._output.find( "[SW_ASSERT] bProbeHolds" ) != string::npos;
    const bool bNamesPlace      = child._output.find( "CrashInjection.cpp:" ) != string::npos;
    SW_EXPECT_TRUE_MSG( bNamesExpression && bNamesPlace, "the assert did not name its expression and place - last output:" + child.getOutputTail() );
    SW_EXPECT_FALSE( findReportFile( folder, "stack.txt" ).empty() );
}
    #endif
#endif
