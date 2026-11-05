#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Telemetry/CrashReportService.h"
#include "Engine/Telemetry/CrashReportUploader.h"

using namespace sw;

namespace
{
    /** @brief 크래시 폴더 · 묶음 폴더를 케이스 자리에 세운 서비스입니다(지금 세션은 "current"). */
    struct CrashReportServiceTest
    {
        CrashReportService _service;
        string             _crashFolder;
        string             _reportsFolder;

        CrashReportServiceTest()
            : _service{}
            , _crashFolder{ test::makeScratchPath( "crash" ) }
            , _reportsFolder{ test::makeScratchPath( "reports" ) }
        {
            (void)FileUtil::ensureDirectoryExists( _crashFolder );
            _service.initialize( _crashFolder, _reportsFolder, "current" );
        }

        /** @brief 지난 실행 @p sessionID 가 남겼을 파일(컨텍스트 · 덤프 · 빵부스러기 · 그 세션의 로그)을 놓습니다. */
        void placeCrash( string_view sessionID ) const
        {
            const string session( sessionID );
            (void)FileUtil::writeTextFile( FileUtil::joinPath( _crashFolder, "crash_" + session + ".txt" ), "reason : boom\nBuildID : b1\n" );
            (void)FileUtil::writeTextFile( FileUtil::joinPath( _crashFolder, "crash_" + session + ".dmp" ), "dump" );
            (void)FileUtil::writeTextFile( FileUtil::joinPath( _crashFolder, "crash_" + session + ".breadcrumbs.txt" ), "one\ntwo\n" );
            (void)FileUtil::writeTextFile( FileUtil::joinPath( _crashFolder, "App_" + session + ".txt" ), "session log" );
        }

        [[nodiscard]] string makeBundleFile( string_view sessionID, string_view fileName ) const
        {
            return FileUtil::joinPath( FileUtil::joinPath( _reportsFolder, "crash_" + string( sessionID ) ), fileName );
        }

        /** @brief 묶음 하나뿐일 때 그 요약입니다. */
        [[nodiscard]] CrashReportSummary readOnlyReport() const
        {
            vector<CrashReportSummary> listReport;
            _service.collectReports( listReport );
            return listReport.size() == 1 ? listReport[0] : CrashReportSummary{};
        }
    };

    /** @brief 정해 둔 결과를 돌려주고 받은 묶음을 기억하는 업로더입니다. */
    class FakeCrashReportUploader final : public ICrashReportUploader
    {
    public:
        vector<CrashReportUploadBundle> _listBundle;
        CrashReportUploadResult         _result;

        explicit FakeCrashReportUploader( CrashReportUploadResult result )
            : _listBundle{}
            , _result{ result }
        {
        }

        CrashReportUploadResult upload( const CrashReportUploadBundle& bundle ) override
        {
            _listBundle.push_back( bundle );
            return _result;
        }
    };
} // namespace

