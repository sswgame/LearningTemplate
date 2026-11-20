// HostServiceSetTest — 호스트 서비스가 설 순서를 표만으로 적는다(`describeDeclaredHostServiceOrder`): 의존 선언대로 서고, 집합에 없는 의존은 정렬 오류다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/HostServiceSet.h"

using namespace sw;

namespace
{
    struct HostServiceSetTestUtil
    {
        /** @brief `A > B > C` 글에서 @p pFirst 가 @p pSecond 보다 앞에 있으면 true 입니다(둘 다 있어야 한다). */
        static bool isBefore( const string& order, const utf8* pFirst, const utf8* pSecond )
        {
            const size_t first  = order.find( pFirst );
            const size_t second = order.find( pSecond );
            return first != string::npos && second != string::npos && first < second;
        }
    };
} // namespace

/**
 * @brief [HostServiceSetTest] App · Server 의 선언 순서가 의존을 지킨다 — 게임은 콘솔보다 먼저, 서버 문맥은 관측 뒤 · 게임 앞, 원격 채널은 콘솔 뒤에 선다
 * @details 몸통 없이(세우기 함수가 늘 성공) 표만으로 정렬한 글을 본다. 변이: `HostService::getSubsystemDependents` 의 서버 문맥 줄을 빼면 `ServiceContext > Game` 단언이 진다.
 */
SW_TEST_CASE( HostServiceSetTest, DeclaredOrderFollowsTheDependencies )
{
    using Util = HostServiceSetTestUtil;

    const string client = describeDeclaredHostServiceOrder( kArrClientHostService );
    SW_EXPECT_TRUE( Util::isBefore( client, HostServiceName::kGame, HostServiceName::kDevConsole ) );
    SW_EXPECT_TRUE( client.find( HostServiceName::kUserSettings ) != string::npos );
    SW_EXPECT_TRUE( client.find( HostServiceName::kRuntimeUIHooks ) != string::npos );

    const string server = describeDeclaredHostServiceOrder( kArrServerHostService );
    SW_EXPECT_TRUE( Util::isBefore( server, HostServiceName::kObservability, HostServiceName::kCrashReports ) );
    SW_EXPECT_TRUE( Util::isBefore( server, HostServiceName::kObservability, HostServiceName::kServiceContext ) );
    SW_EXPECT_TRUE( Util::isBefore( server, HostServiceName::kServiceContext, HostServiceName::kGame ) );
    SW_EXPECT_TRUE( Util::isBefore( server, HostServiceName::kGame, HostServiceName::kDevConsole ) );
    SW_EXPECT_TRUE( Util::isBefore( server, HostServiceName::kDevConsole, HostServiceName::kDebugChannel ) );
}

/**
 * @brief [HostServiceSetTest] 먼저 서야 하는 서비스가 집합에 없으면 순서 대신 정렬 오류 글이 나온다 — 콘솔만 넣으면 게임이 없다
 */
SW_TEST_CASE( HostServiceSetTest, MissingDependencyIsReportedInsteadOfAnOrder )
{
    constexpr HostServiceID kArrConsoleOnly[] = { HostServiceID::DevConsole };
    const string            text              = describeDeclaredHostServiceOrder( kArrConsoleOnly );
    SW_EXPECT_FALSE( text.empty() );
    SW_EXPECT_TRUE( text.find( " > " ) == string::npos );
}
