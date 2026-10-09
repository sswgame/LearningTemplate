#include "pch.h"

#include "Core/CommandLine/CommandLineManager.h"
#include "Core/Common/PlatformOsHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/Diagnostics/CrashContext.h"
#include "Core/Diagnostics/CrashHandler.h"
#include "Core/File/FileUtil.h"

#include "Engine/Serialization/Json/JsonDocument.h"
#include "Engine/Telemetry/CrashReportService.h"
#include "Engine/Telemetry/CrashReportUploader.h"
#include "Engine/Telemetry/HttpClient.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "TestFramework/TestChildProcess.h"
#include "TestFramework/TestFramework.h"

#include <cstdlib>

// 크래시 보고 묶음 — 지난 실행의 크래시 파일(가짜 · 진짜 자식 크래시)을 묶음 폴더로 모으고, 동의(local · ask · send · 철회)가 무엇이 기계를 떠나는지 정하고,
// 보고 프로세스 일(runReporter)이 보낼 줄만 올리며, HTTP 업로더가 multipart 요청을 만든다. 실제 네트워크에는 닿지 않는다(가짜 창구만).

using namespace sw;

namespace
{
    struct CrashBundleTestInternal
    {
        static constexpr const utf8* kCurrentSession = "currentsession00";

        /** @brief 받은 묶음을 적고 정한 결과를 돌려주는 가짜 업로더입니다. */
        class RecordingUploader final : public ICrashReportUploader
        {
        public:
            CrashReportUploadResult upload( const CrashReportUploadBundle& bundle ) override
            {
                _listBundle.push_back( bundle );
                return _result;
            }

            vector<CrashReportUploadBundle> _listBundle{};
            CrashReportUploadResult         _result{ CrashReportUploadResult::Sent };
        };

        class RecordingHttpClient final : public IHttpClient
        {
        public:
            HttpResponse send( const HttpRequest& request ) override
            {
                _listRequest.push_back( request );
                HttpResponse response;
                response._status = 200;
                return response;
            }

            vector<HttpRequest> _listRequest{};
        };

        /** @brief 크래시 핸들러가 남기는 모양 그대로의 가짜 크래시 파일을 씁니다. */
        static void writeFakeCrash( const string& folder, const utf8* pSession, const utf8* pReason )
        {
            const string prefix = FileUtil::joinPath( folder, string( "crash_" ) + pSession );
            string       context;
            context += "session   : ";
            context += pSession;
            context += "\nreason    : ";
            context += pReason;
            context += "\naddress   : 0x0\nprocessId : 77\nthreadId  : 88\nBuild    : Debug\nPlatform : Windows\nRHI      : DirectX12\nGPU      : Test GPU\n";
            context += "BuildId  : 0A1B2C3D4E5F60718293A4B5C6D7E8F91\n";
            SW_EXPECT_TRUE( FileUtil::writeTextFile( prefix + ".txt", context ) );
            const uint8 arrDump[] = { 'M', 'D', 'M', 'P', 0x00, 0x01, 0x02, 0xff };
            SW_EXPECT_TRUE( FileUtil::writeFile( prefix + ".dmp", arrDump, sizeof( arrDump ) ) );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( prefix + ".stack.txt", "==== CRASH ====\n[0] sw::Game::tick\n" ) );
            SW_EXPECT_TRUE( FileUtil::writeTextFile( prefix + ".breadcrumbs.txt", "1.000 session.start\n12.500 progression.waveReached wave=2\n13.000 game.boss\n" ) );
        }

        static string bundleFolder( const string& reportsFolder, const utf8* pSession ) { return FileUtil::joinPath( reportsFolder, string( "crash_" ) + pSession ); }

        static JsonDocument loadManifest( const string& reportsFolder, const utf8* pSession )
        {
            JsonDocument doc;
            string       text;
            if ( FileUtil::readTextFile( FileUtil::joinPath( bundleFolder( reportsFolder, pSession ), "manifest.json" ), text ) )
                (void)doc.tryParse( text ); // 깨진 manifest 는 빈 문서로 남아 부르는 쪽 단언이 값으로 드러낸다
            return doc;
        }

        static string stateOf( const string& reportsFolder, const utf8* pSession ) { return loadManifest( reportsFolder, pSession ).getRoot().get( "state" ).asString(); }
    };
} // namespace

