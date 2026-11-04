// 크래시 보고 — 크래시 경로에서 할당 없이 미리 담아 둔 문맥(키-값 · 빵부스러기)과 세 플랫폼이 함께 쓰는 보고 본문. 앞 절은 케이스마다 제 처리기(설치하지 않은
// `CrashHandler`)를 만들어 보고 조각을 직접 부른다 — 앱의 처리기는 건드리지 않는다. 끝 절은 자식 역할(`SW_TEST_CHILD_ROLE`)을 띄워 진짜로 죽인다.
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/vector.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Process/ProcessSession.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 케이스의 처리기(설치하지 않는다)와 그 보고 폴더입니다. */
    struct CrashReportTest
    {
        CrashReportTest()
            : _handler{}
        {
        }
        CrashReportTest( const CrashReportTest& )            = delete;
        CrashReportTest& operator=( const CrashReportTest& ) = delete;

        /** @brief 케이스 폴더 아래 @p name 폴더를 만들어 이 처리기의 보고 폴더로 정하고 그 경로를 돌려줍니다(못 만들면 빈 글). */
        string useReportFolder( string_view name )
        {
            const string folder = test::makeScratchPath( name );
            if ( FileUtil::ensureDirectoryExists( folder ) == false )
                return {};
            _handler.setReportFolder( folder );
            return folder;
        }

        /** @brief 세션 ID 로 만들어지는 보고 파일 하나를 읽습니다. */
        [[nodiscard]] bool readReportFile( const utf8* pExtension, string& outText ) const
        {
            utf8 arrPath[constant::buffer::kMax1024]{};
            _handler.makeReportPath( arrPath, constant::buffer::kMax1024, pExtension );
            return FileUtil::readTextFile( arrPath, outText );
        }

        CrashHandler _handler;
    };
} // namespace

/**
 * @brief 미리 등록해 둔 키-값이 문맥 파일에 그대로 나온다 — 덤프 · 코어만으로는 "어느 백엔드였는가" 를 알 수 없다.
 */
SW_TEST_CASE_F( CrashReportTest, RegisteredContextReachesTheContextFile )
{
    SW_REQUIRE_FALSE( useReportFolder( "reports" ).empty() );

    _handler.setContextValue( "RHI", "DX12" );
    _handler.setContextValue( "Build", "CrashReportTest" );

    _handler.writeContextFile( "unit test reason", reinterpret_cast<const void*>( 0xDEADBEEFull ), 4242, 8484 );

    string text;
    SW_REQUIRE_TRUE( readReportFile( "txt", text ) );

    SW_EXPECT_TRUE( text.find( "unit test reason" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "deadbeef" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "4242" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "8484" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "DX12" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "CrashReportTest" ) != string::npos );
    SW_EXPECT_TRUE( text.find( ProcessSession::getID() ) != string::npos );
}

/**
 * @brief 경로에 한글이 든 보고 폴더에도 파일이 써진다 — 사용자 이름이 한글이면 `Saved/` 경로도 한글이고, ANSI 로 열면 크래시 경로라 아무 말 없이 하나도 남지 않는다.
 */
SW_TEST_CASE_F( CrashReportTest, ReportFolderWithANonAsciiNameReceivesTheFiles )
{
    SW_REQUIRE_FALSE( useReportFolder( "SwCrashReport_한글 폴더" ).empty() );

    SW_EXPECT_LOG( "Crash.Report" ); // 보고 본문은 오류 로그로도 남는다
    _handler.writeContextFile( "non-ascii folder", nullptr, 1, 2 );
    _handler.writeReport( "non-ascii fault", nullptr, nullptr, false );

    string text;
    SW_EXPECT_TRUE( readReportFile( "txt", text ) && text.find( "non-ascii folder" ) != string::npos );
    SW_EXPECT_TRUE( readReportFile( "stack.txt", text ) && text.find( "non-ascii fault" ) != string::npos );
}

/**
 * @brief 같은 키는 덮어쓰고, 자리가 차면 조용히 버리되 이미 든 값은 망가뜨리지 않는다(크래시 진단을 돕자고 넣은 것이 실패를 키우면 안 된다).
 */
