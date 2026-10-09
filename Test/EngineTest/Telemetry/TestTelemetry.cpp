#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Serialization/Json/JsonDocument.h"
#include "Engine/Telemetry/HttpClient.h"
#include "Engine/Telemetry/TelemetryEvent.h"
#include "Engine/Telemetry/TelemetryService.h"
#include "Engine/Telemetry/TelemetryUploader.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "TestFramework/TestFramework.h"

// 텔레메트리 — 스키마 검사, 동의 문(꺼지면 모으지도 쓰지도 보내지도 않고, 거두면 지운다), 스키마 대조, 묶음 · 회전 · 상한, 올리기(가짜 업로더 ·
// HTTP 창구), 결정적 표본, 장면별 프레임 시간 요약, 사용자 설정과 동의 잇기. 실제 네트워크에는 닿지 않는다(가짜 창구만).

using namespace sw;

namespace
{
    struct TelemetryTestInternal
    {
        static constexpr const utf8* kSchemaXml = R"(
<TelemetrySchema version="2">
  <Pipeline batchEvents="4" flushSeconds="10" maxFileBytes="1024" maxFiles="4" maxTotalBytes="100000" sessionSample="1" breadcrumbs="5"/>
  <Event id="session.start" category="session"/>
  <Event id="session.end" category="session"><Field name="seconds" type="float" required="true"/></Event>
  <Event id="perf.sceneSummary" category="perf">
    <Field name="scene" type="string" required="true"/><Field name="frames" type="int" required="true"/><Field name="seconds" type="float"/>
    <Field name="avgMs" type="float"/><Field name="p50Ms" type="float" required="true"/><Field name="p99Ms" type="float" required="true"/><Field name="maxMs" type="float"/>
  </Event>
  <Event id="game.kill" category="progression">
    <Field name="weapon" type="string" required="true"/><Field name="distance" type="float"/><Field name="headshot" type="bool"/><Field name="combo" type="int"/>
  </Event>
  <Event id="game.rare" category="progression" sample="0.25"><Field name="index" type="int"/></Event>
</TelemetrySchema>
)";

        /** @brief 받은 묶음을 적고 정한 결과를 돌려주는 가짜 업로더입니다. */
        class RecordingUploader final : public ITelemetryUploader
        {
        public:
            TelemetryUploadResult upload( const TelemetryUploadBatch& batch ) override
            {
                _listBatch.push_back( batch );
                return _result;
            }

            vector<TelemetryUploadBatch> _listBatch{};
            TelemetryUploadResult        _result{ TelemetryUploadResult::Sent };
        };

        /** @brief 요청을 적고 정한 상태를 돌려주는 가짜 HTTP 창구입니다(네트워크 없음). */
        class RecordingHttpClient final : public IHttpClient
        {
        public:
            HttpResponse send( const HttpRequest& request ) override
            {
                _listRequest.push_back( request );
                HttpResponse response;
                response._status = _status;
                return response;
            }

            vector<HttpRequest> _listRequest{};
            int32               _status{ 200 };
        };

        static TelemetryContext makeContext( const utf8* pSession = "testsession01" )
        {
            TelemetryContext context;
            context._sessionId   = pSession;
            context._buildConfig = "Debug";
            context._platform    = "Windows";
            context._buildId     = "0123ABCD1";
            context._game        = "probe";
            return context;
        }

        static TelemetryEvent makeKill( const utf8* pWeapon, float32 distance )
        {
            TelemetryEvent event( "game.kill" );
            event.setString( "weapon", pWeapon ).setFloat( "distance", static_cast<float64>( distance ) ).setBool( "headshot", true ).setInt( "combo", 2 );
            return event;
        }

