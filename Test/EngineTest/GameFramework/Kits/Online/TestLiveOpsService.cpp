// 라이브 운영 로직 — 한 서버에서 넣은 이벤트가 그 서버(다시 읽기)와 다른 서버(버스 재촉 — 주기 전)에 보이고 감사 줄이 남음, 회차 경계에서 다시 읽지 않아도 바뀜 알림,
// '@' 매개변수는 원격 설정 값(글 · 정수)으로 풀리고 긴급 스위치가 모두 숨김, 저장소 실패는 아무것도 남기지 않음 · 없는 이벤트 지우기는 NotFound · 규칙 밖은 Invalid.
#include "pch.h"

#include "EngineTest/GameFramework/Online/OnlineMessageTestUtil.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Shared/LiveOpsProtocol.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct LiveOpsServiceTestInternal
    {
        static constexpr int64 kMonday20261005 = 1791158400000ll; ///< 2026-10-05 00:00 UTC
        static constexpr int64 kHour           = 3600000ll;
        static constexpr int64 kDay            = 24 * kHour;

        static LiveEventDefinition makeHalloween()
        {
            LiveEventDefinition definition;
            definition._eventID = "halloween";
            definition._kind    = "xp_boost";
            definition._startMs = kMonday20261005;
            definition._endMs   = kMonday20261005 + 7 * kDay;
            return definition;
        }

        static ServiceAuditEntry makeAudit()
        {
            ServiceAuditEntry audit;
            audit._actor   = "gm.0000000000000001";
            audit._action  = "config.set";
            audit._subject = "config";
            audit._timeMs  = 1;
            return audit;
        }
    };

    /** @brief 서버 하나의 라이브 운영 — 저장소 앞 · 로컬 버스 · 로직. 버스 메시지는 호스트 대신 테스트가 비워 notifyChanged 로 넘긴다. */
    struct LiveOpsNode
    {
        MemoryServiceStore        _store;
        LocalServerBus            _bus;
        ServerBus                 _busFront;
        LiveOpsService            _service;
        vector<LiveOpsCompletion> _listCompletion;
        int32                     _stateChangeCount;

        LiveOpsNode( MemoryServiceDatabase* pDatabase, LocalServerBusHub* pHub, uint64 serverID, const RemoteConfig* pRemoteConfig = nullptr )
            : _store{ pDatabase }
            , _bus{ pHub, serverID }
            , _busFront{ _bus }
            , _service{}
            , _listCompletion{}
            , _stateChangeCount{ 0 }
        {
            _bus.subscribe( LiveOpsBus::kChangedTopic.getName() );
            LiveOpsDependencies dependencies;
            dependencies._pStore        = &_store;
            dependencies._pBus          = &_busFront;
            dependencies._pRemoteConfig = pRemoteConfig;
            _service.initialize( dependencies );
        }

        ~LiveOpsNode()
        {
            _service.shutdown();
            _store.shutdown();
            (void)_store.pollCompletions();
        }

        void step( UnixMilliseconds nowMs )
        {
            for ( int32 round = 0; round < 4; ++round )
            {
                vector<ServerBusMessage> listMessage;
                (void)_bus.pollMessages( listMessage );
                for ( const ServerBusMessage& message : listMessage )
                {
                    LiveOpsChanged changed;
                    const bool     bFromOtherServer = message.getOriginServerID() != _bus.getServerID();
                    if ( bFromOtherServer && LiveOpsBus::kChangedTopic.read( message, changed ) == ServerBusReadResult::Read )
                        _service.notifyChanged();
                }
                _service.update( nowMs );
                (void)_store.pollCompletions();
                _stateChangeCount += _service.takeStateChange() ? 1 : 0;
            }
            _service.drainCompletions( _listCompletion );
        }

        int32 countActive( UnixMilliseconds nowMs ) const
        {
            vector<LiveEventState> listEvent;
            _service.computeActiveEvents( 1, "kr", 100, nowMs, true, listEvent );
            return static_cast<int32>( listEvent.size() );
        }
    };
} // namespace

