// LiveOpsServiceTest — 라이브 운영 로직(메모리 저장소): 쓰기 · 지우기와 감사, 다시 읽기, 열린 목록과 바뀜 알림, 시작 푸시(한 번 · 늦게 본 창은 빼고), 대상(지역), '@' 값 풀기, 긴급 스위치, JSON 입구.
// 규칙 표는 LiveEventRulesTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Runtime/LiveOpsClient.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"
#include "GameFramework/Runtime/Online/Audit/Server/ServiceAuditLog.h"
#include "GameFramework/Runtime/Online/Config/Server/RemoteConfig.h"
#include "GameFramework/Runtime/Online/Store/Server/MemoryServiceStore.h"

using namespace sw;

namespace
{
    constexpr int64     kStartMs = 1700000000000ll;
    constexpr int64     kEndMs   = kStartMs + 3600000;
    constexpr AccountID kGM      = 7;
    constexpr AccountID kPlayer  = 11;

    struct LiveOpsFixture
    {
        MemoryServiceDatabase _database;
        MemoryServiceStore    _store;
        RemoteConfig          _config;
        LiveOpsService        _service;
        int64                 _auditTime{ 0 };

        explicit LiveOpsFixture( bool bConfig = true )
            : _database{}
            , _store{ &_database }
            , _config{}
            , _service{}
        {
            LiveOpsDependencies dependencies;
            dependencies._pStore        = &_store;
            dependencies._pRemoteConfig = bConfig ? &_config : nullptr;
            _service.initialize( dependencies );
        }

        void pump()
        {
            while ( _store.pollCompletions() > 0 )
            {
            }
        }

        /** @brief 틱 하나를 돌리고 저장소 완료를 받습니다. */
        void tick( int64 nowMs )
        {
            _service.update( UnixMilliseconds{ nowMs } );
            pump();
        }

        LiveOpsServiceResult put( const LiveEventDef& definition, int64 nowMs = kStartMs )
        {
            _service.putEvent( definition, kGM, "memo", UnixMilliseconds{ nowMs }, 5 );
            pump();
            return takeResult();
        }

        LiveOpsServiceResult takeResult()
        {
            vector<LiveOpsCompletion> listCompletion;
            _service.takeCompletions( listCompletion );
            SW_EXPECT_EQUAL( size_t( 1 ), listCompletion.size() );
            return listCompletion.empty() ? LiveOpsServiceResult::fail( OnlineError::kUnavailable ) : listCompletion[0]._result;
        }

        vector<LiveEventState> active( string_view region, int64 nowMs, bool bClientOnly = true )
        {
            vector<LiveEventState> listEvent;
            _service.computeActiveEvents( kPlayer, region, 0, UnixMilliseconds{ nowMs }, bClientOnly, listEvent );
            return listEvent;
        }

        void setConfigText( string_view key, string_view text )
        {
            RemoteConfigValue value;
            value._type = RemoteConfigValueType::Text;
            value._text = string( text );
            submit( key, value );
        }

        void setKillSwitch( bool bEnabled )
        {
            RemoteConfigValue value;
            value._type               = RemoteConfigValueType::Flag;
            value._integer            = 1; // 켜진 플래그이고 대상 비율로 끈다
            value._rolloutBasisPoints = bEnabled ? 10000 : 0;
            submit( LiveOpsBus::kKillSwitchFlag, value );
        }

        void submit( string_view key, const RemoteConfigValue& value )
        {
            ServiceAuditEntry audit;
            audit._actor   = "gm.0000000000000001";
            audit._action  = "config.set";
            audit._subject = "config";
            audit._timeMs  = ++_auditTime; // 감사 키가 시각으로 갈린다
            _config.submitSet( _store, key, value, audit );
            pump();
        }
    };

    struct LiveOpsTestUtil
    {
        static LiveEventDef makeEvent( string_view eventID = "spring.sale" )
        {
            LiveEventDef definition;
            definition._eventID = string( eventID );
            definition._kind    = "shop_sale";
            definition._startMs = kStartMs;
            definition._endMs   = kEndMs;
            return definition;
        }

        static LiveEventParameter makeParameter( string_view key, string_view value ) { return LiveEventParameter{ string( key ), string( value ) }; }
    };
} // namespace

