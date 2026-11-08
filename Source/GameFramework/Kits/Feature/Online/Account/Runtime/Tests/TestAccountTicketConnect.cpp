// AccountTicketConnectTest — 접속 티켓 받기 도우미(`AccountTicketConnector`)의 입구 검사: 빈 칸 · 0 이하 시한 · `host:port` 가 아닌 주소는 연결을 열기 전에 이유와 함께 거절하고,
// 자격 없이 붙는 길로 대신하지 않는다. 로그인 → 티켓의 성공 길은 로그인 서버 두 프로세스 시나리오(OnlineKits F5 `net.ticketconnect`)가 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Network/Connection/NetEndpointSecurity.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Account/Runtime/API/AccountTicketConnect.h"

using namespace sw;

namespace
{
    struct TicketConnectTestUtil
    {
        static AccountTicketRequest makeRequest()
        {
            AccountTicketRequest request;
            request._loginAddress        = "127.0.0.1:7000";
            request._loginName           = "alice";
            request._password            = "secret";
            request._serverID            = "game-1";
            request._gameBuild           = "1.0.0";
            request._platform            = "windows";
            request._timeoutMilliseconds = 1000;
            return request;
        }

        /** @brief @p request 가 연결 전에 거절되고 이유가 @p pExpectedPart 를 담는지 봅니다. */
        static void expectRefused( AccountTicketRequest request, const utf8* pExpectedPart )
        {
            AccountTicketConnector connector;
            NetConnectCredentials  credentials;
            const auto             failed1 = connector.acquire( std::move( request ), credentials );
            SW_REQUIRE_FALSE( failed1.has_value() );
            const sw::string error1 = sw::string( failed1.error().message() ) + " " + sw::string( failed1.error().detail() );
            SW_EXPECT_TRUE( error1.find( pExpectedPart ) != string::npos );
        }
    };
    using Util = TicketConnectTestUtil;
} // namespace

/** @brief [AccountTicketConnectTest] 필수 칸 하나라도 비면 연결을 열지 않고 거절한다(빈 값으로 조용히 시도하지 않는다) */
SW_TEST_CASE( AccountTicketConnectTest, IncompleteRequestIsRefused )
{
    const utf8* const kNeeds = "needs the login address";
    {
        AccountTicketRequest request = Util::makeRequest();
        request._loginAddress.clear();
        Util::expectRefused( std::move( request ), kNeeds );
    }
    {
        AccountTicketRequest request = Util::makeRequest();
        request._loginName.clear();
        Util::expectRefused( std::move( request ), kNeeds );
    }
    {
        AccountTicketRequest request = Util::makeRequest();
        request._password.clear();
        Util::expectRefused( std::move( request ), kNeeds );
    }
    {
        AccountTicketRequest request = Util::makeRequest();
        request._serverID.clear();
        Util::expectRefused( std::move( request ), kNeeds );
    }
    {
        AccountTicketRequest request = Util::makeRequest();
        request._timeoutMilliseconds = 0;
        Util::expectRefused( std::move( request ), kNeeds );
    }
}

/** @brief [AccountTicketConnectTest] 로그인 주소는 host:port 여야 한다 — 포트 없는 주소는 이유를 적고 거절한다 */
SW_TEST_CASE( AccountTicketConnectTest, AddressWithoutPortIsRefused )
{
    AccountTicketRequest request = Util::makeRequest();
    request._loginAddress        = "not an address";
    Util::expectRefused( std::move( request ), "needs host:port" );
}

/** @brief [AccountTicketConnectTest] 거절된 뒤에도 닫기는 몇 번이든 된다(연결이 없다) */
SW_TEST_CASE( AccountTicketConnectTest, ShutdownWithoutConnectionIsSafe )
{
    AccountTicketConnector connector;
    NetConnectCredentials  credentials;
    AccountTicketRequest   request = Util::makeRequest();
    request._password.clear();
    const expected<void> acquired = connector.acquire( std::move( request ), credentials );
    connector.shutdown();
    connector.shutdown();
    SW_REQUIRE_FALSE( acquired.has_value() );
    SW_EXPECT_FALSE( acquired.error().message().empty() );
}