/**
 * @brief [CrashBundleTest] 지난 실행의 크래시를 묶음으로 — 덤프 · 컨텍스트 · 스택 · 빵부스러기를 옮기고 그 세션의 로그 끝을 복사하며, 매니페스트에 사유 · 빌드 id ·
 *        시스템 정보 · 파일 목록을 적는다. 지금 세션은 묶지 않고, 두 번 묶지 않는다
 */
SW_TEST_CASE( CrashBundleTest, BundlesPreviousCrashesOnce )
{
    using Internal             = CrashBundleTestInternal;
    const string crashFolder   = test::makeTempDirectory( "Logs" );
    const string reportsFolder = test::makeTempDirectory( "CrashReports" );
    Internal::writeFakeCrash( crashFolder, "oldsession01", "EXCEPTION_ACCESS_VIOLATION" );
    Internal::writeFakeCrash( crashFolder, Internal::kCurrentSession, "still running" );
    // 그 세션의 로그(크기 상한보다 크다)와 다른 세션의 로그.
    string bigLog( static_cast<size_t>( CrashReportService::kMaxLogBytes ), 'x' );
    bigLog += "LAST LINE BEFORE THE CRASH\n";
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( crashFolder, "LOG_2026-10-4-19_oldsession01.txt" ), string( "FIRST LINE\n" ) + bigLog ) );
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( crashFolder, "LOG_2026-10-4-19_othersession.txt" ), "OTHER SESSION\n" ) );

    CrashReportService service;
    service.initialize( crashFolder, reportsFolder, Internal::kCurrentSession );
    SW_EXPECT_EQUAL( 1u, service.collectNewCrashes() );
    SW_EXPECT_EQUAL( 0u, service.collectNewCrashes() );

    const string bundle = Internal::bundleFolder( reportsFolder, "oldsession01" );
    for ( const utf8* pName : { "crash.dmp", "crash.txt", "crash.stack.txt", "crash.breadcrumbs.txt", "last.log", "manifest.json" } )
    {
        SW_EXPECT_TRUE_MSG( FileUtil::exists( FileUtil::joinPath( bundle, pName ) ), pName );
    }
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::joinPath( crashFolder, "crash_oldsession01.dmp" ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( FileUtil::joinPath( crashFolder, "crash_currentsession00.dmp" ) ) );
    SW_EXPECT_FALSE( FileUtil::isDirectory( Internal::bundleFolder( reportsFolder, Internal::kCurrentSession ) ) );

    string lastLog;
    SW_ASSERT_TRUE( FileUtil::readTextFile( FileUtil::joinPath( bundle, "last.log" ), lastLog ) );
    SW_EXPECT_TRUE( lastLog.size() <= CrashReportService::kMaxLogBytes + 64u );
    SW_EXPECT_TRUE( lastLog.find( "LAST LINE BEFORE THE CRASH" ) != string::npos );
    SW_EXPECT_TRUE( lastLog.find( "FIRST LINE" ) == string::npos );
    SW_EXPECT_TRUE( lastLog.find( "OTHER SESSION" ) == string::npos );

    const JsonDocument manifest = Internal::loadManifest( reportsFolder, "oldsession01" );
    const JsonValue    root     = manifest.getRoot();
    SW_EXPECT_TRUE( root.get( "session" ).asString() == "oldsession01" );
    SW_EXPECT_TRUE( root.get( "reason" ).asString() == "EXCEPTION_ACCESS_VIOLATION" );
    SW_EXPECT_TRUE( root.get( "buildId" ).asString() == "0A1B2C3D4E5F60718293A4B5C6D7E8F91" );
    SW_EXPECT_TRUE( root.get( "rhi" ).asString() == "DirectX12" && root.get( "gpu" ).asString() == "Test GPU" );
    SW_EXPECT_TRUE( root.get( "state" ).asString() == "local" );
    SW_EXPECT_EQUAL( 3u, static_cast<uint32>( root.get( "breadcrumbCount" ).asUint() ) );
    SW_EXPECT_TRUE( root.get( "system" ).get( "logicalCores" ).asUint() > 0u );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( root.get( "files" ).size() ) );
    SW_EXPECT_TRUE( root.get( "bundledBy" ).asString() == Internal::kCurrentSession );

    vector<CrashReportSummary> listReport;
    service.collectReports( listReport );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listReport.size() ) );
    SW_EXPECT_TRUE( listReport[0]._state == CrashReportState::Local && listReport[0]._reason == "EXCEPTION_ACCESS_VIOLATION" );
}