/** @brief [LiveOpsServiceTest] 쓰기: 다시 읽어 창 안에서만 보이고 감사 줄이 남는다. 지우면 사라지고, 없는 것을 지우면 NotFound, 틀린 정의는 저장소에 닿지 않는다 */
SW_TEST_CASE( LiveOpsServiceTest, PutReloadRemove )
{
    LiveOpsFixture rig;
    SW_EXPECT_TRUE( rig.put( LiveOpsTestUtil::makeEvent() ) == LiveOpsResult::Ok );
    SW_EXPECT_EQUAL( 1, rig._database.countRecords( ServiceAuditLog::getTable() ) );
    rig.tick( kStartMs );
    const vector<LiveEventState> listOpen = rig.active( "kr", kStartMs );
    SW_REQUIRE_EQUAL( size_t( 1 ), listOpen.size() );
    SW_EXPECT_EQUAL( string( "spring.sale" ), listOpen[0]._eventID );
    SW_EXPECT_EQUAL( kEndMs, listOpen[0]._windowEndMs );
    SW_EXPECT_TRUE( rig.active( "kr", kStartMs - 1 ).empty() );
    SW_EXPECT_TRUE( rig.active( "kr", kEndMs ).empty() ); // 반열림이다
    SW_EXPECT_TRUE( rig._service.isEventActive( "spring.sale", kPlayer, "kr", 0, UnixMilliseconds{ kStartMs } ) );
    SW_EXPECT_FALSE( rig._service.isEventActive( "none", kPlayer, "kr", 0, UnixMilliseconds{ kStartMs } ) );

    const uint64 hashBefore = rig._database.computeContentHash();
    LiveEventDef bad        = LiveOpsTestUtil::makeEvent();
    bad._endMs              = bad._startMs;
    SW_EXPECT_TRUE( rig.put( bad ).hasError( OnlineError::kInvalidRequest ) );
    SW_EXPECT_EQUAL( hashBefore, rig._database.computeContentHash() );

    rig._service.removeEvent( "none", kGM, "m", UnixMilliseconds{ kStartMs }, 6 );
    rig.pump();
    SW_EXPECT_TRUE( rig.takeResult() == LiveOpsResult::NotFound );
    rig._service.removeEvent( "Bad Id", kGM, "m", UnixMilliseconds{ kStartMs }, 6 );
    SW_EXPECT_TRUE( rig.takeResult().hasError( OnlineError::kInvalidRequest ) );
    rig._service.removeEvent( "spring.sale", kGM, "m", UnixMilliseconds{ kStartMs }, 6 );
    rig.pump();
    SW_EXPECT_TRUE( rig.takeResult() == LiveOpsResult::Ok );
    rig.tick( kStartMs );
    SW_EXPECT_TRUE( rig.active( "kr", kStartMs ).empty() );
}

/** @brief [LiveOpsServiceTest] 저장소 실패: 쓰기는 Unavailable 이고, 읽기 실패는 이전 목록을 둔 채 다음 주기에 다시 읽는다 */
SW_TEST_CASE( LiveOpsServiceTest, StoreFailuresKeepPreviousState )
{
    LiveOpsFixture rig;
    rig._database.armFault( ServiceStoreFault::RejectCommit );
    SW_EXPECT_TRUE( rig.put( LiveOpsTestUtil::makeEvent() ).hasError( OnlineError::kUnavailable ) );
    SW_EXPECT_TRUE( rig.put( LiveOpsTestUtil::makeEvent() ) == LiveOpsResult::Ok );
    rig.tick( kStartMs );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.active( "kr", kStartMs ).size() );

    rig._database.armFault( ServiceStoreFault::RejectRead );
    rig.tick( kStartMs + LiveOpsBus::kReloadPeriodMs );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.active( "kr", kStartMs ).size() );
    SW_EXPECT_EQUAL( 0, rig._service.getPendingCount() );
}

/** @brief [LiveOpsServiceTest] 바뀜 알림: 정의가 바뀌거나 창이 열리고 닫힐 때만 한 번 켜진다 — 다시 읽기만으로는 켜지지 않는다 */
SW_TEST_CASE( LiveOpsServiceTest, StateChangeFiresOnOpenAndClose )
{
    LiveOpsFixture rig;
    rig.tick( kStartMs - 2 );
    SW_EXPECT_FALSE( rig._service.takeStateChange() ); // 빈 상태는 알리지 않는다

    SW_EXPECT_TRUE( rig.put( LiveOpsTestUtil::makeEvent() ) == LiveOpsResult::Ok );
    rig.tick( kStartMs - 1 );
    SW_EXPECT_FALSE( rig._service.takeStateChange() ); // 아직 창 밖이다

    rig.tick( kStartMs );
    SW_EXPECT_TRUE( rig._service.takeStateChange() );
    SW_EXPECT_FALSE( rig._service.takeStateChange() );
    rig.tick( kStartMs + LiveOpsBus::kReloadPeriodMs );
    SW_EXPECT_FALSE( rig._service.takeStateChange() );

    rig.tick( kEndMs );
    SW_EXPECT_TRUE( rig._service.takeStateChange() );
}