        static vector<string> splitLines( const string& text )
        {
            vector<string> listLine;
            size_t         lineStart = 0;
            while ( lineStart < text.size() )
            {
                size_t lineEnd = text.find( '\n', lineStart );
                if ( lineEnd == string::npos )
                    lineEnd = text.size();
                if ( lineEnd > lineStart )
                    listLine.push_back( text.substr( lineStart, lineEnd - lineStart ) );
                lineStart = lineEnd + 1;
            }
            return listLine;
        }

        static uint32 countSpoolFiles( const string& folder )
        {
            vector<string> listFile;
            if ( FileUtil::isDirectory( folder ) == false || FileUtil::collectFiles( folder, "", listFile, false ) == false )
                return 0;
            return static_cast<uint32>( listFile.size() );
        }

        static string readAllSpool( const string& folder )
        {
            vector<string> listFile;
            string         all;
            if ( FileUtil::isDirectory( folder ) == false || FileUtil::collectFiles( folder, "", listFile, false ) == false )
                return all;
            for ( const string& path : listFile )
            {
                string text;
                if ( FileUtil::readTextFile( path, text ) )
                    all += text;
            }
            return all;
        }
    };
} // namespace

/**
 * @brief [TelemetryTest] 스키마 — 모르는 원소 · 속성 · 타입, 예약 · 겹친 필드 이름, 겹친 사건, 범위 밖 표본은 파일째 거절하고 읽은 것은 그대로다
 */
SW_TEST_CASE( TelemetryTest, SchemaRejectsUnknownNamesAndKeepsWhatWasLoaded )
{
    TelemetrySchema schema;
    SW_ASSERT_TRUE( schema.loadFromXmlText( TelemetryTestInternal::kSchemaXml, "Telemetry" ) );
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( schema.getEvents().size() ) );
    SW_EXPECT_EQUAL( 2, schema.getVersion() );
    SW_EXPECT_EQUAL( 4u, schema.getSettings()._batchEvents );
    SW_EXPECT_NEAR_EQUAL( 0.25f, schema.findEvent( "game.rare" )->_sampleRate, 1.0e-6f );
    SW_EXPECT_TRUE( schema.findEvent( "game.kill" )->findField( "headshot" )->_type == TelemetryFieldType::Bool );

    struct BadCase
    {
        const utf8* _pXml;
        const utf8* _pMessage;
    };
    const BadCase arrBad[] = {
        {                          R"(<TelemetrySchema><Event id="a" color="red"/></TelemetrySchema>)", "unknown attribute 'color'"},
        {                                     R"(<TelemetrySchema><Metric id="a"/></TelemetrySchema>)",  "unknown element <Metric>"},
        {R"(<TelemetrySchema><Event id="a"><Field name="x" type="vector"/></Event></TelemetrySchema>)",     "unknown type 'vector'"},
        { R"(<TelemetrySchema><Event id="a"><Field name="seq" type="int"/></Event></TelemetrySchema>)",                  "reserved"},
        {                              R"(<TelemetrySchema><Event id="game.kill"/></TelemetrySchema>)",            "declared twice"},
        {                           R"(<TelemetrySchema><Event id="a" sample="2"/></TelemetrySchema>)",              "outside 0..1"},
        { R"(<TelemetrySchema><Event id="b"/><Pipeline batchEvents="9" speed="1"/></TelemetrySchema>)", "unknown attribute 'speed'"},
    };
    for ( const BadCase& bad : arrBad )
    {
        test::ScopedLogCollector logs;
        {
            SW_TEST_DEFENSIVE_SCOPE( "broken telemetry schema" );
            SW_EXPECT_FALSE( schema.loadFromXmlText( bad._pXml, "Broken" ) );
        }
        SW_EXPECT_TRUE_MSG( logs.countContaining( bad._pMessage ) >= 1, ( string( bad._pMessage ) + " not reported:" + logs.joined() ).c_str() );
    }
    // 거절한 파일의 사건(`a` · `b`) · 설정은 들어가지 않았다.
    SW_EXPECT_EQUAL( 5u, static_cast<uint32>( schema.getEvents().size() ) );
    SW_EXPECT_TRUE( schema.findEvent( "b" ) == nullptr );
    SW_EXPECT_EQUAL( 4u, schema.getSettings()._batchEvents );
    // 맞는 파일은 덧붙는다(게임 스키마).
    SW_EXPECT_TRUE( schema.loadFromXmlText( R"(<TelemetrySchema><Event id="game.win"><Field name="score" type="int"/></Event></TelemetrySchema>)", "Game" ) );
    SW_EXPECT_TRUE( schema.findEvent( "game.win" ) != nullptr );
}

