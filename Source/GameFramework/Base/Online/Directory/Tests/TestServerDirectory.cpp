#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Base/Online/Directory/ServerRecord.h"
#include "GameFramework/Base/Online/Directory/ServerRegistration.h"
#include "GameFramework/Base/Online/Directory/ServerRegistryReader.h"
#include "GameFramework/Base/Online/Directory/ServerSelection.h"

// ServerDirectoryTest — 서버 디렉터리의 기반: 기록의 키 · 이름 규칙 · 와이어 형식(판 · 포트 수 · 상태 거절), 포트 샤딩, 접속 분배(후보 조건 · 지역 우선 ·
// 찬 비율 · 서버 id 동률), 등록의 하트비트 · 즉시 쓰기, 읽는 쪽의 스냅숏 · 고른 인원 더하기 · 죽은 멤버 정리. 캐시는 메모리 구현이다(nogpu).

namespace
{
    constexpr int64 kNowMs = 1700000000000ll;

    sw::ServerStatus makeStatus( uint64 serverID, sw::string_view region, int32 load, int32 capacity )
    {
        sw::ServerStatus status;
        status._descriptor._serverID     = serverID;
        status._descriptor._kind         = "game";
        status._descriptor._region       = sw::string( region );
        status._descriptor._address      = "10.0.0.1";
        status._descriptor._buildVersion = 7;
        status._descriptor._capacity     = capacity;
        status._descriptor._port         = 7000;
        status._descriptor._portCount    = 4;
        status._heartbeatMs              = kNowMs;
        status._load                     = load;
        status._state                    = sw::ServerState::Open;
        return status;
    }

    sw::ServerSelectionQuery makeQuery( sw::string_view region )
    {
        sw::ServerSelectionQuery query;
        query._kind   = "game";
        query._region = sw::string( region );
        return query;
    }

    /** @brief 메모리 캐시 + 라우터입니다. 등록(쓰기)과 읽는 쪽(읽기)이 같은 라우터를 쓴다. */
    struct DirectoryRig
    {
        DirectoryRig()
            : _clock{ sw::MonotonicMilliseconds{ 0 } }
            , _database{ _clock }
            , _store{ &_database }
            , _router{}
        {
            _router.initialize( &_store );
        }

        /** @brief 답이 답을 낳는(색인 → 기록) 사슬이 끝날 때까지 나눠 줍니다. */
        void pumpAll()
        {
            for ( int32 round = 0; round < 8; ++round )
            {
                if ( _router.pump() == 0 && _store.getPendingCount() == 0 )
                    return;
            }
        }

        sw::ManualClock             _clock;
        sw::MemoryEphemeralDatabase _database;
        sw::MemoryEphemeralStore    _store;
        sw::EphemeralStoreRouter    _router;
    };
} // namespace

/**
 * @brief [ServerDirectoryTest] 키는 서버 id 를 소문자 16 진 16 자로 싣고(사전순 = 수 순서), 멤버는 되읽히며, 이름은 `[0-9a-z_-]` 1..16 B 다
 */
SW_TEST_CASE( ServerDirectoryTest, RecordKeysAndNameRule )
{
    SW_EXPECT_EQUAL( sw::string( "sd/srv/00000000000000ab" ), sw::ServerRecord::makeRecordKey( 0xAB ) );
    SW_EXPECT_EQUAL( sw::string( "sd/idx/game" ), sw::ServerRecord::makeIndexKey( "game" ) );
    SW_EXPECT_TRUE( sw::EphemeralRequest::isValidKey( sw::ServerRecord::makeRecordKey( ~uint64( 0 ) ) ) );

    uint64 parsed = 0;
    SW_REQUIRE_TRUE( sw::ServerRecord::parseMember( sw::ServerRecord::makeMember( 0x123456789abcdef0ull ), parsed ) );
    SW_EXPECT_EQUAL( uint64( 0x123456789abcdef0ull ), parsed );
    SW_EXPECT_FALSE( sw::ServerRecord::parseMember( "12", parsed ) );
    SW_EXPECT_FALSE( sw::ServerRecord::parseMember( "zzzzzzzzzzzzzzzz", parsed ) );

    SW_EXPECT_TRUE( sw::ServerRecord::isValidName( "eu-west" ) );
    SW_EXPECT_TRUE( sw::ServerRecord::isValidName( "match_2" ) );
    SW_EXPECT_TRUE( sw::ServerRecord::isValidName( sw::string( 16, 'a' ) ) );
    SW_EXPECT_FALSE( sw::ServerRecord::isValidName( sw::string( 17, 'a' ) ) );
    SW_EXPECT_FALSE( sw::ServerRecord::isValidName( "" ) );
    SW_EXPECT_FALSE( sw::ServerRecord::isValidName( "EU" ) );
    SW_EXPECT_FALSE( sw::ServerRecord::isValidName( "eu.west" ) );
}