/** @brief [LiveOpsServiceTest] 시작 푸시: 창이 열리면 한 번 나가고 지역을 싣는다. 푸시를 달지 않은 이벤트는 없고, 열린 지 오래된 창을 처음 보면(서버 재시작) 내지 않는다 */
SW_TEST_CASE( LiveOpsServiceTest, StartPushOncePerWindow )
{
    LiveOpsFixture rig;
    LiveEventDef   withPush = LiveOpsTestUtil::makeEvent( "with.push" );
    withPush._listParameter.push_back( LiveOpsTestUtil::makeParameter( LiveOpsPush::kTitleParameter, "push.title.key" ) );
    withPush._listRegion.push_back( "kr" );
    SW_EXPECT_TRUE( rig.put( withPush ) == LiveOpsResult::Ok );
    SW_EXPECT_TRUE( rig.put( LiveOpsTestUtil::makeEvent( "no.push" ) ) == LiveOpsResult::Ok );

    rig.tick( kStartMs );
    rig.tick( kStartMs + 1 );
    vector<LiveEventStart> listStart;
    rig._service.takeStartedEvents( listStart );
    SW_REQUIRE_EQUAL( size_t( 1 ), listStart.size() );
    SW_EXPECT_EQUAL( string( "with.push" ), listStart[0]._eventID );
    SW_EXPECT_EQUAL( string( "push.title.key" ), listStart[0]._message._titleKey );
    SW_EXPECT_EQUAL( kEndMs, listStart[0]._windowEndMs );
    SW_REQUIRE_EQUAL( size_t( 1 ), listStart[0]._listRegion.size() );

    LiveOpsFixture late;
    SW_EXPECT_TRUE( late.put( withPush ) == LiveOpsResult::Ok );
    late.tick( kStartMs + LiveOpsPush::kStartGraceMs + 1 );
    listStart.clear();
    late._service.takeStartedEvents( listStart );
    SW_EXPECT_TRUE( listStart.empty() );
}

/** @brief [LiveOpsServiceTest] 대상: 지역이 다르면 보이지 않고, 클라이언트에게 숨긴 이벤트는 서버 쪽 조회에서만 보인다 */
SW_TEST_CASE( LiveOpsServiceTest, AudienceFilters )
{
    LiveOpsFixture rig;
    LiveEventDef   regional = LiveOpsTestUtil::makeEvent( "regional" );
    regional._listRegion.push_back( "kr" );
    LiveEventDef hidden    = LiveOpsTestUtil::makeEvent( "hidden" );
    hidden._bClientVisible = SW_FALSE;
    SW_EXPECT_TRUE( rig.put( regional ) == LiveOpsResult::Ok );
    SW_EXPECT_TRUE( rig.put( hidden ) == LiveOpsResult::Ok );
    rig.tick( kStartMs );

    SW_EXPECT_EQUAL( size_t( 1 ), rig.active( "kr", kStartMs ).size() );
    SW_EXPECT_TRUE( rig.active( "eu", kStartMs ).empty() );
    SW_EXPECT_EQUAL( size_t( 2 ), rig.active( "kr", kStartMs, false ).size() );
}