/**
 * @brief [TelemetryTest] 동의 — 꺼져 있으면 모으지도 쓰지도 보내지도 않고, 켜면 쓰고 올리고, 거두면 묶음 · 스풀 파일을 지운다. 지난 실행의 파일도 동의 없이는 남지 않는다
 */
SW_TEST_CASE( TelemetryTest, ConsentGatesCollectionWritingAndUpload )
{
    using Internal                     = TelemetryTestInternal;
    const string                folder = test::makeTempDirectory( "spool" );
    Internal::RecordingUploader uploader;
    {
        TelemetryService telemetry;
        SW_ASSERT_TRUE( telemetry.loadSchemaText( Internal::kSchemaXml, "Telemetry" ) );
        telemetry.initialize( folder, Internal::makeContext() );
        telemetry.setUploader( &uploader );
        SW_EXPECT_FALSE( telemetry.hasConsent() );

        // 꺼짐(기본): 묶음 상한을 넘겨도 파일이 없고 업로더는 불리지 않는다. 빵부스러기는 기계 안에만 남는다.
        for ( int32 index = 0; index < 10; ++index )
        {
            SW_EXPECT_TRUE( telemetry.record( Internal::makeKill( "rifle", 10.0f ) ) == TelemetryRecordResult::NoConsent );
        }
        telemetry.recordFrame( "game/a.scene.xml", 0.016f );
        telemetry.update( 60.0f );
        telemetry.flush();
        SW_EXPECT_EQUAL( 0u, Internal::countSpoolFiles( folder ) );
        SW_EXPECT_TRUE( uploader._listBatch.empty() );
        SW_EXPECT_EQUAL( 10u, telemetry.getStats()._blockedByConsent );
        vector<string> listBreadcrumb;
        telemetry.collectBreadcrumbs( listBreadcrumb );
        SW_EXPECT_EQUAL( 5u, static_cast<uint32>( listBreadcrumb.size() ) );

        // 켬: session.start + 사건이 쓰인다.
        telemetry.setConsent( true );
        SW_EXPECT_TRUE( telemetry.record( Internal::makeKill( "rifle", 12.5f ) ) == TelemetryRecordResult::Recorded );
        telemetry.flush();
        SW_EXPECT_EQUAL( 1u, Internal::countSpoolFiles( folder ) );
        const string spool = Internal::readAllSpool( folder );
        SW_EXPECT_TRUE_MSG( spool.find( "\"session.start\"" ) != string::npos && spool.find( "\"rifle\"" ) != string::npos, spool.c_str() );

        // 거둠: 쓰지 않은 묶음과 파일이 사라진다.
        SW_EXPECT_TRUE( telemetry.record( Internal::makeKill( "shotgun", 3.0f ) ) == TelemetryRecordResult::Recorded );
        telemetry.setConsent( false );
        SW_EXPECT_EQUAL( 0u, Internal::countSpoolFiles( folder ) );
        SW_EXPECT_EQUAL( 1u, telemetry.getStats()._purgedFiles );
        telemetry.flush();
        SW_EXPECT_EQUAL( 0u, Internal::countSpoolFiles( folder ) );
        telemetry.shutdown();
        SW_EXPECT_EQUAL( 0u, Internal::countSpoolFiles( folder ) );
        SW_EXPECT_TRUE( uploader._listBatch.empty() );
    }

    // 지난 실행이 남긴 파일: 동의가 없으면 올리지 않고 지운다.
    SW_ASSERT_TRUE( FileUtil::writeTextFile( FileUtil::joinPath( folder, "telemetry_oldsession_0000.jsonl" ), "{\"type\":\"context\"}\n{\"type\":\"event\"}\n" ) );
    {
        TelemetryService telemetry;
        SW_ASSERT_TRUE( telemetry.loadSchemaText( Internal::kSchemaXml, "Telemetry" ) );
        telemetry.initialize( folder, Internal::makeContext( "nextsession" ) );
        telemetry.setUploader( &uploader );
        vector<string> listFile;
        telemetry.collectSpoolFiles( listFile );
        SW_EXPECT_EQUAL( 1u, static_cast<uint32>( listFile.size() ) );
        telemetry.setConsent( false );
        SW_EXPECT_EQUAL( 0u, Internal::countSpoolFiles( folder ) );
        SW_EXPECT_TRUE( uploader._listBatch.empty() );
    }
}

