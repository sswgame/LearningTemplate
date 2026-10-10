// 접속 분배 — 열림 · 종류 · 빌드 판 · 살아 있음 · 자리, 같은 지역 우선, 찬 비율 · id 순, 다른 지역 금지, 점검 허용.
#include "pch.h"

#include "GameFramework/Base/Online/Directory/ServerSelection.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    struct ServerSelectionTestInternal
    {
        static ServerStatus makeServer( uint64 serverID, string_view region, int32 load, int32 capacity, ServerState state = ServerState::Open, int64 heartbeatMs = 1000 )
        {
            ServerStatus status;
            status._descriptor._serverID     = serverID;
            status._descriptor._kind         = "game";
            status._descriptor._region       = string( region );
            status._descriptor._buildVersion = 7;
            status._descriptor._capacity     = capacity;
            status._load                     = load;
            status._state                    = state;
            status._heartbeatMs              = heartbeatMs;
            return status;
        }

        static ServerSelectionQuery makeQuery( string_view region )
        {
            ServerSelectionQuery query;
            query._kind         = "game";
            query._region       = string( region );
            query._buildVersion = 7;
            return query;
        }
    };
} // namespace

SW_TEST_CASE( ServerSelectionTest, PrefersSameRegionThenLowestFillThenLowestID )
{
    using Internal = ServerSelectionTestInternal;
    vector<ServerStatus> listStatus;
    listStatus.push_back( Internal::makeServer( 5, "eu", 0, 100 ) ); // 비었지만 다른 지역
    listStatus.push_back( Internal::makeServer( 4, "kr", 50, 100 ) );
    listStatus.push_back( Internal::makeServer( 6, "kr", 25, 50 ) ); // 4 와 같은 찬 비율 — id 가 크다
    listStatus.push_back( Internal::makeServer( 9, "kr", 80, 100 ) );
    int32 index = -1;
    SW_ASSERT_TRUE( ServerSelection::pickServer( listStatus, Internal::makeQuery( "kr" ), 2000, index ) );
    SW_EXPECT_EQUAL( listStatus[static_cast<size_t>( index )]._descriptor._serverID, uint64( 4 ) );

    listStatus.push_back( Internal::makeServer( 8, "kr", 10, 100 ) ); // 덜 찼다
    SW_ASSERT_TRUE( ServerSelection::pickServer( listStatus, Internal::makeQuery( "kr" ), 2000, index ) );
    SW_EXPECT_EQUAL( listStatus[static_cast<size_t>( index )]._descriptor._serverID, uint64( 8 ) );
}

SW_TEST_CASE( ServerSelectionTest, SkipsClosedStaleFullOtherKindAndOtherBuilds )
{
    using Internal = ServerSelectionTestInternal;
    vector<ServerStatus> listStatus;
    listStatus.push_back( Internal::makeServer( 1, "kr", 0, 100, ServerState::Draining ) );
    listStatus.push_back( Internal::makeServer( 2, "kr", 0, 100, ServerState::Open, -20000 ) ); // 하트비트가 낡았다
    listStatus.push_back( Internal::makeServer( 3, "kr", 100, 100 ) );                          // 가득
    ServerStatus otherBuild              = Internal::makeServer( 4, "kr", 0, 100 );
    otherBuild._descriptor._buildVersion = 8;
    listStatus.push_back( otherBuild );
    ServerStatus otherKind      = Internal::makeServer( 5, "kr", 0, 100 );
    otherKind._descriptor._kind = "chat";
    listStatus.push_back( otherKind );
    listStatus.push_back( Internal::makeServer( 6, "kr", 0, 100, ServerState::Starting ) );
    listStatus.push_back( Internal::makeServer( 7, "kr", 0, 100, ServerState::Maintenance ) );
    int32 index = 3;
    SW_EXPECT_FALSE( ServerSelection::pickServer( listStatus, Internal::makeQuery( "kr" ), 2000, index ) );
    SW_EXPECT_EQUAL( index, -1 );

    ServerSelectionQuery anyBuild = Internal::makeQuery( "kr" );
    anyBuild._buildVersion        = 0; // 판 무관이면 4 가 후보
    SW_ASSERT_TRUE( ServerSelection::pickServer( listStatus, anyBuild, 2000, index ) );
    SW_EXPECT_EQUAL( listStatus[static_cast<size_t>( index )]._descriptor._serverID, uint64( 4 ) );

    ServerSelectionQuery allowed = Internal::makeQuery( "kr" );
    allowed._bIncludeMaintenance = SW_TRUE; // 점검 허용 계정은 점검 상태 서버도
    SW_ASSERT_TRUE( ServerSelection::pickServer( listStatus, allowed, 2000, index ) );
    SW_EXPECT_EQUAL( listStatus[static_cast<size_t>( index )]._descriptor._serverID, uint64( 7 ) );
}

SW_TEST_CASE( ServerSelectionTest, FallsBackToOtherRegionOnlyWhenAllowedAndSeatsMustFit )
{
    using Internal = ServerSelectionTestInternal;
    vector<ServerStatus> listStatus;
    listStatus.push_back( Internal::makeServer( 1, "eu", 95, 100 ) );
    ServerSelectionQuery query = Internal::makeQuery( "kr" );
    int32                index = -1;
    SW_EXPECT_TRUE( ServerSelection::pickServer( listStatus, query, 2000, index ) );
    query._bAllowOtherRegion = SW_FALSE;
    SW_EXPECT_FALSE( ServerSelection::pickServer( listStatus, query, 2000, index ) );
    query._bAllowOtherRegion = SW_TRUE;
    query._seatCount         = 6; // 파티가 남은 자리(5)보다 크다
    SW_EXPECT_FALSE( ServerSelection::pickServer( listStatus, query, 2000, index ) );
    query._seatCount = 5;
    SW_EXPECT_TRUE( ServerSelection::pickServer( listStatus, query, 2000, index ) );
}
