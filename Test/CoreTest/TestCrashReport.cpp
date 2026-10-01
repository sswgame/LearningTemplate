#include "pch.h"

#include "Core/Common/PlatformOsHeaders.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/CrashContext.h"
#include "Core/Process/CrashHandler.h"
#include "Core/Process/Process.h"
#include "Core/String/StringBuilder.h"

#include "TestFramework/TestFramework.h"

#include <cstdlib>

// ------------------------------------------------------------------------------
// 1) CrashReport — 크래시 경로에서 **할당 없이** 미리 담아 둔 컨텍스트와, 세 플랫폼이 함께 쓰는 리포트 본문
//    진짜 크래시를 낼 수는 없으므로 리포트를 만드는 조각들을 직접 부른다.
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 이 파일의 테스트가 리포트를 쏟아 놓을 임시 폴더를 준비하고 그 경로를 돌려줍니다. */
    sw::string prepareReportFolderInternal()
    {
        const sw::string folder = test::makeTempDirectory( "SwCrashReportTest" );
        sw::setCrashReportFolder( folder );
        return folder;
    }

    /** @brief 자식 프로세스가 물려받을 환경 변수를 정합니다. @p pValue 가 nullptr 이면 지웁니다. */
    void setEnvironmentValueInternal( const utf8* pName, const utf8* pValue )
    {
#if defined( SW_PLATFORM_WINDOWS )
        SetEnvironmentVariableA( pName, pValue );
#else
        if ( pValue != nullptr )
            setenv( pName, pValue, 1 );
        else
            unsetenv( pName );
#endif
    }

    /** @brief @p folder 에서 이름이 @p suffix 로 끝나는 첫 파일을 찾습니다. 없으면 빈 문자열입니다. */
    sw::string findReportFileInternal( const sw::string& folder, sw::string_view suffix )
    {
        sw::vector<sw::string> listFile;
        sw::FileUtil::collectFiles( folder, "", listFile, false );
        for ( const sw::string& filePath : listFile )
        {
            const sw::string_view pathView{ filePath.c_str(), filePath.size() };
            if ( pathView.size() >= suffix.size() && pathView.substr( pathView.size() - suffix.size() ) == suffix )
                return filePath;
        }
        return {};
    }

    /** @brief 세션 ID 로 만들어지는 리포트 파일 하나를 읽습니다. */
    bool readReportFileInternal( const utf8* pExtension, sw::string& outText )
    {
        utf8 arrPath[sw::constant::kMaxBuffer1024]{};
        sw::buildCrashReportPath( arrPath, sw::constant::kMaxBuffer1024, pExtension );
        return sw::FileUtil::readTextFile( arrPath, outText );
    }
} // namespace

/**
 * @brief [CrashReportTest] 미리 등록해 둔 키-값이 컨텍스트 파일에 그대로 나온다
 * @details 덤프·코어만으로는 "어느 백엔드였는가" 를 알 수 없다. 그 답은 크래시 **전에** 올려 둔
 *          것에서만 나오므로, 올린 것이 실제로 파일에 닿는지 못박는다.
 */