/**
 * @brief [ServerDirectoryTest] 기록은 모든 칸을 되읽고, 다른 형식 판 · 포트 수 0 이나 상한 밖 · 모르는 상태 · 잘린 바이트는 거절한다
 */
SW_TEST_CASE( ServerDirectoryTest, RecordRoundTripsAndRejectsBadFields )
{
    sw::ServerStatus status           = makeStatus( 0xBEEF, "kr", 12, 100 );
    status._state                     = sw::ServerState::Maintenance;
    const sw::vector<uint8> listBytes = sw::ServerRecord::encode( status );
    SW_REQUIRE_TRUE( listBytes.empty() == false );
    SW_EXPECT_EQUAL( sw::ServerRecord::kFormatVersion, listBytes[0] );

    sw::ServerStatus read;
    SW_REQUIRE_TRUE( sw::ServerRecord::decode( listBytes, read ) );
    SW_EXPECT_EQUAL( uint64( 0xBEEF ), read._descriptor._serverID );
    SW_EXPECT_EQUAL( sw::string( "game" ), read._descriptor._kind );
    SW_EXPECT_EQUAL( sw::string( "kr" ), read._descriptor._region );
    SW_EXPECT_EQUAL( sw::string( "10.0.0.1" ), read._descriptor._address );
    SW_EXPECT_EQUAL( uint32( 7 ), read._descriptor._buildVersion );
    SW_EXPECT_EQUAL( 100, read._descriptor._capacity );
    SW_EXPECT_EQUAL( uint16( 7000 ), read._descriptor._port );
    SW_EXPECT_EQUAL( uint8( 4 ), read._descriptor._portCount );
    SW_EXPECT_EQUAL( kNowMs, read._heartbeatMs );
    SW_EXPECT_EQUAL( 12, read._load );
    SW_EXPECT_TRUE( read._state == sw::ServerState::Maintenance );

    sw::vector<uint8> listOldVersion = listBytes;
    listOldVersion[0]                = 1;
    SW_EXPECT_FALSE( sw::ServerRecord::decode( listOldVersion, read ) );

    sw::ServerStatus noPort       = status;
    noPort._descriptor._portCount = 0;
    SW_EXPECT_FALSE( sw::ServerRecord::decode( sw::ServerRecord::encode( noPort ), read ) );
    sw::ServerStatus tooManyPorts       = status;
    tooManyPorts._descriptor._portCount = static_cast<uint8>( sw::ServerDescriptor::kMaxPortCount + 1 );
    SW_EXPECT_FALSE( sw::ServerRecord::decode( sw::ServerRecord::encode( tooManyPorts ), read ) );
    sw::ServerStatus maxPorts       = status;
    maxPorts._descriptor._portCount = static_cast<uint8>( sw::ServerDescriptor::kMaxPortCount );
    SW_EXPECT_TRUE( sw::ServerRecord::decode( sw::ServerRecord::encode( maxPorts ), read ) );

    sw::ServerStatus unknownState = status;
    unknownState._state           = sw::ServerState::Count;
    SW_EXPECT_FALSE( sw::ServerRecord::decode( sw::ServerRecord::encode( unknownState ), read ) );

    sw::vector<uint8> listTruncated = listBytes;
    listTruncated.resize( listTruncated.size() - 2 );
    SW_EXPECT_FALSE( sw::ServerRecord::decode( listTruncated, read ) );
    SW_EXPECT_FALSE( sw::ServerRecord::decode( {}, read ) );
}

