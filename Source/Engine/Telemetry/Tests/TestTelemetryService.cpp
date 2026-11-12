// TelemetryService — 스풀 쓰기 · 올리기가 부른 스레드 밖(스풀 워커)에서, 서비스 잠금 없이 돌고 실패에 물러서는지(감사 N-6).
#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetryService.h"
#include "Engine/Telemetry/TelemetryUploader.h"

#include <chrono>
#include <condition_variable>
#include <thread>

using namespace sw;

namespace
{
    // 빵부스러기 0 — 크래시 처리기(엔진 서브시스템)를 부르지 않는다. flushSeconds 0 은 update 마다 스풀 워커에 넘긴다.
    constexpr const utf8* kSchemaText = "<TelemetrySchema version=\"1\"><Pipeline batchEvents=\"64\" flushSeconds=\"0\" breadcrumbs=\"0\" uploadRetrySeconds=\"10\" uploadRetryMaxSeconds=\"100\"/>"
                                        "<Event id=\"test.ping\"/></TelemetrySchema>";
    constexpr auto        kWaitLimit  = std::chrono::seconds( 10 ); ///< 스풀 워커가 이만큼 안에 업로더에 닿지 않으면 실패다(걸려 멈추지 않게)

    /** @brief 스풀 폴더에 지난 실행이 남긴 닫힌 파일 하나를 두고 선 서비스입니다. */
    struct TelemetryServiceTest
    {
        TelemetryService _service;
        string           _folder;
        string           _leftoverFile;

        TelemetryServiceTest()
            : _service{}
            , _folder{ test::makeScratchPath( "telemetry" ) }
            , _leftoverFile{}
        {
            (void)FileUtil::ensureDirectoryExists( _folder );
            _leftoverFile = FileUtil::joinPath( _folder, "telemetry_old_0000.jsonl" );
            (void)FileUtil::writeTextFile( _leftoverFile, "{\"type\":\"context\"}\n{\"type\":\"event\"}\n" );
            SW_EXPECT_TRUE( _service.loadSchemaText( kSchemaText, "test.telemetry.xml" ) );
            TelemetryContext context;
            context._sessionID = "now";
            _service.initialize( _folder, context );
            _service.setConsent( true );
        }
    };

    /** @brief 문이 열릴 때까지 `upload` 안에서 막히는 업로더입니다. 부른 스레드와 받은 묶음을 기억합니다. */
    class GatedTelemetryUploader final : public ITelemetryUploader
    {
    public:
        GatedTelemetryUploader()
            : _mutex{}
            , _changed{}
            , _listBatch{}
            , _uploadThread{}
            , _result{ TelemetryUploadResult::Sent }
            , _bEntered{ SW_FALSE }
            , _bOpen{ SW_FALSE }
        {
        }

        TelemetryUploadResult upload( const TelemetryUploadBatch& batch ) override
        {
            UniqueLock<mutex> lock{ _mutex };
            _listBatch.push_back( batch );
            _uploadThread = std::this_thread::get_id();
            _bEntered     = SW_TRUE;
            _changed.notify_all();
            while ( _bOpen == SW_FALSE )
            {
                _changed.wait( lock );
            }
            return _result;
        }

        [[nodiscard]] bool waitUntilEntered()
        {
            UniqueLock<mutex> lock{ _mutex };
            return _changed.wait_for( lock, kWaitLimit, [this]
            { return _bEntered == SW_TRUE; } );
        }

        void open()
        {
            {
                ScopedLock<mutex> lock{ _mutex };
                _bOpen = SW_TRUE;
            }
            _changed.notify_all();
        }

        void setResult( TelemetryUploadResult result )
        {
            ScopedLock<mutex> lock{ _mutex };
            _result = result;
        }

        uint32 getCallCount() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return static_cast<uint32>( _listBatch.size() );
        }

        TelemetryUploadBatch getBatch( uint32 index ) const
        {
            ScopedLock<mutex> lock{ _mutex };
            return index < _listBatch.size() ? _listBatch[index] : TelemetryUploadBatch{};
        }

        std::thread::id getUploadThread() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _uploadThread;
        }

    private:
        mutable mutex                _mutex;
        std::condition_variable_any  _changed;
        vector<TelemetryUploadBatch> _listBatch;
        std::thread::id              _uploadThread;
        TelemetryUploadResult        _result;
        uint8                        _bEntered : 1;
        uint8                        _bOpen    : 1;
    };
} // namespace

/**
 * @brief [TelemetryServiceTest] 올리기가 끝점에서 막혀도 기록 · 시계 · 조회는 돌아온다 — 올리기는 스풀 워커 스레드에서 서비스 잠금 없이 돈다.
 *        업로더를 떼면 남은 묶음을 그 업로더로 보낸 뒤에야 돌아오고, 그 뒤 올리기는 기본 업로더로 간다.
 */