/**
 * @brief [TelemetryTest] 스키마 대조 — 모르는 사건 · 필드, 다른 타입, 빠진 필수 필드는 거절하고(정수는 실수 칸에 받는다), 서지 않은 서비스는 받지 않는다
 */
SW_TEST_CASE( TelemetryTest, RecordValidatesAgainstTheSchema )
{
    using Internal = TelemetryTestInternal;
    TelemetryService telemetry;
    SW_EXPECT_TRUE( telemetry.record( Internal::makeKill( "rifle", 1.0f ) ) == TelemetryRecordResult::NotInitialized );
    SW_ASSERT_TRUE( telemetry.loadSchemaText( Internal::kSchemaXml, "Telemetry" ) );
    telemetry.initialize( test::makeTempDirectory( "spool" ), Internal::makeContext() );
    telemetry.setConsent( true );
    test::ScopedLogCollector logs;
    {
        SW_TEST_DEFENSIVE_SCOPE( "telemetry events outside the schema" );
        SW_EXPECT_TRUE( telemetry.record( TelemetryEvent( "game.unknown" ) ) == TelemetryRecordResult::UnknownEvent );
        TelemetryEvent extraField = Internal::makeKill( "rifle", 1.0f );
        extraField.setInt( "ammo", 3 );
        SW_EXPECT_TRUE( telemetry.record( extraField ) == TelemetryRecordResult::InvalidField );
        TelemetryEvent wrongType( "game.kill" );
        wrongType.setString( "weapon", "rifle" ).setString( "combo", "two" );
        SW_EXPECT_TRUE( telemetry.record( wrongType ) == TelemetryRecordResult::InvalidField );
        TelemetryEvent missingRequired( "game.kill" );
        missingRequired.setFloat( "distance", 3.0 );
        SW_EXPECT_TRUE( telemetry.record( missingRequired ) == TelemetryRecordResult::InvalidField );
    }
    SW_EXPECT_TRUE( logs.countContaining( "'game.unknown' is not in the schema" ) == 1 );
    SW_EXPECT_TRUE( logs.countContaining( "misses required field 'weapon'" ) == 1 );
    TelemetryEvent intForFloat( "game.kill" );
    intForFloat.setString( "weapon", "pistol" ).setInt( "distance", 7 );
    SW_EXPECT_TRUE( telemetry.record( intForFloat ) == TelemetryRecordResult::Recorded );
    SW_EXPECT_EQUAL( 4u, telemetry.getStats()._rejected );
}

/**
 * @brief [TelemetryTest] 묶음 · 회전 · 상한 · 올리기 — 묶음 크기에서 쓰고, 파일 크기 상한에서 다음 번호로 넘기며, 닫힌 파일은 업로더가 받은 것만 지우고,
 *        보내지 못한 파일은 파일 수 상한에서 가장 오래된 것부터 버린다. 줄은 JSON 이고 첫 줄이 문맥이다
 */