/**
 * @brief [CrashBundleTest] 동의 — local 은 보내지 않고, ask 는 답을 기다려 답대로, send 는 보낼 줄에 세운다. local 로 바꾸면 아직 보내지 않은 묶음이 기계 안으로
 *        돌아온다. 보고 프로세스 일은 보낼 줄만 올리고, 실패는 시도 상한까지만 다시 한다
 */
SW_TEST_CASE( CrashBundleTest, ConsentDecidesWhatLeavesTheMachine )
{
    using Internal                   = CrashBundleTestInternal;
    const string       crashFolder   = test::makeTempDirectory( "Logs" );
    const string       reportsFolder = test::makeTempDirectory( "CrashReports" );
    CrashReportService service;
    service.initialize( crashFolder, reportsFolder, Internal::kCurrentSession );
    Internal::RecordingUploader uploader;

    // local(기본): 묶기만 — 올리는 일이 아무것도 보내지 않는다.
    SW_EXPECT_TRUE( service.getConsent() == CrashReportConsent::Local );
    Internal::writeFakeCrash( crashFolder, "localcrash", "abort" );
    SW_EXPECT_EQUAL( 1u, service.collectNewCrashes() );
    SW_EXPECT_EQUAL( 0u, service.countPendingUploads() );
    SW_EXPECT_FALSE( service.launchReporterProcess() );
    SW_EXPECT_EQUAL( 0u, CrashReportService::runReporter( reportsFolder, uploader ) );
    SW_EXPECT_TRUE( uploader._listBundle.empty() );

    // ask: 답을 기다린다 — 아니오는 Declined, 예는 보낼 줄.
    service.setConsent( CrashReportConsent::Ask );
    Internal::writeFakeCrash( crashFolder, "askno", "abort" );
    Internal::writeFakeCrash( crashFolder, "askyes", "abort" );
    SW_EXPECT_EQUAL( 2u, service.collectNewCrashes() );
    SW_EXPECT_TRUE( Internal::stateOf( reportsFolder, "askno" ) == "awaitingDecision" );
    SW_EXPECT_EQUAL( 0u, CrashReportService::runReporter( reportsFolder, uploader ) );
    SW_EXPECT_TRUE( service.decide( "askno", false ) );
    SW_EXPECT_TRUE( service.decide( "askyes", true ) );
    SW_EXPECT_FALSE( service.decide( "askyes", true ) );     // 이미 답했다
    SW_EXPECT_FALSE( service.decide( "localcrash", true ) ); // local 로 모인 것은 묻지 않는다
    SW_EXPECT_TRUE( Internal::stateOf( reportsFolder, "askno" ) == "declined" );
    SW_EXPECT_EQUAL( 1u, service.countPendingUploads() );
    // 보고 실행 파일을 정하지 않은 호스트(이 시험 실행 파일)는 보낼 것이 있어도 자기를 다시 띄우지 않는다.
    SW_EXPECT_FALSE( service.launchReporterProcess() );

    // 보고 프로세스 일: 보낼 줄 하나만 올리고 Sent, 덤프는 지운다.
    SW_EXPECT_EQUAL( 1u, CrashReportService::runReporter( reportsFolder, uploader ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( uploader._listBundle.size() ) );
    SW_EXPECT_TRUE( uploader._listBundle[0]._sessionId == "askyes" );
    SW_EXPECT_TRUE( uploader._listBundle[0]._manifest.find( "\"askyes\"" ) != string::npos );
    SW_EXPECT_EQUAL( 4u, static_cast<uint32>( uploader._listBundle[0]._listFilePath.size() ) ); // 덤프 · 컨텍스트 · 스택 · 빵부스러기(이 세션의 로그는 없다)
    SW_EXPECT_TRUE( Internal::stateOf( reportsFolder, "askyes" ) == "sent" );
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::joinPath( Internal::bundleFolder( reportsFolder, "askyes" ), "crash.dmp" ) ) );

    // send: 바로 보낼 줄. 그런데 보내기 전에 local 로 바꾸면(철회) 보내지 않는다 — 기다리던 ask 묶음도.
    service.setConsent( CrashReportConsent::Send );
    Internal::writeFakeCrash( crashFolder, "sendcrash", "abort" );
    SW_EXPECT_EQUAL( 1u, service.collectNewCrashes() );
    SW_EXPECT_TRUE( Internal::stateOf( reportsFolder, "sendcrash" ) == "queued" );
    SW_EXPECT_TRUE( Internal::stateOf( reportsFolder, "localcrash" ) == "local" ); // 소급해 보내지 않는다
    service.setConsent( CrashReportConsent::Local );
    SW_EXPECT_TRUE( Internal::stateOf( reportsFolder, "sendcrash" ) == "local" );
    uploader._listBundle.clear();
    SW_EXPECT_EQUAL( 0u, CrashReportService::runReporter( reportsFolder, uploader ) );
    SW_EXPECT_TRUE( uploader._listBundle.empty() );

    // 실패는 시도 상한(3)까지만 — 그 뒤로는 보고 프로세스를 띄우지 않는다.
    service.setConsent( CrashReportConsent::Send );
    Internal::writeFakeCrash( crashFolder, "failing", "abort" );
    SW_EXPECT_EQUAL( 1u, service.collectNewCrashes() );
    uploader._result = CrashReportUploadResult::Failed;
    for ( uint32 attempt = 0; attempt < CrashReportService::kMaxAttempts + 2; ++attempt )
    {
        (void)CrashReportService::runReporter( reportsFolder, uploader );
    }
    SW_EXPECT_EQUAL( CrashReportService::kMaxAttempts, static_cast<uint32>( uploader._listBundle.size() ) );
    SW_EXPECT_EQUAL( 0u, service.countPendingUploads() );
}