SW_TEST_CASE( LiveOpsServiceTest, PutEventIsSeenAfterReloadAndOnOtherServer )
{
    using Internal = LiveOpsServiceTestInternal;
    MemoryServiceDatabase  database;
    LocalServerBusHub      hub;
    LiveOpsNode            first( &database, &hub, 1 );
    LiveOpsNode            second( &database, &hub, 2 );
    const UnixMilliseconds nowMs{ Internal::kMonday20261005 + Internal::kHour };
    first.step( nowMs );
    second.step( nowMs ); // 첫 다시 읽기 — 빈 표
    SW_EXPECT_EQUAL( second.countActive( nowMs ), 0 );

    first._service.putEvent( Internal::makeHalloween(), 7, "spooky week", nowMs, 1 );
    first.step( nowMs );
    SW_ASSERT_EQUAL( first._listCompletion.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( first._listCompletion[0]._result == LiveOpsResult::Ok );
    SW_EXPECT_EQUAL( first.countActive( nowMs ), 1 );
    SW_EXPECT_EQUAL( first._stateChangeCount, 1 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceAuditLog::getTable() ), 1 );

    second.step( nowMs + 1 ); // 주기(30 초) 전 — 버스가 재촉했다
    SW_EXPECT_EQUAL( second.countActive( nowMs + 1 ), 1 );
    SW_EXPECT_EQUAL( second._stateChangeCount, 1 );
    SW_EXPECT_TRUE( second._service.isEventActive( "halloween", 1, "kr", 100, nowMs + 1 ) );
    SW_EXPECT_FALSE( second._service.isEventActive( "halloween", 1, "kr", 100, UnixMilliseconds{ Internal::kMonday20261005 + 7 * Internal::kDay } ) );

    first._service.removeEvent( "halloween", 7, "", nowMs + 2, 2 );
    first.step( nowMs + 2 );
    second.step( nowMs + 3 );
    SW_EXPECT_EQUAL( second.countActive( nowMs + 3 ), 0 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceAuditLog::getTable() ), 2 );
}

SW_TEST_CASE( LiveOpsServiceTest, GmEventControlTakesAndListsJSONDefinitions )
{
    using Internal = LiveOpsServiceTestInternal;
    MemoryServiceDatabase  database;
    LocalServerBusHub      hub;
    LiveOpsNode            node( &database, &hub, 1 );
    const UnixMilliseconds nowMs{ Internal::kMonday20261005 + Internal::kHour };
    node.step( nowMs );
    ILiveEventControl& control = node._service;
    control.putEventJSON( LiveOpsProtocol::writeEventJSON( Internal::makeHalloween() ), 7, "json", nowMs, 1 );
    control.putEventJSON( "{not json", 7, "json", nowMs, 2 );
    node.step( nowMs );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 2 ) );
    for ( const LiveOpsCompletion& completion : node._listCompletion )
    {
        SW_EXPECT_TRUE( completion._result == ( completion._requestTag == 1 ? LiveOpsResult::Ok : LiveOpsResult::Invalid ) );
    }
    vector<string> listJSON;
    control.collectEventJSON( listJSON );
    SW_ASSERT_EQUAL( listJSON.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( listJSON[0].find( "halloween" ) != string::npos );
    LiveEventDefinition readBack;
    SW_ASSERT_TRUE( LiveOpsProtocol::readEventJSON( listJSON[0], readBack ) );
    SW_EXPECT_EQUAL( readBack._kind, string( "xp_boost" ) );
}

SW_TEST_CASE( LiveOpsServiceTest, BoundaryRaisesStateChangeWithoutReload )
{
    using Internal = LiveOpsServiceTestInternal;
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    LiveOpsNode           node( &database, &hub, 1 );
    LiveEventDefinition   evening;
    evening._eventID           = "evening_drop";
    evening._kind              = "drop";
    evening._startMs           = Internal::kMonday20261005;
    evening._endMs             = Internal::kMonday20261005 + 3 * Internal::kDay;
    evening._recurrence        = LiveEventRecurrence::Daily;
    evening._activeMinuteOfDay = 20 * 60;
    evening._activeDurationMs  = Internal::kHour;
    const int64 eveningMs      = Internal::kMonday20261005 + 20 * Internal::kHour;
    node._service.putEvent( evening, 0, "", UnixMilliseconds{ eveningMs - 60000 }, 1 );
    node.step( UnixMilliseconds{ eveningMs - 60000 } ); // 19:59 — 닫혀 있다
    SW_EXPECT_EQUAL( node._stateChangeCount, 0 );
    node.step( UnixMilliseconds{ eveningMs } ); // 20:00 — 열림
    SW_EXPECT_EQUAL( node._stateChangeCount, 1 );
    SW_EXPECT_EQUAL( node.countActive( UnixMilliseconds{ eveningMs } ), 1 );
    node.step( UnixMilliseconds{ eveningMs + Internal::kHour / 2 } );
    SW_EXPECT_EQUAL( node._stateChangeCount, 1 );
    node.step( UnixMilliseconds{ eveningMs + Internal::kHour } ); // 21:00 — 닫힘
    SW_EXPECT_EQUAL( node._stateChangeCount, 2 );
    node.step( UnixMilliseconds{ eveningMs + Internal::kDay } ); // 다음 날 20:00 — 다시
    SW_EXPECT_EQUAL( node._stateChangeCount, 3 );
}