SW_TEST_CASE( TelemetryTest, BatchesRotateAndRespectCaps )
{
    using Internal                     = TelemetryTestInternal;
    const string                folder = test::makeTempDirectory( "spool" );
    Internal::RecordingUploader uploader;
    uploader._result = TelemetryUploadResult::Kept;
    TelemetryService telemetry;
    SW_ASSERT_TRUE( telemetry.loadSchemaText( Internal::kSchemaXml, "Telemetry" ) );
    telemetry.initialize( folder, Internal::makeContext() );
    telemetry.setUploader( &uploader );
    telemetry.setConsent( true ); // session.start 가 묶음의 첫 줄

    // 묶음 4: 셋째 사건까지는 쓰이지 않고 넷째(session.start 포함)에서 쓰인다.
    SW_EXPECT_TRUE( telemetry.record( Internal::makeKill( "rifle", 1.0f ) ) == TelemetryRecordResult::Recorded );
    SW_EXPECT_TRUE( telemetry.record( Internal::makeKill( "rifle", 2.0f ) ) == TelemetryRecordResult::Recorded );
    SW_EXPECT_EQUAL( 0u, Internal::countSpoolFiles( folder ) );
    SW_EXPECT_TRUE( telemetry.record( Internal::makeKill( "rifle", 3.0f ) ) == TelemetryRecordResult::Recorded );
    SW_EXPECT_EQUAL( 1u, Internal::countSpoolFiles( folder ) );
    const vector<string> listLine = Internal::splitLines( Internal::readAllSpool( folder ) );
    SW_ASSERT_EQUAL( 5u, static_cast<uint32>( listLine.size() ) );
    JsonDocument contextLine;
    SW_ASSERT_TRUE( contextLine.parse( listLine[0], "context" ) );
    SW_EXPECT_TRUE( contextLine.getRoot().get( "type" ).asString() == "context" );
    SW_EXPECT_TRUE( contextLine.getRoot().get( "session" ).asString() == "testsession01" );
    SW_EXPECT_TRUE( contextLine.getRoot().get( "buildId" ).asString() == "0123ABCD1" );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( contextLine.getRoot().get( "schema" ).asInt() ) );
    for ( size_t lineIndex = 1; lineIndex < listLine.size(); ++lineIndex )
    {
        JsonDocument eventLine;
        SW_ASSERT_TRUE( eventLine.parse( listLine[lineIndex], "event" ) );
        SW_EXPECT_EQUAL( static_cast<int64>( lineIndex - 1 ), eventLine.getRoot().get( "seq" ).asInt() );
    }
    JsonDocument killLine;
    SW_ASSERT_TRUE( killLine.parse( listLine[4], "kill" ) );
    SW_EXPECT_TRUE( killLine.getRoot().get( "event" ).asString() == "game.kill" );
    SW_EXPECT_NEAR_EQUAL( 3.0, killLine.getRoot().get( "fields" ).get( "distance" ).asFloat(), 1.0e-6 );
    SW_EXPECT_TRUE( killLine.getRoot().get( "fields" ).get( "headshot" ).asBool() );

    // 파일 1 KB: 사건을 계속 넣으면 여러 파일로 회전하고, 업로더가 두는(Kept) 동안 파일 수 상한(4)에서 오래된 것부터 버린다.
    for ( int32 index = 0; index < 200; ++index )
    {
        (void)telemetry.record( Internal::makeKill( "minigun_with_a_long_weapon_name", static_cast<float32>( index ) ) );
    }
    telemetry.flush();
    const TelemetryStats stats = telemetry.getStats();
    SW_EXPECT_TRUE_MSG( stats._rotatedFiles >= 10, std::to_string( stats._rotatedFiles ).c_str() );
    SW_EXPECT_TRUE( stats._droppedFiles >= 6 );
    SW_EXPECT_TRUE( Internal::countSpoolFiles( folder ) <= 4u );
    vector<string> listFile;
    telemetry.collectSpoolFiles( listFile );
    for ( const string& path : listFile )
    {
        SW_EXPECT_TRUE_MSG( FileUtil::getFileSize( path ) <= 1024u, path.c_str() );
    }
    SW_EXPECT_FALSE( uploader._listBatch.empty() );
    SW_EXPECT_EQUAL( 0u, stats._uploadedFiles );

    // 받는 쪽이 받으면 닫힌 파일이 모두 지워진다 — 남는 것은 지금 쓰는 파일 하나.
    uploader._result = TelemetryUploadResult::Sent;
    uploader._listBatch.clear();
    telemetry.flush();
    SW_EXPECT_EQUAL( 1u, Internal::countSpoolFiles( folder ) );
    SW_ASSERT_FALSE( uploader._listBatch.empty() );
    const TelemetryUploadBatch& batch = uploader._listBatch.front();
    SW_EXPECT_TRUE( batch._sessionId == "testsession01" );
    SW_EXPECT_EQUAL( static_cast<uint32>( Internal::splitLines( batch._content ).size() ) - 1u, batch._eventCount );
    // 끝: 지금 파일도 닫아 올린다(session.end 포함).
    telemetry.shutdown();
    SW_EXPECT_EQUAL( 0u, Internal::countSpoolFiles( folder ) );
    SW_EXPECT_TRUE( uploader._listBatch.back()._content.find( "\"session.end\"" ) != string::npos );
}