SW_TEST_CASE( CrashReportTest, RegisteredContextReachesTheContextFile )
{
    prepareReportFolderInternal();

    sw::CrashHandler::setContextValue( "RHI", "DX12" );
    sw::CrashHandler::setContextValue( "Build", "CrashReportTest" );

    sw::writeCrashContextFile( "unit test reason", reinterpret_cast<const void*>( 0xDEADBEEFull ), 4242, 8484 );

    sw::string text;
    SW_ASSERT_TRUE( readReportFileInternal( "txt", text ) );

    SW_EXPECT_TRUE( text.find( "unit test reason" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "deadbeef" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "4242" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "8484" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "DX12" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "CrashReportTest" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( sw::CrashHandler::getSessionId() ) != sw::string::npos );
}

/**
 * @brief [CrashReportTest] 같은 키는 덮어쓰고, 자리가 차면 조용히 버린다
 * @details 자리가 없을 때 버리는 것은 의도다 — 크래시 진단을 돕자고 넣은 것이 실패를 키우면 안 된다.
 *          다만 **버리되 망가뜨리지는 않아야** 한다. 이미 들어간 값은 그대로 남아야 한다.
 */
SW_TEST_CASE( CrashReportTest, ContextStoreOverwritesKeyAndDropsOverflowSafely )
{
    prepareReportFolderInternal();

    sw::CrashContextStore& store = sw::CrashContextStore::get();
    store._entryCount            = 0;

    store.set( "RHI", "Vulkan" );
    store.set( "RHI", "DX11" );
    SW_EXPECT_EQUAL( 1u, store._entryCount );
    SW_EXPECT_TRUE( store._arrEntry[0]._value.view() == sw::string_view( "DX11" ) );

    // 정원을 넘겨 본다. 개수는 정원에서 멈추고 첫 항목은 그대로여야 한다.
    for ( uint32 index = 0; index < sw::CrashHandler::kMaxContextEntry + 8; ++index )
    {
        const sw::string key = "Key" + sw::string( std::to_string( index ).c_str() );
        store.set( key, "value" );
    }
    SW_EXPECT_EQUAL( sw::CrashHandler::kMaxContextEntry, store._entryCount );
    SW_EXPECT_TRUE( store._arrEntry[0]._value.view() == sw::string_view( "DX11" ) );

    // 빈 키는 아무 일도 하지 않는다.
    const uint32 countBefore = store._entryCount;
    store.set( "", "ignored" );
    SW_EXPECT_EQUAL( countBefore, store._entryCount );

    store._entryCount = 0;
}

/**
 * @brief [CrashReportTest] 리포트가 "무엇을 보내면 되는지" 를 적고, 없는 덤프는 적지 않는다
 * @details 이 본문은 예전에 Windows 와 POSIX 가 각자 적고 있었고 이미 갈려 있었다 — 파일 목록이
 *          Windows 에만 있었고(리눅스 사용자는 리포트가 어디 났는지 알 수 없었다), 그 목록마저
 *          미니덤프를 **쓰지 못했을 때도** 적고 스택 파일은 아예 빠뜨렸다. 이제 한 곳에서 만든다.
 */
SW_TEST_CASE( CrashReportTest, ReportListsFilesToSendAndSkipsTheDumpWhenThereIsNone )
{
    prepareReportFolderInternal();

    utf8 arrContextPath[sw::constant::kMaxBuffer1024]{};
    utf8 arrStackPath[sw::constant::kMaxBuffer1024]{};
    utf8 arrDumpPath[sw::constant::kMaxBuffer1024]{};
    sw::buildCrashReportPath( arrContextPath, sw::constant::kMaxBuffer1024, "txt" );
    sw::buildCrashReportPath( arrStackPath, sw::constant::kMaxBuffer1024, "stack.txt" );
    sw::buildCrashReportPath( arrDumpPath, sw::constant::kMaxBuffer1024, "dmp" );

    // 1) 덤프가 없을 때 — 컨텍스트와 스택 파일만 적혀야 한다.
    sw::writeCrashReport( "unit test fault", reinterpret_cast<const void*>( 0x1234ull ), nullptr, false );

    sw::string text;
    SW_ASSERT_TRUE( readReportFileInternal( "stack.txt", text ) );

    SW_EXPECT_TRUE( text.find( "unit test fault" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( "crash report files" ) != sw::string::npos );
    SW_EXPECT_TRUE( text.find( arrContextPath ) != sw::string::npos );
    SW_EXPECT_TRUE_MSG( text.find( arrStackPath ) != sw::string::npos,
                        "스택 파일 경로가 목록에 없다 — 정작 스택이 든 파일을 안 보내게 된다" );
    SW_EXPECT_TRUE_MSG( text.find( arrDumpPath ) == sw::string::npos,
                        "쓰지도 않은 미니덤프를 보내라고 적었다" );

    // 2) 덤프를 썼을 때 — 그때만 덤프 경로가 붙는다.
    sw::writeCrashReport( "unit test fault", reinterpret_cast<const void*>( 0x1234ull ), nullptr, true );

    SW_ASSERT_TRUE( readReportFileInternal( "stack.txt", text ) );
    SW_EXPECT_TRUE( text.find( arrDumpPath ) != sw::string::npos );
}

/**
 * @brief [CrashReportTest] 세션 ID 는 한 프로세스에서 하나이고 리포트 경로에 들어간다
 * @details 고객이 보낸 덤프와 로그가 같은 실행의 것인지 확인하는 유일한 방법이다.
 */
SW_TEST_CASE( CrashReportTest, SessionIdIsStableAndAppearsInReportPaths )
{
    const utf8* pFirst  = sw::CrashHandler::getSessionId();
    const utf8* pSecond = sw::getCrashSessionId();

    SW_ASSERT_NOT_NULL( pFirst );
    SW_EXPECT_STREQ( pFirst, pSecond );

    const sw::string folder = prepareReportFolderInternal();

    utf8 arrPath[sw::constant::kMaxBuffer1024]{};
    sw::buildCrashReportPath( arrPath, sw::constant::kMaxBuffer1024, "dmp" );

    const sw::string path{ arrPath };
    SW_EXPECT_TRUE( path.find( folder ) != sw::string::npos );
    SW_EXPECT_TRUE( path.find( pFirst ) != sw::string::npos );
    SW_EXPECT_TRUE( path.find( ".dmp" ) != sw::string::npos );
}

// ------------------------------------------------------------------------------
// 2) 진짜 크래시 — 이 실행 파일을 자식으로 띄워 죽게 하고, 리포트가 남았는지 본다
//    크래시 핸들러는 크래시가 나야만 돈다. 조각을 직접 부르는 위 테스트로는 "그 방식으로 죽으면 핸들러에 들어오기는 하는가 · 들어와서
//    쓸 스택이 남아 있는가" 를 볼 수 없다. 실측으로 스택 오버플로(덤프 0 바이트)와 abort · 순수 가상 호출(필터 미진입)이 그렇게 새고 있었다.
// ------------------------------------------------------------------------------

/**
 * @brief [CrashReportTest] 자식 프로세스 역할: 환경 변수가 시키는 방식으로 죽는다. 그냥 실행하면 건너뛴다.
 * @details 따로 스위트를 두지 않는다 — 이 케이스만 있는 스위트는 평소 실행에서 "모든 케이스가 건너뜀" 이 되어 실패로 친다.
 */
SW_TEST_CASE( CrashReportTest, ChildProcessCrashesAsRequested )
{
    const utf8* pKind   = std::getenv( "SW_CRASH_CHILD_KIND" );
    const utf8* pFolder = std::getenv( "SW_CRASH_CHILD_FOLDER" );
    if ( pKind == nullptr || pFolder == nullptr )
        SW_TEST_SKIP( "crash child only — EveryCrashKindLeavesAReport launches it" );

    sw::setCrashReportFolder( pFolder );
#if defined( SW_PLATFORM_WINDOWS )
    // 오류 보고 창이 떠서 부모가 기다리지 않게 한다(핸들러가 처리하면 원래 뜨지 않는다 — 처리하지 못했을 때를 위한 것).
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );
#endif
    sw::CrashHandler::crashForTest( static_cast<sw::CrashTestKind>( std::atoi( pKind ) ) );
    SW_EXPECT_TRUE_MSG( false, "crashForTest returned instead of crashing" );
}

/**
 * @brief [CrashReportTest] 죽는 방식마다(접근 위반 · 스택 오버플로 · 작업 스레드 스택 오버플로 · abort · 순수 가상 호출) 리포트가 남는다.
 * @details 자식이 죽은 뒤 리포트 폴더에 콜 스택 파일이 있어야 하고, Windows 는 미니덤프도 비어 있지 않아야 한다. 예전에는
 *          - 스택 오버플로: 필터에 들어오지만 넘친 스택에서 다시 넘쳐 덤프가 0 바이트였다(Windows). 작업 스레드에는 대체 스택이 없었다(POSIX).
 *          - abort · 순수 가상 호출: CRT 가 `__fastfail` 로 끝내 필터에 아예 들어오지 않았다(Windows).
 */
SW_TEST_CASE( CrashReportTest, EveryCrashKindLeavesAReport )
{
#if defined( SW_SANITIZER_ADDRESS ) || defined( SW_SANITIZER_THREAD )
    SW_TEST_SKIP( "AddressSanitizer owns the fatal signals and pads the frames — it reports these crashes itself" );
#endif

    struct CrashCase
    {
        sw::CrashTestKind _kind;
        const utf8*       _pName;
    };
    const CrashCase arrCase[] = {
        {    sw::CrashTestKind::AccessViolation,      "access-violation"},
        {      sw::CrashTestKind::StackOverflow,        "stack-overflow"},
        {sw::CrashTestKind::WorkerStackOverflow, "worker-stack-overflow"},
        {              sw::CrashTestKind::Abort,                 "abort"},
        {    sw::CrashTestKind::PureVirtualCall,     "pure-virtual-call"},
    };

    const sw::string executablePath = sw::FileUtil::getExecutablePath();
    SW_ASSERT_FALSE( executablePath.empty() );

    for ( const CrashCase& crashCase : arrCase )
    {
        sw::StringBuilder<sw::constant::kMaxBuffer64> folderName;
        folderName.append( "SwCrashChild_" ).append( crashCase._pName );
        const sw::string folder = test::makeTempPath( folderName.view() );
        sw::FileUtil::removeDirectory( folder );
        sw::FileUtil::ensureDirectoryExists( folder );

        sw::StringBuilder<sw::constant::kMaxBuffer16> kindText;
        kindText.append( static_cast<int32>( crashCase._kind ) );
        setEnvironmentValueInternal( "SW_CRASH_CHILD_KIND", kindText.c_str() );
        setEnvironmentValueInternal( "SW_CRASH_CHILD_FOLDER", folder.c_str() );

        sw::StringBuilder<sw::constant::kMaxPathSize> command;
        command.append( '"' ).append( executablePath.c_str() ).append( "\" --test_filter=CrashReportTest.ChildProcessCrashesAsRequested" );
        sw::ProcessOptions options;
        options._workingDirectory = sw::FileUtil::getCurrentPath();
        const int32 exitCode      = sw::Process::execute( command.view(), options );

        setEnvironmentValueInternal( "SW_CRASH_CHILD_KIND", nullptr );
        setEnvironmentValueInternal( "SW_CRASH_CHILD_FOLDER", nullptr );

        SW_EXPECT_TRUE_MSG( exitCode != 0, crashCase._pName );

        const sw::string stackPath = findReportFileInternal( folder, "stack.txt" );
        SW_EXPECT_TRUE_MSG( stackPath.empty() == false, crashCase._pName );
        if ( stackPath.empty() == false )
        {
            sw::string stackText;
            SW_EXPECT_TRUE( sw::FileUtil::readTextFile( stackPath, stackText ) );
            SW_EXPECT_TRUE_MSG( stackText.find( "CRASH" ) != sw::string::npos, crashCase._pName );
        }
#if defined( SW_PLATFORM_WINDOWS )
        const sw::string dumpPath = findReportFileInternal( folder, ".dmp" );
        SW_EXPECT_TRUE_MSG( dumpPath.empty() == false && sw::FileUtil::getFileSize( dumpPath ) > 0, crashCase._pName );
#endif
        sw::FileUtil::removeDirectory( folder );
    }
}