SW_TEST_CASE( LiveOpsServiceTest, ParameterResolvesRemoteConfigAndKillSwitchHidesAll )
{
    using Internal = LiveOpsServiceTestInternal;
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    MemoryServiceStore    configFront{ &database };
    RemoteConfig          config;
    RemoteConfigValue     rate;
    rate._type = RemoteConfigValueType::Text;
    rate._text = "2.5";
    config.submitSet( configFront, "event.xp_rate", rate, Internal::makeAudit() );
    RemoteConfigValue bonus;
    bonus._type    = RemoteConfigValueType::Integer;
    bonus._integer = 3;
    config.submitSet( configFront, "event.bonus", bonus, Internal::makeAudit() );
    (void)configFront.pollCompletions();
    SW_ASSERT_TRUE( config.getLastSetResult() == ServiceStoreResult::Ok );

    LiveOpsNode         node( &database, &hub, 1, &config );
    LiveEventDefinition definition = Internal::makeHalloween();
    definition._listParameter.push_back( LiveEventParameter{ "rate", "@event.xp_rate" } );
    definition._listParameter.push_back( LiveEventParameter{ "bonus", "@event.bonus" } );
    definition._listParameter.push_back( LiveEventParameter{ "label", "spooky" } );
    definition._listParameter.push_back( LiveEventParameter{ "missing", "@event.nope" } );
    const UnixMilliseconds nowMs{ Internal::kMonday20261005 + Internal::kHour };
    node._service.putEvent( definition, 0, "", nowMs, 1 );
    node.step( nowMs );
    string value;
    SW_ASSERT_TRUE( node._service.findParameter( "halloween", "rate", value ) );
    SW_EXPECT_STREQ( value.c_str(), "2.5" );
    SW_ASSERT_TRUE( node._service.findParameter( "halloween", "bonus", value ) );
    SW_EXPECT_STREQ( value.c_str(), "3" );
    SW_ASSERT_TRUE( node._service.findParameter( "halloween", "label", value ) );
    SW_EXPECT_STREQ( value.c_str(), "spooky" );
    SW_ASSERT_TRUE( node._service.findParameter( "halloween", "missing", value ) );
    SW_EXPECT_STREQ( value.c_str(), "@event.nope" ); // 없는 키는 그대로(경고)
    SW_EXPECT_FALSE( node._service.findParameter( "halloween", "nope", value ) );
    vector<LiveEventState> listEvent;
    node._service.computeActiveEvents( 1, "kr", 100, nowMs, true, listEvent );
    SW_ASSERT_EQUAL( listEvent.size(), size_t( 1 ) );
    SW_ASSERT_EQUAL( listEvent[0]._listParameter.size(), size_t( 4 ) );
    SW_EXPECT_STREQ( listEvent[0]._listParameter[0]._value.c_str(), "2.5" );

    const int32       changeCountBefore = node._stateChangeCount;
    RemoteConfigValue killSwitch;
    killSwitch._type               = RemoteConfigValueType::Flag;
    killSwitch._integer            = 1;
    killSwitch._rolloutBasisPoints = 0;
    config.submitSet( configFront, LiveOpsBus::kKillSwitchFlag, killSwitch, Internal::makeAudit() );
    (void)configFront.pollCompletions();
    SW_EXPECT_EQUAL( node.countActive( nowMs ), 0 );
    SW_EXPECT_FALSE( node._service.isEventActive( "halloween", 1, "kr", 100, nowMs ) );
    node.step( nowMs + 1 );
    SW_EXPECT_EQUAL( node._stateChangeCount, changeCountBefore + 1 ); // 모두에게 "다시 받아라"
}

SW_TEST_CASE( LiveOpsServiceTest, StoreFailureOnPutLeavesNothing )
{
    using Internal = LiveOpsServiceTestInternal;
    MemoryServiceDatabase  database;
    LocalServerBusHub      hub;
    LiveOpsNode            node( &database, &hub, 1 );
    const UnixMilliseconds nowMs{ Internal::kMonday20261005 };
    node.step( nowMs );
    const uint64 hashBefore = database.computeContentHash();
    database.armFault( ServiceStoreFault::RejectCommit );
    node._service.putEvent( Internal::makeHalloween(), 0, "", nowMs, 1 );
    node.step( nowMs );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( node._listCompletion[0]._result == LiveOpsResult::Unavailable );
    SW_EXPECT_EQUAL( database.computeContentHash(), hashBefore );
    SW_EXPECT_EQUAL( node.countActive( nowMs ), 0 );

    node._service.removeEvent( "nope", 0, "", nowMs, 2 );
    LiveEventDefinition broken = Internal::makeHalloween();
    broken._endMs              = broken._startMs;
    node._service.putEvent( broken, 0, "", nowMs, 3 );
    node.step( nowMs );
    SW_ASSERT_EQUAL( node._listCompletion.size(), size_t( 3 ) );
    SW_EXPECT_TRUE( node._listCompletion[1]._result == LiveOpsResult::Invalid ); // 규칙 밖은 맡기기 전에
    SW_EXPECT_TRUE( node._listCompletion[2]._result == LiveOpsResult::NotFound );
    SW_EXPECT_EQUAL( node._service.getPendingCount(), 0 );
}