/**
 * @brief [TelemetryTest] 표본 — 사건 표본(0.25)은 비율 근처이고 같은 세션이면 같은 사건을 고르며, 세션 표본 0 이면 아무것도 남지 않는다
 */
SW_TEST_CASE( TelemetryTest, SamplingIsDeterministicPerSession )
{
    using Internal     = TelemetryTestInternal;
    const auto runRare = [&]( const utf8* pSession, vector<int32>& outListKept ) -> uint32
    {
        TelemetryService telemetry;
        SW_EXPECT_TRUE( telemetry.loadSchemaText( Internal::kSchemaXml, "Telemetry" ) );
        telemetry.initialize( test::makeTempDirectory( pSession ), Internal::makeContext( pSession ) );
        telemetry.setConsent( true );
        outListKept.clear();
        for ( int32 index = 0; index < 2000; ++index )
        {
            TelemetryEvent event( "game.rare" );
            event.setInt( "index", index );
            if ( telemetry.record( event ) == TelemetryRecordResult::Recorded )
                outListKept.push_back( index );
        }
        return telemetry.getStats()._sampledOut;
    };
    vector<int32> listFirst;
    vector<int32> listSecond;
    vector<int32> listOther;
    const uint32  sampledOut = runRare( "sessionalpha", listFirst );
    (void)runRare( "sessionalpha", listSecond );
    (void)runRare( "sessionbravo", listOther );
    SW_EXPECT_TRUE_MSG( 400 <= listFirst.size() && listFirst.size() <= 600, std::to_string( listFirst.size() ).c_str() );
    SW_EXPECT_EQUAL( 2000u, static_cast<uint32>( listFirst.size() ) + sampledOut );
    SW_EXPECT_TRUE( listFirst == listSecond );
    SW_EXPECT_TRUE( listFirst != listOther );

    TelemetryService none;
    SW_ASSERT_TRUE( none.loadSchemaText( R"(<TelemetrySchema><Pipeline sessionSample="0"/><Event id="game.ping"/></TelemetrySchema>)", "NoSession" ) );
    none.initialize( test::makeTempDirectory( "none" ), Internal::makeContext() );
    none.setConsent( true );
    SW_EXPECT_FALSE( none.isSessionSampled() );
    SW_EXPECT_TRUE( none.record( TelemetryEvent( "game.ping" ) ) == TelemetryRecordResult::SampledOut );
}