/**
 * @brief [CrashBundleTest] 보고 프로세스 명령줄 — `-crash-reporter="<폴더>"` 는 명령줄 표의 CRASH_REPORTER 로 읽히고(따옴표는 OS 가 벗긴다),
 *        없으면 비어 있다. 보고 프로세스의 기본 업로더(`NullCrashReportUploader`)는 보내지 않고 시도만 센다
 */
SW_TEST_CASE( CrashBundleTest, ReporterArgumentIsACommandLineEntry )
{
    using Internal                   = CrashBundleTestInternal;
    const string       crashFolder   = test::makeTempDirectory( "Logs" );
    const string       reportsFolder = test::makeTempDirectory( "CrashReports" );
    CrashReportService service;
    service.initialize( crashFolder, reportsFolder, Internal::kCurrentSession );
    service.setConsent( CrashReportConsent::Send );
    Internal::writeFakeCrash( crashFolder, "queued01", "abort" );
    SW_ASSERT_EQUAL( 1u, service.collectNewCrashes() );

    utf8  arrApp[]        = "App.exe";
    utf8  arrDx[]         = "-dx12";
    utf8* arrNoReporter[] = { arrApp, arrDx };
    {
        CommandLineManager commandLine;
        commandLine.initialize();
        commandLine.parse( 2, arrNoReporter );
        string folder;
        SW_EXPECT_FALSE( commandLine.getArgument( CommandLineArgument::CRASH_REPORTER, folder ) );
    }

    // 띄우는 쪽은 `-crash-reporter="<폴더>"` 로 적는다 — 프로세스가 받는 argv 에는 따옴표가 벗겨져 있다.
    string       reporterArgument = string( CrashReportService::kReporterArgument ) + "=" + reportsFolder;
    vector<utf8> argumentBytes( reporterArgument.begin(), reporterArgument.end() );
    argumentBytes.push_back( '\0' );
    utf8*              arrReporter[] = { arrApp, argumentBytes.data() };
    CommandLineManager commandLine;
    commandLine.initialize();
    commandLine.parse( 2, arrReporter );
    string folder;
    SW_ASSERT_TRUE( commandLine.getArgument( CommandLineArgument::CRASH_REPORTER, folder ) );
    SW_EXPECT_TRUE( folder == reportsFolder );
    SW_EXPECT_EQUAL( 0u, CrashReportService::runReporter( folder, NullCrashReportUploader::get() ) );
    const JsonDocument manifest = Internal::loadManifest( reportsFolder, "queued01" );
    SW_EXPECT_TRUE( manifest.getRoot().get( "state" ).asString() == "queued" );
    SW_EXPECT_EQUAL( 1u, static_cast<uint32>( manifest.getRoot().get( "attempts" ).asUint() ) );
}