/** @brief [LiveOpsServiceTest] '@' 값: 원격 설정의 글로 풀고, 없는 키는 경고하고 그대로 둔다. 일반 값은 그대로다 */
SW_TEST_CASE( LiveOpsServiceTest, ParametersResolveRemoteConfig )
{
    SW_EXPECT_LOG( "LiveOpsService.LiveOpsRemoteConfigKey" );
    LiveOpsFixture rig;
    rig.setConfigText( "sale.percent", "30" );
    LiveEventDef definition = LiveOpsTestUtil::makeEvent();
    definition._listParameter.push_back( LiveOpsTestUtil::makeParameter( "percent", "@sale.percent" ) );
    definition._listParameter.push_back( LiveOpsTestUtil::makeParameter( "missing", "@no.such.key" ) );
    definition._listParameter.push_back( LiveOpsTestUtil::makeParameter( "plain", "literal" ) );
    SW_EXPECT_TRUE( rig.put( definition ) == LiveOpsResult::Ok );
    rig.tick( kStartMs );

    string value;
    SW_REQUIRE_TRUE( rig._service.findParameter( "spring.sale", "percent", value ) );
    SW_EXPECT_EQUAL( string( "30" ), value );
    SW_REQUIRE_TRUE( rig._service.findParameter( "spring.sale", "plain", value ) );
    SW_EXPECT_EQUAL( string( "literal" ), value );
    SW_REQUIRE_TRUE( rig._service.findParameter( "spring.sale", "missing", value ) );
    SW_EXPECT_EQUAL( string( "@no.such.key" ), value );
    SW_EXPECT_FALSE( rig._service.findParameter( "spring.sale", "none", value ) );
    SW_EXPECT_FALSE( rig._service.findParameter( "none", "percent", value ) );
}

/** @brief [LiveOpsServiceTest] 긴급 스위치: 끄면 아무 이벤트도 보이지 않고 클라이언트가 다시 받게 알린다. 켜면 돌아온다 */
SW_TEST_CASE( LiveOpsServiceTest, KillSwitchHidesEverything )
{
    LiveOpsFixture rig;
    SW_EXPECT_TRUE( rig.put( LiveOpsTestUtil::makeEvent() ) == LiveOpsResult::Ok );
    rig.tick( kStartMs );
    (void)rig._service.takeStateChange();

    rig.setKillSwitch( false );
    rig.tick( kStartMs + 1 );
    SW_EXPECT_TRUE( rig._service.takeStateChange() );
    SW_EXPECT_TRUE( rig.active( "kr", kStartMs + 1 ).empty() );
    SW_EXPECT_FALSE( rig._service.isEventActive( "spring.sale", kPlayer, "kr", 0, UnixMilliseconds{ kStartMs + 1 } ) );

    rig.setKillSwitch( true );
    rig.tick( kStartMs + 2 );
    SW_EXPECT_TRUE( rig._service.takeStateChange() );
    SW_EXPECT_EQUAL( size_t( 1 ), rig.active( "kr", kStartMs + 2 ).size() );
}

/** @brief [LiveOpsServiceTest] JSON 입구: 같은 규칙으로 저장하고 내보낸 JSON 을 다시 넣어도 같다. 깨진 JSON 은 Invalid 다 */
SW_TEST_CASE( LiveOpsServiceTest, JSONRoundTrip )
{
    SW_EXPECT_LOG( "JSON.ParseError" );
    LiveOpsFixture rig;
    SW_EXPECT_TRUE( rig.put( LiveOpsTestUtil::makeEvent() ) == LiveOpsResult::Ok );
    rig.tick( kStartMs );
    vector<string> listJSON;
    rig._service.collectEventJSON( listJSON );
    SW_REQUIRE_EQUAL( size_t( 1 ), listJSON.size() );

    rig._service.putEventJSON( listJSON[0], kGM, "m", UnixMilliseconds{ kStartMs }, 6 );
    rig.pump();
    SW_EXPECT_TRUE( rig.takeResult() == LiveOpsResult::Ok );

    rig._service.putEventJSON( "{ not json", kGM, "m", UnixMilliseconds{ kStartMs }, 7 );
    SW_EXPECT_TRUE( rig.takeResult().hasError( OnlineError::kInvalidRequest ) );
}

/** @brief [LiveOpsServiceTest] 클라이언트의 이벤트 캐시는 열린 이벤트 응답만 고친다. 기기 등록 응답(빈 목록)이 캐시를 지우지 않는다 */
SW_TEST_CASE( LiveOpsServiceTest, DeviceReplyKeepsTheEventCache )
{
    LiveOpsClient     client;
    LiveOpsStateReply stateReply;
    stateReply._listEvent.emplace_back()._kind = "double_xp";
    client.onCallSucceeded( LiveOpsMethod::GetLiveState{}, LiveOpsStateRequest{}, stateReply );
    SW_EXPECT_TRUE( client.hasEventKind( "double_xp" ) );
    const uint64 revision = client.getRevision();

    client.onCallSucceeded( LiveOpsMethod::RegisterDevice{}, PushDeviceRegistration{}, LiveOpsDeviceReply{} );
    SW_EXPECT_TRUE( client.hasEventKind( "double_xp" ) );
    SW_EXPECT_EQUAL( revision, client.getRevision() );
}