/**
 * @brief [TelemetryTest] 프레임 시간 — 히스토그램 백분위가 0.05 ms 안이고, 장면이 바뀔 때 · 끝날 때 장면마다 요약(프레임 수 · p50 · p99 · 최대)이 남는다
 */
SW_TEST_CASE( TelemetryTest, SceneSummariesCarryFramePercentiles )
{
    using Internal = TelemetryTestInternal;
    TelemetryFrameHistogram histogram;
    for ( int32 index = 1; index <= 100; ++index )
    {
        histogram.add( static_cast<float32>( index ) ); // 1..100 ms
    }
    SW_EXPECT_NEAR_EQUAL( 50.0f, histogram.computePercentileMs( 0.5f ), 0.06f );
    SW_EXPECT_NEAR_EQUAL( 99.0f, histogram.computePercentileMs( 0.99f ), 0.06f );
    SW_EXPECT_NEAR_EQUAL( 100.0f, histogram.getMaxMs(), 1.0e-5f );
    histogram.add( 400.0f ); // 넘침 칸
    SW_EXPECT_NEAR_EQUAL( 400.0f, histogram.computePercentileMs( 1.0f ), 1.0e-4f );

    const string     folder = test::makeTempDirectory( "spool" );
    TelemetryService telemetry;
    SW_ASSERT_TRUE( telemetry.loadSchemaText( Internal::kSchemaXml, "Telemetry" ) );
    telemetry.initialize( folder, Internal::makeContext() );
    telemetry.setConsent( true );
    // 장면 A: 16 ms 99 프레임 + 100 ms 1 프레임(끊김). 장면 B: 8 ms 50 프레임.
    for ( int32 frame = 0; frame < 99; ++frame )
    {
        telemetry.recordFrame( "game/a.scene.xml", 0.016f );
    }
    telemetry.recordFrame( "game/a.scene.xml", 0.100f );
    for ( int32 frame = 0; frame < 50; ++frame )
    {
        telemetry.recordFrame( "game/b.scene.xml", 0.008f );
    }
    telemetry.shutdown();
    // 업로더가 없으니(기본 — 보내지 않는다) 닫힌 파일이 스풀에 남는다.
    vector<JsonDocument> listSummary;
    for ( const string& line : Internal::splitLines( Internal::readAllSpool( folder ) ) )
    {
        JsonDocument doc;
        SW_ASSERT_TRUE( doc.parse( line, "line" ) );
        if ( doc.getRoot().get( "event" ).asString() == "perf.sceneSummary" )
            listSummary.push_back( std::move( doc ) );
    }
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( listSummary.size() ) );
    const JsonValue first = listSummary[0].getRoot().get( "fields" );
    SW_EXPECT_TRUE( first.get( "scene" ).asString() == "game/a.scene.xml" );
    SW_EXPECT_EQUAL( 100, static_cast<int32>( first.get( "frames" ).asInt() ) );
    SW_EXPECT_NEAR_EQUAL( 16.05, first.get( "p50Ms" ).asFloat(), 0.06 );
    SW_EXPECT_NEAR_EQUAL( 16.05, first.get( "p99Ms" ).asFloat(), 0.06 );
    SW_EXPECT_NEAR_EQUAL( 100.0, first.get( "maxMs" ).asFloat(), 0.01 );
    const JsonValue second = listSummary[1].getRoot().get( "fields" );
    SW_EXPECT_TRUE( second.get( "scene" ).asString() == "game/b.scene.xml" );
    SW_EXPECT_EQUAL( 50, static_cast<int32>( second.get( "frames" ).asInt() ) );
    SW_EXPECT_NEAR_EQUAL( 8.05, second.get( "p50Ms" ).asFloat(), 0.06 );
}

/**
 * @brief [TelemetryTest] HTTP 업로더 — 파일 그대로를 POST 본문으로, 세션 · 사건 수 · 키를 헤더로 보내고 2xx 만 보낸 것으로 친다. 기본 창구는 보내지 않는다
 */