/**
 * @brief [ServerDirectoryTest] 포트 샤딩: 계정마다 늘 같은 포트이고, 나머지로 첫 포트부터 포트 수 안에 고르게 퍼지며, 포트 하나면 늘 첫 포트다
 */
SW_TEST_CASE( ServerDirectoryTest, PortShardIsStablePerAccountAndInRange )
{
    const sw::ServerDescriptor descriptor = makeStatus( 1, "kr", 0, 10 )._descriptor;
    SW_EXPECT_EQUAL( descriptor.pickPort( 12345 ), descriptor.pickPort( 12345 ) );
    SW_EXPECT_EQUAL( uint16( 7000 ), descriptor.pickPort( 8 ) );
    SW_EXPECT_EQUAL( uint16( 7003 ), descriptor.pickPort( 7 ) );
    int32 arrHit[4] = {};
    for ( uint64 accountID = 1; accountID <= 400; ++accountID )
    {
        const uint16 port = descriptor.pickPort( accountID );
        SW_REQUIRE_TRUE( 7000 <= port && port < 7004 );
        ++arrHit[port - 7000];
    }
    for ( const int32 hit : arrHit )
    {
        SW_EXPECT_EQUAL( 100, hit );
    }
    SW_EXPECT_EQUAL( uint16( 9000 ), sw::ServerDescriptor::pickShardPort( 9000, 1, 987654321 ) );
}

/**
 * @brief [ServerDirectoryTest] 후보 조건: Open(점검 허용이면 Maintenance 도) · 같은 종류 · 빌드 일치(0 이면 무관) · 하트비트 신선 · 부하 + 좌석 ≤ 정원 > 0
 */
SW_TEST_CASE( ServerDirectoryTest, CandidateRequiresOpenKindBuildFreshAndSeats )
{
    const sw::ServerSelectionQuery query = makeQuery( "kr" );
    SW_EXPECT_TRUE( sw::ServerSelection::isCandidate( makeStatus( 1, "kr", 0, 10 ), query, kNowMs ) );

    sw::ServerStatus draining = makeStatus( 1, "kr", 0, 10 );
    draining._state           = sw::ServerState::Draining;
    SW_EXPECT_FALSE( sw::ServerSelection::isCandidate( draining, query, kNowMs ) );
    sw::ServerStatus starting = makeStatus( 1, "kr", 0, 10 );
    starting._state           = sw::ServerState::Starting;
    SW_EXPECT_FALSE( sw::ServerSelection::isCandidate( starting, query, kNowMs ) );

    sw::ServerStatus maintenance = makeStatus( 1, "kr", 0, 10 );
    maintenance._state           = sw::ServerState::Maintenance;
    SW_EXPECT_FALSE( sw::ServerSelection::isCandidate( maintenance, query, kNowMs ) );
    sw::ServerSelectionQuery allowedQuery = query;
    allowedQuery._bIncludeMaintenance     = SW_TRUE;
    SW_EXPECT_TRUE( sw::ServerSelection::isCandidate( maintenance, allowedQuery, kNowMs ) );

    sw::ServerStatus chat  = makeStatus( 1, "kr", 0, 10 );
    chat._descriptor._kind = "chat";
    SW_EXPECT_FALSE( sw::ServerSelection::isCandidate( chat, query, kNowMs ) );

    sw::ServerSelectionQuery buildQuery = query;
    buildQuery._buildVersion            = 8;
    SW_EXPECT_FALSE( sw::ServerSelection::isCandidate( makeStatus( 1, "kr", 0, 10 ), buildQuery, kNowMs ) );
    buildQuery._buildVersion = 7;
    SW_EXPECT_TRUE( sw::ServerSelection::isCandidate( makeStatus( 1, "kr", 0, 10 ), buildQuery, kNowMs ) );

    SW_EXPECT_TRUE( sw::ServerSelection::isCandidate( makeStatus( 1, "kr", 0, 10 ), query, kNowMs + query._staleMs ) );
    SW_EXPECT_FALSE( sw::ServerSelection::isCandidate( makeStatus( 1, "kr", 0, 10 ), query, kNowMs + query._staleMs + 1 ) );

    sw::ServerSelectionQuery partyQuery = query;
    partyQuery._seatCount               = 3;
    SW_EXPECT_TRUE( sw::ServerSelection::isCandidate( makeStatus( 1, "kr", 7, 10 ), partyQuery, kNowMs ) );
    SW_EXPECT_FALSE( sw::ServerSelection::isCandidate( makeStatus( 1, "kr", 8, 10 ), partyQuery, kNowMs ) );
    SW_EXPECT_FALSE( sw::ServerSelection::isCandidate( makeStatus( 1, "kr", 0, 0 ), query, kNowMs ) ); // 정원 0 은 받지 않는다(나눗셈도 하지 않는다)
}

