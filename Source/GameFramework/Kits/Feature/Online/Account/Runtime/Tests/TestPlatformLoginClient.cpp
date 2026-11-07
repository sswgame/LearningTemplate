// PlatformLoginClientTest — 개발 · 테스트용 외부 로그인 클라이언트(`FakePlatformLoginClient`): 정한 제공자는 다음 `update` 에 그 티켓을, 정하지 않은 제공자는 실패를 돌려주고,
// 결과는 요청마다 꼭 한 번 나온다(인터페이스 약속). PC 의 실제 길(`LoopbackPkceLoginClient`)은 LoopbackPkceLoginClientTest 가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/PlatformLoginClient.h"

using namespace sw;

namespace
{
    constexpr MonotonicMilliseconds kNowMs{ 1000 };

    struct PlatformLoginTestUtil
    {
        static string toText( const vector<uint8>& bytes ) { return string( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() ); }
    };
} // namespace

/** @brief [PlatformLoginClientTest] 정한 제공자는 그 티켓으로 성공하고, 결과는 다음 update 에 한 번 나온다 */
SW_TEST_CASE( PlatformLoginClientTest, PresetProviderSucceedsOnNextUpdate )
{
    FakePlatformLoginClient client;
    client.setTicket( "google", "id-token|nonce" );
    const uint64 requestID = client.beginLogin( "google", kNowMs );
    SW_EXPECT_TRUE( requestID != 0 );

    vector<PlatformLoginClientResult> listResult;
    SW_EXPECT_EQUAL( 0, client.pollResults( listResult ) ); // update 전에는 끝나지 않는다
    client.update( kNowMs );
    SW_REQUIRE_EQUAL( 1, client.pollResults( listResult ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listResult.size() );
    SW_EXPECT_EQUAL( requestID, listResult[0]._requestID );
    SW_EXPECT_EQUAL( string( "google" ), listResult[0]._provider );
    SW_EXPECT_TRUE( listResult[0]._bSucceeded == SW_TRUE );
    SW_EXPECT_TRUE( listResult[0]._bCancelled == SW_FALSE );
    SW_EXPECT_EQUAL( string( "id-token|nonce" ), PlatformLoginTestUtil::toText( listResult[0]._ticket ) );

    listResult.clear();
    client.update( kNowMs );
    SW_EXPECT_EQUAL( 0, client.pollResults( listResult ) ); // 두 번 나오지 않는다
}

/** @brief [PlatformLoginClientTest] 정하지 않은 제공자는 실패 이유와 함께 끝나고 티켓이 비어 있다 */
SW_TEST_CASE( PlatformLoginClientTest, UnknownProviderFailsWithReason )
{
    FakePlatformLoginClient client;
    client.setTicket( "google", "id-token|nonce" );
    const uint64 requestID = client.beginLogin( "steam", kNowMs );
    client.update( kNowMs );

    vector<PlatformLoginClientResult> listResult;
    SW_REQUIRE_EQUAL( 1, client.pollResults( listResult ) );
    SW_EXPECT_EQUAL( requestID, listResult[0]._requestID );
    SW_EXPECT_TRUE( listResult[0]._bSucceeded == SW_FALSE );
    SW_EXPECT_TRUE( listResult[0]._ticket.empty() );
    SW_EXPECT_FALSE( listResult[0]._failureText.empty() );
}

/** @brief [PlatformLoginClientTest] 같은 제공자를 다시 정하면 새 티켓이고, 동시에 연 요청은 id 가 달라 결과를 가른다 */
SW_TEST_CASE( PlatformLoginClientTest, ResetTicketAndDistinctRequests )
{
    FakePlatformLoginClient client;
    client.setTicket( "apple", "old" );
    client.setTicket( "apple", "new" );
    const uint64 firstID  = client.beginLogin( "apple", kNowMs );
    const uint64 secondID = client.beginLogin( "apple", kNowMs );
    SW_EXPECT_TRUE( firstID != secondID );
    client.update( kNowMs );

    vector<PlatformLoginClientResult> listResult;
    SW_REQUIRE_EQUAL( 2, client.pollResults( listResult ) );
    SW_EXPECT_EQUAL( firstID, listResult[0]._requestID );
    SW_EXPECT_EQUAL( secondID, listResult[1]._requestID );
    for ( const PlatformLoginClientResult& result : listResult )
    {
        SW_EXPECT_EQUAL( string( "new" ), PlatformLoginTestUtil::toText( result._ticket ) );
    }
}
