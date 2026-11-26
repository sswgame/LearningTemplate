// PlaygroundOnlineFlowTest — 잃은 허브에 다시 붙을지의 판정(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Games/Playground/Online/PlaygroundOnlineFlow.h"

using namespace sw;

/** @brief [PlaygroundOnlineFlowTest] 시간 초과 · 줄 넘침 · 서버 재시작이면 다시 붙고, 점검 · 운영자 종료 · 까닭 없는 닫힘(추방 · 같은 계정의 다른 접속)이면 붙지 않는다 */
SW_TEST_CASE( PlaygroundOnlineFlowTest, ReconnectsTheHubOnlyWhenTheServerComesBack )
{
    const auto makeNotification = []( GameShutdownReason reason )
    {
        GameShutdownNotice notification;
        notification._reason = reason;
        return optional<GameShutdownNotice>{ notification };
    };
    SW_EXPECT_TRUE( PlaygroundOnlineFlow::shouldReconnectHub( NetDisconnectReason::Timeout, nullopt ) );
    SW_EXPECT_TRUE( PlaygroundOnlineFlow::shouldReconnectHub( NetDisconnectReason::ReceiveOverflow, nullopt ) ); // 앱이 멈춘 동안 신뢰 메시지가 넘쳤다
    SW_EXPECT_TRUE( PlaygroundOnlineFlow::shouldReconnectHub( NetDisconnectReason::ReliableOverflow, nullopt ) );
    SW_EXPECT_TRUE( PlaygroundOnlineFlow::shouldReconnectHub( NetDisconnectReason::Remote, makeNotification( GameShutdownReason::ServerRestart ) ) );
    SW_EXPECT_FALSE( PlaygroundOnlineFlow::shouldReconnectHub( NetDisconnectReason::Remote, nullopt ) );
    SW_EXPECT_FALSE( PlaygroundOnlineFlow::shouldReconnectHub( NetDisconnectReason::Timeout, makeNotification( GameShutdownReason::Maintenance ) ) );
    SW_EXPECT_FALSE( PlaygroundOnlineFlow::shouldReconnectHub( NetDisconnectReason::Remote, makeNotification( GameShutdownReason::ServerStopped ) ) );
}