SW_TEST_CASE_F( CrashReportTest, ContextStoreOverwritesKeyAndDropsOverflowSafely )
{
    CrashContextStore store;

    store.set( "RHI", "Vulkan" );
    store.set( "RHI", "DX11" );
    SW_EXPECT_EQUAL( 1u, store._entryCount );
    SW_EXPECT_TRUE( store._arrEntry[0]._value.view() == string_view( "DX11" ) );

    // 정원을 넘겨 본다. 개수는 정원에서 멈추고 첫 항목은 그대로여야 한다.
    for ( uint32 index = 0; index < CrashContextStore::kMaxEntry + 8; ++index )
        store.set( formatToString( "Key%#", index ), "value" );
    SW_EXPECT_EQUAL( CrashContextStore::kMaxEntry, store._entryCount );
    SW_EXPECT_TRUE( store._arrEntry[0]._value.view() == string_view( "DX11" ) );

    // 빈 키는 아무 일도 하지 않는다.
    const uint32 previousCount = store._entryCount;
    store.set( "", "ignored" );
    SW_EXPECT_EQUAL( previousCount, store._entryCount );
}

/**
 * @brief 보고가 "무엇을 보내면 되는지" 를 적고, 쓰지 않은 덤프는 적지 않는다 — 본문은 Windows · POSIX 가 한 곳에서 만든다.
 */
SW_TEST_CASE_F( CrashReportTest, ReportListsFilesToSendAndSkipsTheDumpWhenThereIsNone )
{
    SW_EXPECT_LOG( "Crash.Report" ); // 보고 본문은 오류 로그로도 남는다
    SW_REQUIRE_FALSE( useReportFolder( "reports" ).empty() );

    utf8 arrContextPath[constant::buffer::kMax1024]{};
    utf8 arrStackPath[constant::buffer::kMax1024]{};
    utf8 arrDumpPath[constant::buffer::kMax1024]{};
    _handler.makeReportPath( arrContextPath, constant::buffer::kMax1024, "txt" );
    _handler.makeReportPath( arrStackPath, constant::buffer::kMax1024, "stack.txt" );
    _handler.makeReportPath( arrDumpPath, constant::buffer::kMax1024, "dmp" );

    // 1) 덤프가 없을 때 — 문맥과 스택 파일만 적혀야 한다.
    _handler.writeReport( "unit test fault", reinterpret_cast<const void*>( 0x1234ull ), nullptr, false );

    string text;
    SW_REQUIRE_TRUE( readReportFile( "stack.txt", text ) );

    SW_EXPECT_TRUE( text.find( "unit test fault" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "crash report files" ) != string::npos );
    SW_EXPECT_TRUE( text.find( arrContextPath ) != string::npos );
    SW_EXPECT_TRUE_MSG( text.find( arrStackPath ) != string::npos, "the stack file is missing from the list - the file with the stack would not be sent" );
    SW_EXPECT_TRUE_MSG( text.find( arrDumpPath ) == string::npos, "the list names a minidump that was never written" );

    // 2) 덤프를 썼을 때 — 그때만 덤프 경로가 붙는다.
    _handler.writeReport( "unit test fault", reinterpret_cast<const void*>( 0x1234ull ), nullptr, true );

    SW_REQUIRE_TRUE( readReportFile( "stack.txt", text ) );
    SW_EXPECT_TRUE( text.find( arrDumpPath ) != string::npos );
}

/**
 * @brief 세션 ID 는 한 프로세스에서 하나이고 보고 경로에 들어간다 — 고객이 보낸 덤프와 로그가 같은 실행의 것인지 확인하는 유일한 길이다.
 */
SW_TEST_CASE_F( CrashReportTest, SessionIDIsStableAndAppearsInReportPaths )
{
    const utf8* pFirst  = ProcessSession::getID();
    const utf8* pSecond = ProcessSession::getID();

    SW_REQUIRE_NOT_NULL( pFirst );
    SW_EXPECT_EQUAL( string( pFirst ), string( pSecond ) );

    const string folder = useReportFolder( "reports" );
    SW_REQUIRE_FALSE( folder.empty() );

    utf8 arrPath[constant::buffer::kMax1024]{};
    _handler.makeReportPath( arrPath, constant::buffer::kMax1024, "dmp" );

    const string path{ arrPath };
    SW_EXPECT_TRUE( path.find( folder ) != string::npos );
    SW_EXPECT_TRUE( path.find( pFirst ) != string::npos );
    SW_EXPECT_TRUE( path.find( ".dmp" ) != string::npos );
}