/**
 * @brief [ServerDirectoryTest] 고르기: 같은 지역이 더 차 있어도 먼저, 같은 지역이 없으면 다른 지역(허용할 때만), 같은 쪽 안에서는 찬 비율 → 서버 id 순
 */
SW_TEST_CASE( ServerDirectoryTest, PickPrefersSameRegionThenFillThenServerID )
{
    sw::vector<sw::ServerStatus> listStatus;
    listStatus.push_back( makeStatus( 30, "eu", 0, 100 ) );  // 0: 다른 지역, 비었다
    listStatus.push_back( makeStatus( 20, "kr", 50, 100 ) ); // 1: 같은 지역, 절반
    listStatus.push_back( makeStatus( 10, "kr", 25, 50 ) );  // 2: 같은 지역, 절반(찬 비율이 같다 — id 가 작다)
    listStatus.push_back( makeStatus( 40, "kr", 90, 100 ) ); // 3: 같은 지역, 거의 찼다

    int32 index = -1;
    SW_REQUIRE_TRUE( sw::ServerSelection::pickServer( listStatus, makeQuery( "kr" ), kNowMs, index ) );
    SW_EXPECT_EQUAL( 2, index );

    listStatus[2]._load = 26; // 이제 2 가 조금 더 찼다
    SW_REQUIRE_TRUE( sw::ServerSelection::pickServer( listStatus, makeQuery( "kr" ), kNowMs, index ) );
    SW_EXPECT_EQUAL( 1, index );

    SW_REQUIRE_TRUE( sw::ServerSelection::pickServer( listStatus, makeQuery( "us" ), kNowMs, index ) );
    SW_EXPECT_EQUAL( 0, index ); // 같은 지역이 없다 — 전체에서 가장 빈 것

    sw::ServerSelectionQuery localOnly = makeQuery( "us" );
    localOnly._bAllowOtherRegion       = SW_FALSE;
    SW_EXPECT_FALSE( sw::ServerSelection::pickServer( listStatus, localOnly, kNowMs, index ) );
    SW_EXPECT_EQUAL( -1, index );

    SW_EXPECT_FALSE( sw::ServerSelection::pickServer( {}, makeQuery( "kr" ), kNowMs, index ) );
    SW_EXPECT_EQUAL( -1, index );
}

/**
 * @brief [ServerDirectoryTest] 등록: 종류 · 지역 이름 · 서버 id · 라우터 · 주소 길이가 틀리면 오류와 함께 false 이고 아무것도 쓰지 않는다
 */