/**
 * @brief [LiveOpsProtocolTest] 라이브 운영 메시지는 상한까지 왕복하고, 이벤트 정의와 기기 레코드는 자기 기술형으로 왕복하며, 다른 판의 레코드는 읽지 않는다
 */
SW_TEST_CASE( LiveOpsProtocolTest, MessagesAndRecordsRoundTrip )
{
    using Messages = LiveOpsProtocol::MessageTypes;
    using Util     = test::OnlineMessageTestUtil;
    LiveOpsReply reply;
    reply._result = LiveOpsResult::UnknownProvider;
    LiveEventState state;
    state._eventID     = string( 48, 'e' );
    state._kind        = string( 32, 'k' );
    state._windowEndMs = -1;
    state._listParameter.assign( 16, LiveEventParameter{ string( 32, 'p' ), string( 128, 'v' ) } );
    reply._listEvent.assign( 256, state );
    SW_EXPECT_TRUE( Util::roundTrips<Messages>( reply ) );
    SW_EXPECT_TRUE( Util::roundTrips<Messages>( PushDeviceRegistration{ "apns", string( 256, 't' ), "ko-KR", 5 } ) );
    SW_EXPECT_TRUE( Util::refusesEveryPrefix<Messages>( LiveOpsStateRequest{ "kr", 7 } ) );
    SW_EXPECT_TRUE( ( Util::refusesAsOtherType<Messages, LiveOpsDeviceKey>( LiveOpsStateRequest{ "kr", 7 } ) ) );

    LiveEventDefinition definition;
    definition._eventID    = "halloween";
    definition._kind       = "xp_boost";
    definition._listRegion = { "kr", "eu-west" };
    definition._recurrence = LiveEventRecurrence::Weekly;
    definition._listParameter.push_back( LiveEventParameter{ "rate", "@liveops.xp" } );
    LiveEventDefinition decodedEvent;
    vector<uint8>       eventBytes;
    SW_ASSERT_TRUE( LiveOpsProtocol::encodeEvent( definition, eventBytes ) );
    SW_ASSERT_TRUE( LiveOpsProtocol::decodeEvent( "halloween", eventBytes, decodedEvent ) );
    SW_EXPECT_TRUE( decodedEvent._listRegion.size() == 2 && decodedEvent._listRegion[1] == "eu-west" );
    SW_EXPECT_TRUE( decodedEvent._recurrence == LiveEventRecurrence::Weekly && decodedEvent._listParameter[0]._value == "@liveops.xp" );
    PushDeviceRegistration decodedDevice;
    vector<uint8>          deviceBytes;
    SW_ASSERT_TRUE( LiveOpsProtocol::encodeDevice( PushDeviceRegistration{ "fcm", "token", "en", 9 }, deviceBytes ) );
    SW_ASSERT_TRUE( LiveOpsProtocol::decodeDevice( "device", deviceBytes, decodedDevice ) );
    SW_EXPECT_TRUE( decodedDevice._token == "token" && decodedDevice._registeredMs == 9 );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a corrupt durable record is logged" );
        vector<uint8> otherVersion = eventBytes;
        otherVersion[0]            = 2;
        SW_EXPECT_FALSE( LiveOpsProtocol::decodeEvent( "halloween", otherVersion, decodedEvent ) ); // 다른 버전은 변환 없이 읽지 않는다
        // 기기 등록은 고정 배치형 코덱이 있으므로 쓰기와 읽기가 선언된 범위(토큰 256 바이트)도 확인한다
        const PushDeviceRegistration longToken{ "fcm", string( PushLimit::kMaxTokenSize + 1, 't' ), "en", 9 };
        vector<uint8>                longTokenBytes;
        SW_EXPECT_FALSE( LiveOpsProtocol::encodeDevice( longToken, longTokenBytes ) );
        SW_ASSERT_TRUE( test::OnlineMessageTestUtil::writeUncheckedRecord( 1, "sw::PushDeviceRegistration", longToken, longTokenBytes ) ); // 다른 빌드가 쓴 레코드를 흉내 낸다
        SW_EXPECT_FALSE( LiveOpsProtocol::decodeDevice( "device", longTokenBytes, decodedDevice ) );
        SW_EXPECT_TRUE( decodedDevice._token == "token" ); // 못 읽으면 그대로이다
    }
    {
        SW_TEST_DEFENSIVE_SCOPE( "out-of-range values are not written" );
        SW_EXPECT_TRUE( Util::refusesToWrite<Messages>( LiveOpsDeviceKey{ string( 17, 'p' ), "token" } ) );
    }
}