SW_TEST_CASE( TelemetryServiceTest, UploadDoesNotBlockRecordOrFrame )
{
    TelemetryServiceTest   fixture;
    GatedTelemetryUploader uploader;
    fixture._service.setUploader( uploader );

    SW_EXPECT_TRUE( fixture._service.record( TelemetryEvent{ hashed_string( "test.ping" ) } ) == TelemetryRecordResult::Recorded );
    fixture._service.update( 0.5f ); // 줄을 쓰고 지난 실행의 닫힌 파일을 올린다 — 업로더에서 막힌다
    SW_REQUIRE_TRUE( uploader.waitUntilEntered() );
    SW_EXPECT_TRUE( uploader.getUploadThread() != std::this_thread::get_id() );

    // 올리기가 막힌 동안 게임 스레드의 일은 모두 돌아온다(전에는 같은 잠금에서 HTTP 시한만큼 기다렸다).
    SW_EXPECT_TRUE( fixture._service.record( TelemetryEvent{ hashed_string( "test.ping" ) } ) == TelemetryRecordResult::Recorded );
    fixture._service.update( 0.5f );
    fixture._service.recordFrame( "scene", 0.016f );
    SW_EXPECT_EQUAL( uint32{ 2 }, fixture._service.getStats()._recorded );
    SW_EXPECT_EQUAL( uint32{ 0 }, fixture._service.getStats()._uploadedFiles );

    uploader.open();
    fixture._service.releaseUploader( uploader ); // 지금 파일을 닫아 이 업로더로 보낸 뒤 돌아온다
    SW_EXPECT_EQUAL( uint32{ 2 }, uploader.getCallCount() );
    SW_EXPECT_EQUAL( string( "telemetry_old_0000.jsonl" ), FileUtil::getFileNamePart( uploader.getBatch( 0 )._filePath ) ); // 지난 실행의 파일부터(오래된 것부터)
    SW_EXPECT_EQUAL( uint32{ 2 }, uploader.getBatch( 1 )._eventCount );
    SW_EXPECT_EQUAL( uint32{ 2 }, fixture._service.getStats()._uploadedFiles );
    SW_EXPECT_FALSE( FileUtil::exists( fixture._leftoverFile ) );
    vector<string> listSpool;
    fixture._service.collectSpoolFiles( listSpool );
    SW_EXPECT_TRUE( listSpool.empty() );

    // 뗀 뒤에는 기본 업로더(보내지 않는다)로 간다 — 뗀 업로더는 다시 불리지 않는다.
    SW_EXPECT_TRUE( fixture._service.record( TelemetryEvent{ hashed_string( "test.ping" ) } ) == TelemetryRecordResult::Recorded );
    fixture._service.update( 0.5f );
    fixture._service.flush();
    SW_EXPECT_EQUAL( uint32{ 2 }, uploader.getCallCount() );
    fixture._service.shutdown();
}

/**
 * @brief [TelemetryServiceTest] 올리기가 실패하면 세션 시계로 지수 물러서기만큼 다시 보내지 않고, 지나면 다시 보내 성공하면 파일을 지운다.
 */
SW_TEST_CASE( TelemetryServiceTest, FailedUploadBacksOff )
{
    TelemetryServiceTest   fixture;
    GatedTelemetryUploader uploader;
    uploader.open();
    uploader.setResult( TelemetryUploadResult::Failed );
    fixture._service.setUploader( uploader );

    fixture._service.flush(); // 첫 실패 — 0 ~ 10 초 물러선다(시각 0 에 한 번만 넘긴다 — 첫 지터가 0 이어도 다시 보내지 않는다)
    SW_EXPECT_EQUAL( uint32{ 1 }, uploader.getCallCount() );

    fixture._service.update( 100.0f ); // 첫 물러서기를 넘는다 — 둘째 실패, 5 ~ 10 초 물러선다
    fixture._service.flush();
    SW_EXPECT_EQUAL( uint32{ 2 }, uploader.getCallCount() );

    fixture._service.update( 1.0f ); // 아직 물러서는 중 — 보내지 않는다
    fixture._service.flush();
    SW_EXPECT_EQUAL( uint32{ 2 }, uploader.getCallCount() );
    SW_EXPECT_TRUE( FileUtil::exists( fixture._leftoverFile ) );

    uploader.setResult( TelemetryUploadResult::Sent );
    fixture._service.update( 10.0f ); // 물러서기가 끝났다 — 다시 보내 받아들여진다
    fixture._service.flush();
    SW_EXPECT_EQUAL( uint32{ 3 }, uploader.getCallCount() );
    SW_EXPECT_EQUAL( uint32{ 1 }, fixture._service.getStats()._uploadedFiles );
    SW_EXPECT_FALSE( FileUtil::exists( fixture._leftoverFile ) );
    fixture._service.releaseUploader( uploader );
    fixture._service.shutdown();
}

/**
 * @brief [TelemetryServiceTest] 동의를 거두면 앞서 넘긴 쓰기 뒤에 스풀 파일이 모두 지워진다(지우기도 스풀 워커가 순서대로 한다).
 */
SW_TEST_CASE( TelemetryServiceTest, WithdrawnConsentPurgesAfterQueuedWrites )
{
    TelemetryServiceTest fixture;
    SW_EXPECT_TRUE( fixture._service.record( TelemetryEvent{ hashed_string( "test.ping" ) } ) == TelemetryRecordResult::Recorded );
    fixture._service.flush();
    fixture._service.setConsent( false );
    fixture._service.flush(); // 동의가 없어 넘기지 않고 지우기까지 기다린다
    vector<string> listSpool;
    fixture._service.collectSpoolFiles( listSpool );
    SW_EXPECT_TRUE( listSpool.empty() );
    SW_EXPECT_FALSE( FileUtil::exists( fixture._leftoverFile ) );
    SW_EXPECT_EQUAL( uint32{ 2 }, fixture._service.getStats()._purgedFiles ); // 지난 실행의 파일 + 방금 쓴 파일
    fixture._service.shutdown();
}