SW_TEST_CASE( ServerDirectoryTest, RegistrationRejectsInvalidDescriptor )
{
    SW_EXPECT_LOG( "ServerRegistration.InvalidDescriptor" );
    DirectoryRig               rig;
    const sw::ServerDescriptor valid = makeStatus( 5, "kr", 0, 10 )._descriptor;

    sw::ServerRegistration registration;
    sw::ServerDescriptor   badKind = valid;
    badKind._kind                  = "Game";
    SW_EXPECT_FALSE( registration.initialize( &rig._router, badKind ) );
    sw::ServerDescriptor badRegion = valid;
    badRegion._region              = "";
    SW_EXPECT_FALSE( registration.initialize( &rig._router, badRegion ) );
    sw::ServerDescriptor noID = valid;
    noID._serverID            = 0;
    SW_EXPECT_FALSE( registration.initialize( &rig._router, noID ) );
    sw::ServerDescriptor longAddress = valid;
    longAddress._address             = sw::string( static_cast<size_t>( sw::ServerRecord::kMaxAddressSize ) + 1, 'a' );
    SW_EXPECT_FALSE( registration.initialize( &rig._router, longAddress ) );
    SW_EXPECT_FALSE( registration.initialize( nullptr, valid ) );
    sw::ServerDescriptor noPort = valid;
    noPort._portCount           = 0; // 읽는 쪽이 모두 거절할 기록이다
    SW_EXPECT_FALSE( registration.initialize( &rig._router, noPort ) );
    sw::ServerDescriptor tooManyPorts = valid;
    tooManyPorts._portCount           = static_cast<uint8>( sw::ServerDescriptor::kMaxPortCount + 1 );
    SW_EXPECT_FALSE( registration.initialize( &rig._router, tooManyPorts ) );
    sw::ServerDescriptor portWraps = valid;
    portWraps._port                = 65534;
    portWraps._portCount           = 3; // 65534 · 65535 · 0 — 끝 포트가 넘친다
    SW_EXPECT_FALSE( registration.initialize( &rig._router, portWraps ) );

    registration.update( sw::UnixMilliseconds{ kNowMs } );
    SW_EXPECT_EQUAL( 0, rig._store.getPendingCount() ); // 초기화하지 못한 등록은 쓰지 않는다
}

/**
 * @brief [ServerDirectoryTest] 등록의 쓰기: 처음 · 상태나 부하가 바뀐 즉시 · 하트비트 주기마다 쓰고, 그 사이에는 쓰지 않으며, 같은 값으로 바꾸는 것은 바뀜이 아니다
 */
SW_TEST_CASE( ServerDirectoryTest, RegistrationWritesOnChangeAndHeartbeatOnly )
{
    DirectoryRig           rig;
    sw::ServerRegistration registration;
    SW_REQUIRE_TRUE( registration.initialize( &rig._router, makeStatus( 5, "kr", 0, 10 )._descriptor ) );
    SW_EXPECT_TRUE( registration.getStatus()._state == sw::ServerState::Starting );

    const sw::UnixMilliseconds start{ kNowMs };
    registration.update( start );
    SW_EXPECT_EQUAL( kNowMs, registration.getStatus()._heartbeatMs );

    registration.update( start + 100 );
    SW_EXPECT_EQUAL( kNowMs, registration.getStatus()._heartbeatMs ); // 주기 전이고 바뀐 것이 없다

    registration.setState( sw::ServerState::Open );
    registration.update( start + 200 );
    SW_EXPECT_EQUAL( kNowMs + 200, registration.getStatus()._heartbeatMs );

    registration.setLoad( 0 );                      // 같은 값
    registration.setState( sw::ServerState::Open ); // 같은 값
    registration.update( start + 300 );
    SW_EXPECT_EQUAL( kNowMs + 200, registration.getStatus()._heartbeatMs );

    registration.setLoad( 3 );
    registration.update( start + 400 );
    SW_EXPECT_EQUAL( kNowMs + 400, registration.getStatus()._heartbeatMs );

    registration.update( start + 400 + sw::ServerRegistration::kHeartbeatPeriodMs - 1 );
    SW_EXPECT_EQUAL( kNowMs + 400, registration.getStatus()._heartbeatMs );
    registration.update( start + 400 + sw::ServerRegistration::kHeartbeatPeriodMs );
    SW_EXPECT_EQUAL( kNowMs + 400 + sw::ServerRegistration::kHeartbeatPeriodMs, registration.getStatus()._heartbeatMs );

    // 캐시에 쓴 기록은 마지막 상태다(쓰기 답을 라우터가 먼저 비운 뒤 앞에서 직접 읽는다)
    rig.pumpAll();
    (void)rig._store.submit( sw::EphemeralRequest::makeGet( sw::ServerRecord::makeRecordKey( 5 ) ) );
    sw::vector<sw::EphemeralReply> listReply;
    (void)rig._store.pollReplies( listReply );
    SW_REQUIRE_EQUAL( size_t( 1 ), listReply.size() );
    sw::ServerStatus stored;
    SW_REQUIRE_TRUE( sw::ServerRecord::decode( listReply[0]._value, stored ) );
    SW_EXPECT_EQUAL( 3, stored._load );
    SW_EXPECT_TRUE( stored._state == sw::ServerState::Open );
}