/**
 * @brief [CrashBundleTest] HTTP 업로더 — multipart/form-data 로 매니페스트와 파일을 보내고 덤프는 `upload_file_minidump` 이다. 기본 창구는 보내지 않는다
 */
SW_TEST_CASE( CrashBundleTest, HttpUploaderSendsMultipartMinidump )
{
    using Internal                   = CrashBundleTestInternal;
    const string       crashFolder   = test::makeTempDirectory( "Logs" );
    const string       reportsFolder = test::makeTempDirectory( "CrashReports" );
    CrashReportService service;
    service.initialize( crashFolder, reportsFolder, Internal::kCurrentSession );
    service.setConsent( CrashReportConsent::Send );
    Internal::writeFakeCrash( crashFolder, "multipart", "abort" );
    SW_ASSERT_EQUAL( 1u, service.collectNewCrashes() );

    Internal::RecordingHttpClient client;
    HttpCrashReportUploader       uploader( client, "https://crash.invalid/api/minidump" );
    SW_EXPECT_EQUAL( 1u, CrashReportService::runReporter( reportsFolder, uploader ) );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( client._listRequest.size() ) );
    const HttpRequest& request = client._listRequest[0];
    SW_EXPECT_TRUE( request._url == "https://crash.invalid/api/minidump" );
    SW_EXPECT_TRUE( StringUtil::startsWith( request.findHeader( "Content-Type" ), "multipart/form-data; boundary=" ) );
    SW_EXPECT_TRUE( request.findHeader( "X-Crash-Session" ) == "multipart" );
    SW_EXPECT_TRUE( request._body.find( "name=\"manifest\"" ) != string::npos );
    SW_EXPECT_TRUE( request._body.find( "name=\"upload_file_minidump\"; filename=\"crash.dmp\"" ) != string::npos );
    SW_EXPECT_TRUE( request._body.find( string( "MDMP\0\x01\x02", 7 ) ) != string::npos );
    SW_EXPECT_TRUE( request._body.find( "crash.breadcrumbs.txt" ) != string::npos );
    SW_EXPECT_TRUE( StringUtil::endsWith( request._body, string( "--" ) + HttpCrashReportUploader::kBoundary + "--\r\n" ) );

    HttpCrashReportUploader offline( NullHttpClient::get(), "https://crash.invalid/api/minidump" );
    CrashReportUploadBundle bundle;
    SW_EXPECT_TRUE( offline.upload( bundle ) == CrashReportUploadResult::Failed );
}

/**
 * @brief [CrashBundleTest] 동의는 사용자 설정 `telemetry.crashReports` 의 확정 값이다(기본 local)
 */
SW_TEST_CASE( CrashBundleTest, ConsentFollowsTheUserSetting )
{
    UserSettingsManager settings;
    settings.initialize( UserSettingsTargets{} );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText( R"(<UserSettingsSchema version="1"><Category id="privacy"/>
<Setting id="telemetry.crashReports" category="privacy" type="enum" default="local"><Option value="local"/><Option value="ask"/><Option value="send"/></Setting>
</UserSettingsSchema>)",
                                                    "privacy.settings.xml" ) );
    settings.reapplyAll();
    CrashReportService service;
    service.initialize( test::makeTempDirectory( "Logs" ), test::makeTempDirectory( "CrashReports" ), CrashBundleTestInternal::kCurrentSession );
    service.bindConsentSetting( settings );
    SW_EXPECT_TRUE( service.getConsent() == CrashReportConsent::Local );
    SW_EXPECT_TRUE( settings.setPendingValue( "telemetry.crashReports", "ask" ) == UserSettingSetResult::Accepted );
    SW_EXPECT_TRUE( service.getConsent() == CrashReportConsent::Local );
    (void)settings.applyPending(); // 적용 결과는 아래 동의 단언이 확인한다
    SW_EXPECT_TRUE( service.getConsent() == CrashReportConsent::Ask );
    service.shutdown();
    settings.shutdown();
}