SW_TEST_CASE( TelemetryTest, HttpUploaderBuildsTheRequestAndNeverTouchesTheNetwork )
{
    TelemetryTestInternal::RecordingHttpClient client;
    HttpTelemetryUploader                      uploader( client, "https://telemetry.invalid/v1/events", "key-123" );
    TelemetryUploadBatch                       batch;
    batch._content    = "{\"type\":\"context\"}\n{\"type\":\"event\"}\n";
    batch._sessionId  = "s1";
    batch._eventCount = 1;
    SW_EXPECT_TRUE( uploader.upload( batch ) == TelemetryUploadResult::Sent );
    SW_ASSERT_EQUAL( 1u, static_cast<uint32>( client._listRequest.size() ) );
    const HttpRequest& request = client._listRequest[0];
    SW_EXPECT_TRUE( request._method == "POST" && request._url == "https://telemetry.invalid/v1/events" );
    SW_EXPECT_TRUE( request._body == batch._content );
    SW_EXPECT_TRUE( request.findHeader( "content-type" ) == "application/x-ndjson" );
    SW_EXPECT_TRUE( request.findHeader( "X-Telemetry-Session" ) == "s1" );
    SW_EXPECT_TRUE( request.findHeader( "X-Telemetry-Events" ) == "1" );
    SW_EXPECT_TRUE( request.findHeader( "X-Api-Key" ) == "key-123" );
    client._status = 503;
    SW_EXPECT_TRUE( uploader.upload( batch ) == TelemetryUploadResult::Failed );

    HttpTelemetryUploader offline( NullHttpClient::get(), "https://telemetry.invalid/v1/events", "" );
    SW_EXPECT_TRUE( offline.upload( batch ) == TelemetryUploadResult::Failed );
    SW_EXPECT_TRUE( NullTelemetryUploader::get().upload( batch ) == TelemetryUploadResult::Kept );
}

/**
 * @brief [TelemetryTest] 동의는 사용자 설정 `telemetry.enabled` 의 확정 값이다 — 보류만으로는 켜지지 않고, 적용하면 켜지고, 기본값으로 되돌리면 꺼진다
 */
SW_TEST_CASE( TelemetryTest, ConsentFollowsTheUserSetting )
{
    UserSettingsManager settings;
    settings.initialize( UserSettingsTargets{} );
    SW_ASSERT_TRUE( settings.loadSchemaFromXmlText( R"(<UserSettingsSchema version="1"><Category id="privacy"/>
<Setting id="telemetry.enabled" category="privacy" type="bool" default="false"/></UserSettingsSchema>)",
                                                    "privacy.settings.xml" ) );
    settings.reapplyAll();
    TelemetryService telemetry;
    SW_ASSERT_TRUE( telemetry.loadSchemaText( TelemetryTestInternal::kSchemaXml, "Telemetry" ) );
    telemetry.initialize( test::makeTempDirectory( "spool" ), TelemetryTestInternal::makeContext() );
    telemetry.bindConsentSetting( settings );
    SW_EXPECT_FALSE( telemetry.hasConsent() );

    SW_EXPECT_TRUE( settings.setPendingBoolValue( "telemetry.enabled", true ) == UserSettingSetResult::Accepted );
    SW_EXPECT_FALSE( telemetry.hasConsent() ); // 보류는 동의가 아니다
    (void)settings.applyPending();             // 적용 결과는 아래 동의 단언이 확인한다
    SW_EXPECT_TRUE( telemetry.hasConsent() );

    (void)settings.setPendingBoolValue( "telemetry.enabled", false );
    (void)settings.applyPending(); // 적용 결과는 아래 동의 단언이 확인한다
    SW_EXPECT_FALSE( telemetry.hasConsent() );
    telemetry.shutdown();
    settings.shutdown();
}