/**
 * @brief 빵부스러기 — 최근 32 줄만 오래된 것부터 남고(긴 줄은 잘린다), 크래시 문맥과 함께 `breadcrumbs.txt` 로 쓰인다(할당 없이 쓰는 고정 고리).
 */
SW_TEST_CASE_F( CrashReportTest, BreadcrumbsKeepTheLatestLinesInOrder )
{
    SW_REQUIRE_FALSE( useReportFolder( "reports" ).empty() );
    const CrashBreadcrumbStore& store = _handler.getBreadcrumbStore();
    for ( uint32 index = 0; index < 40; ++index )
    {
        StringBuilder<constant::buffer::kMax64> line;
        line.appendFormat( "event %#", index );
        _handler.addBreadcrumb( line.view() );
    }
    _handler.addBreadcrumb( string( 400, 'z' ) );

    utf8         arrText[CrashBreadcrumbStore::kMaxBreadcrumb * CrashBreadcrumbStore::kMaxLength]{};
    const uint32 length = store.writeText( arrText, static_cast<uint32>( sizeof( arrText ) ) );
    const string text( arrText, length );
    SW_EXPECT_TRUE( text.find( "event 8\n" ) == string::npos );
    SW_EXPECT_TRUE( text.find( "event 9\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "event 9\n" ) < text.find( "event 39\n" ) );
    string truncatedLine( CrashBreadcrumbStore::kMaxLength - 1, 'z' ); // 긴 줄은 칸(널 포함)에 맞게 잘린다
    truncatedLine += '\n';
    SW_EXPECT_TRUE( text.find( truncatedLine ) != string::npos );
    uint32 lineCount = 0;
    for ( const utf8 character : text )
        lineCount += character == '\n' ? 1u : 0u;
    SW_EXPECT_EQUAL( CrashBreadcrumbStore::kMaxBreadcrumb, lineCount );

    _handler.writeContextFile( "breadcrumb test", nullptr, 1, 2 );
    string fileText;
    SW_REQUIRE_TRUE( readReportFile( "breadcrumbs.txt", fileText ) );
    SW_EXPECT_TRUE( fileText == text );
}

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
    string makeCrashChildArgument( CrashTestKind kind, const string& folder, uint32 deadlineSeconds = 0 )
    {
        string argument = formatToString( "%#|%#", static_cast<int32>( kind ), folder );
        if ( deadlineSeconds > 0 )
            argument += formatToString( "|%#", deadlineSeconds );
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
        CrashTestKind _kind;
        const utf8*   _pName;
    };
    constexpr CrashCase kArrCrashCase[] = {
        {    CrashTestKind::AccessViolation,      "access-violation"},
        {      CrashTestKind::StackOverflow,        "stack-overflow"},
        {CrashTestKind::WorkerStackOverflow, "worker-stack-overflow"},
        {              CrashTestKind::Abort,                 "abort"},
        {    CrashTestKind::PureVirtualCall,     "pure-virtual-call"},
    };
} // namespace

/**
 * @brief 자식 역할: 인자(`<종류>|<보고 폴더>[|<보고 시한>]`)가 시키는 방식으로 죽는다. 돌아오면 실패다.
 */
SW_TEST_CHILD_ROLE( CrashReportTest, ChildCrashesAsRequested )
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
    CrashHandler::crashForTest( static_cast<CrashTestKind>( kindCode ) );
    SW_EXPECT_TRUE_MSG( false, "crashForTest returned instead of crashing" );
}

