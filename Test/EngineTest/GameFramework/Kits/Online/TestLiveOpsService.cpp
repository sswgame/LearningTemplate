// 라이브 운영 로직 — 한 서버에서 넣은 이벤트가 그 서버(다시 읽기)와 다른 서버(버스 재촉 — 주기 전)에 보이고 감사 줄이 남음, 회차 경계에서 다시 읽지 않아도 바뀜 알림,
// '@' 매개변수는 원격 설정 값(글 · 정수)으로 풀리고 긴급 스위치가 모두 숨김, 저장소 실패는 아무것도 남기지 않음 · 없는 이벤트 지우기는 NotFound · 규칙 밖은 Invalid.
#include "pch.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Config/RemoteConfig.h"
#include "GameFramework/Base/Online/Store/MemoryServiceStore.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"

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

    /** @brief 서버 하나의 라이브 운영 — 저장소 앞 · 로컬 버스 · 로직. 버스 메시지는 호스트 대신 시험이 비워 notifyChanged 로 넘긴다. */
    struct LiveOpsNode
    {
        MemoryServiceStore        _store;
        LocalServerBus            _bus;
        LiveOpsService            _service;
        vector<LiveOpsCompletion> _listCompletion;
        int32                     _stateChangeCount;

        LiveOpsNode( MemoryServiceDatabase* pDatabase, LocalServerBusHub* pHub, uint64 serverID, const RemoteConfig* pRemoteConfig = nullptr )
            : _store{ pDatabase }
            , _bus{ pHub, serverID }
            , _service{}
            , _listCompletion{}
            , _stateChangeCount{ 0 }
        {
            _bus.subscribe( LiveOpsBus::kChangedTopic );
            LiveOpsDependencies dependencies;
            dependencies._pStore        = &_store;
            dependencies._pBus          = &_bus;
            dependencies._pRemoteConfig = pRemoteConfig;
            _service.initialize( dependencies );
        }

        ~LiveOpsNode()
        {
            _service.shutdown();
            _store.shutdown();
            (void)_store.pollCompletions();
        }

        void step( int64 nowMs )
        {
            for ( int32 round = 0; round < 4; ++round )
            {
                vector<ServerBusMessage> listMessage;
                (void)_bus.pollMessages( listMessage );
                for ( const ServerBusMessage& message : listMessage )
                {
                    if ( message._originServerID != _bus.getServerID() )
                        _service.notifyChanged();
                }
                _service.tick( nowMs );
                (void)_store.pollCompletions();
                _stateChangeCount += _service.takeStateChange() ? 1 : 0;
            }
            _service.drainCompletions( _listCompletion );
        }

        int32 countActive( int64 nowMs ) const
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
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    LiveOpsNode           first( &database, &hub, 1 );
    LiveOpsNode           second( &database, &hub, 2 );
    const int64           nowMs = Internal::kMonday20261005 + Internal::kHour;
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
    SW_EXPECT_FALSE( second._service.isEventActive( "halloween", 1, "kr", 100, Internal::kMonday20261005 + 7 * Internal::kDay ) );

    first._service.removeEvent( "halloween", 7, "", nowMs + 2, 2 );
    first.step( nowMs + 2 );
    second.step( nowMs + 3 );
    SW_EXPECT_EQUAL( second.countActive( nowMs + 3 ), 0 );
    SW_EXPECT_EQUAL( database.countRecords( ServiceAuditLog::getTable() ), 2 );
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
    node._service.putEvent( evening, 0, "", eveningMs - 60000, 1 );
    node.step( eveningMs - 60000 ); // 19:59 — 닫혀 있다
    SW_EXPECT_EQUAL( node._stateChangeCount, 0 );
    node.step( eveningMs ); // 20:00 — 열림
    SW_EXPECT_EQUAL( node._stateChangeCount, 1 );
    SW_EXPECT_EQUAL( node.countActive( eveningMs ), 1 );
    node.step( eveningMs + Internal::kHour / 2 );
    SW_EXPECT_EQUAL( node._stateChangeCount, 1 );
    node.step( eveningMs + Internal::kHour ); // 21:00 — 닫힘
    SW_EXPECT_EQUAL( node._stateChangeCount, 2 );
    node.step( eveningMs + Internal::kDay ); // 다음 날 20:00 — 다시
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
    config.submitSet( configFront, nullptr, "event.xp_rate", rate, Internal::makeAudit() );
    RemoteConfigValue bonus;
    bonus._type    = RemoteConfigValueType::Integer;
    bonus._integer = 3;
    config.submitSet( configFront, nullptr, "event.bonus", bonus, Internal::makeAudit() );
    (void)configFront.pollCompletions();
    SW_ASSERT_TRUE( config.getLastSetResult() == ServiceStoreResult::Ok );

    LiveOpsNode         node( &database, &hub, 1, &config );
    LiveEventDefinition definition = Internal::makeHalloween();
    definition._listParameter.push_back( LiveEventParameter{ "rate", "@event.xp_rate" } );
    definition._listParameter.push_back( LiveEventParameter{ "bonus", "@event.bonus" } );
    definition._listParameter.push_back( LiveEventParameter{ "label", "spooky" } );
    definition._listParameter.push_back( LiveEventParameter{ "missing", "@event.nope" } );
    const int64 nowMs = Internal::kMonday20261005 + Internal::kHour;
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
    config.submitSet( configFront, nullptr, LiveOpsBus::kKillSwitchFlag, killSwitch, Internal::makeAudit() );
    (void)configFront.pollCompletions();
    SW_EXPECT_EQUAL( node.countActive( nowMs ), 0 );
    SW_EXPECT_FALSE( node._service.isEventActive( "halloween", 1, "kr", 100, nowMs ) );
    node.step( nowMs + 1 );
    SW_EXPECT_EQUAL( node._stateChangeCount, changeCountBefore + 1 ); // 모두에게 "다시 받아라"
}

SW_TEST_CASE( LiveOpsServiceTest, StoreFailureOnPutLeavesNothing )
{
    using Internal = LiveOpsServiceTestInternal;
    MemoryServiceDatabase database;
    LocalServerBusHub     hub;
    LiveOpsNode           node( &database, &hub, 1 );
    const int64           nowMs = Internal::kMonday20261005;
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
