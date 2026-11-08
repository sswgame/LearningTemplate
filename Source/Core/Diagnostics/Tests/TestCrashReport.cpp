// 크래시 보고 — 크래시 경로에서 할당 없이 미리 담아 둔 문맥(키-값 · 빵부스러기)과 세 플랫폼이 함께 쓰는 보고 본문. 앞 절은 케이스마다 제 처리기(설치하지 않은
// `CrashHandler`)를 만들어 보고 조각을 직접 부른다 — 앱의 처리기는 건드리지 않는다. 끝 절은 자식 역할(`SW_TEST_CHILD_ROLE`)을 띄워 진짜로 죽인다.
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/PlatformOSHeaders.h"
#include "Core/Process/ProcessSession.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 케이스의 처리기(설치하지 않는다)와 그 보고 폴더입니다. */
    struct CrashReportTest
    {

        CrashHandler _handler;
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
SW_TEST_CASE_F( CrashReportTest, ReportFolderWithANonASCIINameReceivesTheFiles )
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