/**
 * @brief [CrashBundleTest] 자식 프로세스가 진짜로 죽는다 — 빵부스러기 · 빌드 id 를 올린 뒤 접근 위반(`CrashBundleTest.RealCrashLeavesABundle` 이 띄운다)
 */
SW_TEST_CASE( CrashBundleTest, ChildCrashesWithBreadcrumbs )
{
    const utf8* pFolder = std::getenv( "SW_CRASH_BUNDLE_CHILD_FOLDER" );
    if ( pFolder == nullptr )
        SW_TEST_SKIP( "crash child only — RealCrashLeavesABundle launches it" );
    setCrashReportFolder( pFolder );
#if defined( SW_PLATFORM_WINDOWS )
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );
#endif
    CrashHandler::setContextValue( "RHI", "BundleTestRhi" );
    CrashHandler::addBreadcrumb( "0.500 session.start" );
    CrashHandler::addBreadcrumb( "9.250 progression.waveReached wave=4 kills=31" );
    CrashHandler::crashForTest( CrashTestKind::AccessViolation );
    SW_EXPECT_TRUE_MSG( false, "crashForTest returned instead of crashing" );
}

/**
 * @brief [CrashBundleTest] 진짜 크래시 → 다음 실행의 묶음 — 자식이 남긴 덤프(Windows) · 컨텍스트(빌드 id · 백엔드) · 스택 · 빵부스러기가 묶음에 든다
 */
SW_TEST_CASE( CrashBundleTest, RealCrashLeavesABundle )
{
#if defined( SW_SANITIZER_ADDRESS ) || defined( SW_SANITIZER_THREAD )
    SW_TEST_SKIP( "AddressSanitizer owns the fatal signals — the crash handler does not write the report" );
#endif
    const string                         crashFolder      = test::makeTempDirectory( "ChildLogs" );
    const string                         reportsFolder    = test::makeTempDirectory( "CrashReports" );
    const test::ChildEnvironmentVariable arrEnvironment[] = {
        { "SW_CRASH_BUNDLE_CHILD_FOLDER", crashFolder }
    };
    const test::ChildRunResult child = test::runThisExecutableAsChild( "CrashBundleTest.ChildCrashesWithBreadcrumbs", arrEnvironment, 30 );
    SW_ASSERT_TRUE( child._bLaunched );
    SW_ASSERT_TRUE_MSG( child._bTimedOut == false, child.getOutputTail().c_str() );
    SW_EXPECT_TRUE( child._exitCode != 0 );

    CrashReportService service;
    service.initialize( crashFolder, reportsFolder, CrashHandler::getSessionId() );
    SW_ASSERT_EQUAL( 1u, service.collectNewCrashes() );
    vector<CrashReportSummary> listReport;
    service.collectReports( listReport );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( listReport.size() ) );
    const CrashReportSummary& report = listReport[0];
    SW_EXPECT_TRUE( report._sessionId != CrashHandler::getSessionId() );
    SW_EXPECT_FALSE( report._reason.empty() );
#if defined( SW_PLATFORM_WINDOWS )
    // 모든 구성이 /DEBUG 로 링크된다 — 부트스트랩이 올린 빌드 id 가 컨텍스트에 있다.
    SW_EXPECT_FALSE( report._buildId.empty() );
    SW_EXPECT_TRUE( FileUtil::getFileSize( FileUtil::joinPath( report._folder, "crash.dmp" ) ) > 0u );
#endif
    string breadcrumbs;
    SW_ASSERT_TRUE( FileUtil::readTextFile( FileUtil::joinPath( report._folder, "crash.breadcrumbs.txt" ), breadcrumbs ) );
    SW_EXPECT_TRUE_MSG( breadcrumbs.find( "progression.waveReached wave=4 kills=31" ) != string::npos, breadcrumbs.c_str() );
    string context;
    SW_ASSERT_TRUE( FileUtil::readTextFile( FileUtil::joinPath( report._folder, "crash.txt" ), context ) );
    SW_EXPECT_TRUE_MSG( context.find( "BundleTestRhi" ) != string::npos, context.c_str() );
    SW_EXPECT_TRUE( FileUtil::exists( FileUtil::joinPath( report._folder, "crash.stack.txt" ) ) );
}