// 지난 세션의 파일이 묶음으로 옮겨 가고, 지금 세션은 건드리지 않으며, 다시 묶지 않는다.
SW_TEST_CASE_F( CrashReportServiceTest, CollectMovesCrashFilesAndSkipsTheCurrentSession )
{
    placeCrash( "old" );
    placeCrash( "current" );

    SW_REQUIRE_EQUAL( _service.collectNewCrashes(), 1u );

    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::joinPath( _crashFolder, "crash_old.txt" ) ) );
    SW_EXPECT_FALSE( FileUtil::exists( FileUtil::joinPath( _crashFolder, "crash_old.dmp" ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( FileUtil::joinPath( _crashFolder, "crash_current.txt" ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( FileUtil::joinPath( _crashFolder, "App_old.txt" ) ) ); // 로그는 복사다 - 로그 폴더의 것은 로그 정리가 맡는다
    SW_EXPECT_TRUE( FileUtil::exists( makeBundleFile( "old", "crash.dmp" ) ) );
    string log;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( makeBundleFile( "old", "last.log" ), log ) );
    SW_EXPECT_EQUAL( log, string( "session log" ) );

    const CrashReportSummary report = readOnlyReport();
    SW_EXPECT_EQUAL( report._sessionID, string( "old" ) );
    SW_EXPECT_EQUAL( report._reason, string( "boom" ) );
    SW_EXPECT_EQUAL( report._buildID, string( "b1" ) );
    SW_EXPECT_TRUE( report._state == CrashReportState::Local ); // 기본은 기계 안에만
    SW_EXPECT_EQUAL( _service.countPendingUploads(), 0u );

    SW_EXPECT_EQUAL( _service.collectNewCrashes(), 0u );
}

// 물은 묶음에는 한 번만 답할 수 있다.
SW_TEST_CASE_F( CrashReportServiceTest, DecideAnswersOnlyAWaitingBundle )
{
    _service.setConsent( CrashReportConsent::Ask );
    placeCrash( "declined" );
    SW_REQUIRE_EQUAL( _service.collectNewCrashes(), 1u );
    SW_EXPECT_TRUE( readOnlyReport()._state == CrashReportState::AwaitingDecision );

    SW_EXPECT_TRUE( _service.decide( "declined", false ) );
    SW_EXPECT_TRUE( readOnlyReport()._state == CrashReportState::Declined );
    SW_EXPECT_FALSE( _service.decide( "declined", true ) );
    SW_EXPECT_TRUE( readOnlyReport()._state == CrashReportState::Declined );
}

// 철회는 보낼 줄의 묶음을 기계 안으로 돌리고, 그렇게 돌아온 묶음은 `send` 로 바꿔도 소급해 보내지 않는다.
SW_TEST_CASE_F( CrashReportServiceTest, ConsentChangeAppliesToExistingBundles )
{
    _service.setConsent( CrashReportConsent::Ask );
    placeCrash( "old" );
    SW_REQUIRE_EQUAL( _service.collectNewCrashes(), 1u );

    _service.setConsent( CrashReportConsent::Send );
    SW_EXPECT_TRUE( readOnlyReport()._state == CrashReportState::Queued );
    SW_EXPECT_EQUAL( _service.countPendingUploads(), 1u );

    _service.setConsent( CrashReportConsent::Local );
    SW_EXPECT_TRUE( readOnlyReport()._state == CrashReportState::Local );

    _service.setConsent( CrashReportConsent::Send );
    SW_EXPECT_TRUE( readOnlyReport()._state == CrashReportState::Local );
    SW_EXPECT_EQUAL( _service.countPendingUploads(), 0u );
}

// 받는 쪽이 받으면 큰 덤프만 지우고 "무엇을 보냈나" 는 남긴다. 다시 올리지 않는다.
SW_TEST_CASE_F( CrashReportServiceTest, ReporterSendsQueuedBundleOnce )
{
    _service.setConsent( CrashReportConsent::Send );
    placeCrash( "old" );
    SW_REQUIRE_EQUAL( _service.collectNewCrashes(), 1u );

    FakeCrashReportUploader uploader( CrashReportUploadResult::Sent );
    SW_EXPECT_EQUAL( CrashReportService::runReporter( _reportsFolder, uploader ), 1u );

    SW_REQUIRE_EQUAL( uploader._listBundle.size(), size_t{ 1 } );
    SW_EXPECT_EQUAL( uploader._listBundle[0]._sessionID, string( "old" ) );
    SW_EXPECT_EQUAL( uploader._listBundle[0]._listFilePath.size(), size_t{ 4 } ); // crash.txt · crash.dmp · crash.breadcrumbs.txt · last.log
    SW_EXPECT_TRUE( readOnlyReport()._state == CrashReportState::Sent );
    SW_EXPECT_FALSE( FileUtil::exists( makeBundleFile( "old", "crash.dmp" ) ) );
    SW_EXPECT_TRUE( FileUtil::exists( makeBundleFile( "old", "crash.txt" ) ) );

    SW_EXPECT_EQUAL( CrashReportService::runReporter( _reportsFolder, uploader ), 0u );
    SW_EXPECT_EQUAL( uploader._listBundle.size(), size_t{ 1 } );
}

// 실패는 시도 수만 올리고, 상한에 닿으면 더 올리지 않는다.
SW_TEST_CASE_F( CrashReportServiceTest, ReporterStopsAtTheAttemptLimit )
{
    _service.setConsent( CrashReportConsent::Send );
    placeCrash( "old" );
    SW_REQUIRE_EQUAL( _service.collectNewCrashes(), 1u );

    FakeCrashReportUploader uploader( CrashReportUploadResult::Failed );
    for ( uint32 attempt = 0; attempt < CrashReportService::kMaxAttempts + 2; ++attempt )
    {
        SW_EXPECT_EQUAL( CrashReportService::runReporter( _reportsFolder, uploader ), 0u );
    }

    SW_EXPECT_EQUAL( uploader._listBundle.size(), size_t{ CrashReportService::kMaxAttempts } );
    SW_EXPECT_EQUAL( readOnlyReport()._attempts, CrashReportService::kMaxAttempts );
    SW_EXPECT_TRUE( readOnlyReport()._state == CrashReportState::Queued );
    SW_EXPECT_TRUE( FileUtil::exists( makeBundleFile( "old", "crash.dmp" ) ) );
    SW_EXPECT_EQUAL( _service.countPendingUploads(), 0u );
}

// 묶음은 상한까지만 남는다.
SW_TEST_CASE_F( CrashReportServiceTest, ReportCapRemovesTheExtraBundles )
{
    const uint32 count = CrashReportService::kMaxReports + 2;
    for ( uint32 index = 0; index < count; ++index )
    {
        placeCrash( "s" + to_string( index ) );
    }

    SW_EXPECT_EQUAL( _service.collectNewCrashes(), count );

    vector<CrashReportSummary> listReport;
    _service.collectReports( listReport );
    SW_EXPECT_EQUAL( static_cast<uint32>( listReport.size() ), CrashReportService::kMaxReports );
}

// 다른 프로세스의 보관 개수 정리가 목록을 읽은 뒤에 묶음을 지울 수 있다. 사라진 묶음은 오류 로그 없이 "없음" 이다(로그가 나면 이 테스트가 진다).
SW_TEST_CASE_F( CrashReportServiceTest, BundleThatVanishedIsNotAnError )
{
    SW_REQUIRE_TRUE( FileUtil::ensureDirectoryExists( FileUtil::joinPath( _reportsFolder, "crash_gone" ) ) );

    vector<CrashReportSummary> listReport;
    _service.collectReports( listReport );
    SW_EXPECT_TRUE( listReport.empty() );
    SW_EXPECT_FALSE( _service.decide( "gone", true ) );
    SW_EXPECT_FALSE( _service.decide( "neverThere", true ) );

    FakeCrashReportUploader uploader( CrashReportUploadResult::Sent );
    SW_EXPECT_EQUAL( CrashReportService::runReporter( _reportsFolder, uploader ), 0u );
}

SW_TEST_CASE( CrashReportServiceTest, ConsentTextParses )
{
    CrashReportConsent consent = CrashReportConsent::Local;
    SW_EXPECT_TRUE( parseCrashReportConsent( "send", consent ) );
    SW_EXPECT_TRUE( consent == CrashReportConsent::Send );
    SW_EXPECT_TRUE( parseCrashReportConsent( toString( CrashReportConsent::Ask ), consent ) );
    SW_EXPECT_TRUE( consent == CrashReportConsent::Ask );
    SW_EXPECT_FALSE( parseCrashReportConsent( "yes", consent ) );
}