/**
 * @brief [ServerDirectoryTest] 읽는 쪽: 등록한 서버들의 스냅숏을 만들고, 고른 인원을 다음 읽기까지 더하며, 정상 종료한 서버는 다음 읽기에서 빠진다
 */
SW_TEST_CASE( ServerDirectoryTest, ReaderSnapshotsRegisteredServersAndCountsPicks )
{
    DirectoryRig               rig;
    const sw::UnixMilliseconds now{ kNowMs };
    sw::ServerRegistration     first;
    sw::ServerRegistration     second;
    SW_REQUIRE_TRUE( first.initialize( &rig._router, makeStatus( 1, "kr", 0, 10 )._descriptor ) );
    SW_REQUIRE_TRUE( second.initialize( &rig._router, makeStatus( 2, "kr", 0, 10 )._descriptor ) );
    first.setState( sw::ServerState::Open );
    second.setState( sw::ServerState::Open );
    second.setLoad( 1 );
    first.update( now );
    second.update( now );
    rig.pumpAll();

    sw::ServerRegistryReader reader;
    reader.initialize( &rig._router, "game", 2000 );
    reader.update( now );
    SW_EXPECT_TRUE( reader.isRefreshing() );
    rig.pumpAll();
    SW_EXPECT_FALSE( reader.isRefreshing() );
    SW_EXPECT_EQUAL( uint64( 1 ), reader.getRefreshCount() );
    SW_REQUIRE_EQUAL( size_t( 2 ), reader.getSnapshot().size() );

    sw::ServerStatus picked;
    SW_REQUIRE_TRUE( reader.pickServer( makeQuery( "kr" ), now, picked ) );
    SW_EXPECT_EQUAL( uint64( 1 ), picked._descriptor._serverID ); // 1 이 비었다
    SW_EXPECT_EQUAL( 1, picked._load );                           // 고른 인원을 더했다
    SW_REQUIRE_TRUE( reader.pickServer( makeQuery( "kr" ), now, picked ) );
    SW_EXPECT_EQUAL( uint64( 1 ), picked._descriptor._serverID ); // 둘 다 1 — 동률은 id 가 작은 쪽
    SW_REQUIRE_TRUE( reader.pickServer( makeQuery( "kr" ), now, picked ) );
    SW_EXPECT_EQUAL( uint64( 2 ), picked._descriptor._serverID );

    // 주기 전에는 다시 읽지 않는다
    reader.update( now + 1999 );
    SW_EXPECT_FALSE( reader.isRefreshing() );

    second.shutdown();
    reader.requestRefresh();
    reader.update( now + 2000 );
    rig.pumpAll();
    SW_EXPECT_EQUAL( uint64( 2 ), reader.getRefreshCount() );
    SW_REQUIRE_EQUAL( size_t( 1 ), reader.getSnapshot().size() );
    SW_EXPECT_EQUAL( uint64( 1 ), reader.getSnapshot()[0]._descriptor._serverID );
    SW_EXPECT_EQUAL( 0, reader.getSnapshot()[0]._load ); // 새 스냅숏은 고른 인원을 잊는다(기록의 부하가 원본이다)
}