/**
 * @brief 죽는 방식마다(접근 위반 · 스택 오버플로 · 작업 스레드 스택 오버플로 · abort · 순수 가상 호출) 보고가 남는다 — 방식마다 자식 하나.
 * @details 스택 오버플로: 넘친 스택에서 덤프를 쓰면 다시 넘쳐 덤프가 0 바이트다(Windows), 작업 스레드에도 대체 스택이 있어야 한다(POSIX).
 *          abort · 순수 가상 호출: CRT 가 `__fastfail` 로 끝내면 필터에 아예 들어오지 않는다(Windows).
 */
SW_TEST_CASE_EACH( CrashReportTest, EveryCrashKindLeavesAReport, kArrCrashCase, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const string folder = test::makeScratchPath( param._pName );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );

    const test::ChildRunResult child =
        test::runChildRole( "CrashReportTest.ChildCrashesAsRequested", makeCrashChildArgument( param._kind, folder ), kCrashChildTimeoutSeconds );
    SW_REQUIRE_TRUE_MSG( child._bLaunched && child._bTimedOut == false, param._pName );
    SW_EXPECT_TRUE_MSG( child._exitCode != 0, param._pName + string( " - last output:" ) + child.getOutputTail() );
    expectCrashFiles( folder, param._pName );
}

/**
 * @brief 보고가 끝날 수 없어도(다른 스레드가 stderr 를 쥔 채 놓지 않는다) 프로세스는 보고 시한에 끝나고 스택 파일은 남는다.
 * @details 보고는 죽어 가는 프로세스 안에서 돈다 — 크래시가 남긴 락을 기다리면 영영 끝나지 않는다. 스택 파일을 stderr 보다 먼저 쓰고, 시한이 지났다는 줄은
 *          stderr(`fputs`)가 아닌 길로 쓴다. 보고 시한 2 초는 자식 시한보다 한참 짧다 — 자식 시한에 죽으면 보고 시한이 듣지 않은 것이다.
 */
SW_TEST_CASE( CrashReportTest, StuckReportStillEndsAndKeepsTheStackFile, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const string folder = test::makeScratchPath( "stuck" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );

    const test::ChildRunResult child =
        test::runChildRole( "CrashReportTest.ChildCrashesAsRequested", makeCrashChildArgument( CrashTestKind::StderrHeld, folder, kStuckReportDeadlineSeconds ), kCrashChildTimeoutSeconds );
    SW_REQUIRE_TRUE( child._bLaunched && child._bTimedOut == false );
    SW_EXPECT_TRUE( child._exitCode != 0 );
    SW_EXPECT_TRUE_MSG( child._output.find( "made no progress before its deadline" ) != string::npos, "no line saying the report hit its deadline - last output:" + child.getOutputTail() );
    expectCrashFiles( folder, "stuck" );
}

    #if defined( SW_DEBUG )
/**
 * @brief 실패한 `SW_ASSERT` 는 멈추기 전에 식 · 자리를 남기고 크래시 보고도 남는다(단언은 Debug 에만 있다).
 * @details 아무것도 찍지 않고 멈추면 디버거 없이 돌 때 "EXCEPTION_BREAKPOINT"(리눅스 SIGILL)와 스택만 남아 어느 식이 어긋났는지 모른다.
 */
SW_TEST_CASE( CrashReportTest, FailedAssertNamesItsExpressionBeforeStopping, ._tags = "slow", ._timeoutSeconds = 180.0f )
{
    const string folder = test::makeScratchPath( "assert" );
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( folder ) );

    const test::ChildRunResult child =
        test::runChildRole( "CrashReportTest.ChildCrashesAsRequested", makeCrashChildArgument( CrashTestKind::AssertFailure, folder ), kCrashChildTimeoutSeconds );
    SW_REQUIRE_TRUE( child._bLaunched && child._bTimedOut == false );
    SW_EXPECT_TRUE( child._exitCode != 0 );
    const bool bNamesExpression = child._output.find( "[SW_ASSERT] s_bAssertProbeHolds" ) != string::npos;
    const bool bNamesPlace      = child._output.find( "CrashContext.cpp:" ) != string::npos;
    SW_EXPECT_TRUE_MSG( bNamesExpression && bNamesPlace, "the assert did not name its expression and place - last output:" + child.getOutputTail() );
    SW_EXPECT_FALSE( findReportFile( folder, "stack.txt" ).empty() );
}
    #endif
#endif