/**
 * @brief [ServerDirectoryTest] 하트비트가 멈춰 기록이 시한으로 사라진 서버: 스냅숏에서 빠지고, 읽는 쪽이 색인의 낡은 멤버를 지운다
 */
SW_TEST_CASE( ServerDirectoryTest, ReaderDropsExpiredServerAndCleansIndex )
{
    DirectoryRig               rig;
    const sw::UnixMilliseconds now{ kNowMs };
    sw::ServerRegistration     alive;
    sw::ServerRegistration     dead;
    SW_REQUIRE_TRUE( alive.initialize( &rig._router, makeStatus( 1, "kr", 0, 10 )._descriptor ) );
    SW_REQUIRE_TRUE( dead.initialize( &rig._router, makeStatus( 2, "kr", 0, 10 )._descriptor ) );
    alive.update( now );
    dead.update( now );
    rig.pumpAll();

    rig._clock.advanceMilliseconds( sw::ServerRegistration::kRecordTTLMs );
    alive.update( now + sw::ServerRegistration::kRecordTTLMs ); // 살아 있는 쪽만 하트비트를 썼다
    rig.pumpAll();

    sw::ServerRegistryReader reader;
    reader.initialize( &rig._router, "game", 2000 );
    reader.update( now + sw::ServerRegistration::kRecordTTLMs );
    rig.pumpAll();
    SW_REQUIRE_EQUAL( size_t( 1 ), reader.getSnapshot().size() );
    SW_EXPECT_EQUAL( uint64( 1 ), reader.getSnapshot()[0]._descriptor._serverID );

    (void)rig._store.submit( sw::EphemeralRequest::makeScoreRank( sw::ServerRecord::makeIndexKey( "game" ), sw::ServerRecord::makeMember( 2 ) ) );
    (void)rig._store.submit( sw::EphemeralRequest::makeScoreRank( sw::ServerRecord::makeIndexKey( "game" ), sw::ServerRecord::makeMember( 1 ) ) );
    sw::vector<sw::EphemeralReply> listReply;
    (void)rig._store.pollReplies( listReply );
    SW_REQUIRE_EQUAL( size_t( 2 ), listReply.size() );
    SW_EXPECT_TRUE( listReply[0]._result == sw::EphemeralResult::NotFound );
    SW_EXPECT_TRUE( listReply[1]._result == sw::EphemeralResult::Ok );
}

/**
 * @brief [ServerDirectoryTest] 읽는 쪽을 먼저 내리면 기다리던 답이 사라진 읽기를 부르지 않고(라우터의 주인 몫), 스냅숏을 비운다
 */
SW_TEST_CASE( ServerDirectoryTest, ReaderShutdownCancelsPendingReads )
{
    DirectoryRig               rig;
    const sw::UnixMilliseconds now{ kNowMs };
    sw::ServerRegistration     registration;
    SW_REQUIRE_TRUE( registration.initialize( &rig._router, makeStatus( 1, "kr", 0, 10 )._descriptor ) );
    registration.update( now );
    rig.pumpAll();

    {
        sw::ServerRegistryReader reader;
        reader.initialize( &rig._router, "game", 2000 );
        reader.update( now );
        SW_EXPECT_EQUAL( 1, rig._router.getPendingCount() );
        reader.shutdown();
        SW_EXPECT_EQUAL( 0, rig._router.getPendingCount() );
        SW_EXPECT_TRUE( reader.getSnapshot().empty() );
        SW_EXPECT_FALSE( reader.isRefreshing() );
        reader.update( now + 10000 ); // 내린 뒤에는 읽지 않는다
        SW_EXPECT_EQUAL( 0, rig._router.getPendingCount() );
    }
    rig.pumpAll(); // 지워진 읽는 쪽의 답이 와도 부르지 않는다
    SW_EXPECT_EQUAL( 0, rig._router.getPendingCount() );
}
