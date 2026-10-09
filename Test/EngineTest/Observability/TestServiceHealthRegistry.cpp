#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "Engine/Observability/ServiceHealthRegistry.h"

#include "TestFramework/TestFramework.h"

// 서버 상태 — 틱이 멈추면 살아 있지 않음, 준비됨은 필수 검사 · 비우는 중을 본다, 보고 줄.

using namespace sw;

SW_TEST_CASE( ServiceHealthRegistryTest, LivenessFollowsTheTick )
{
    ServiceHealthRegistry health;
    health.setLivenessTimeoutMs( 1000 );
    SW_EXPECT_FALSE( health.isLive( 0 ) ); // 아직 한 번도 돌지 않았다
    health.markTick( 5000 );
    SW_EXPECT_TRUE( health.isLive( 6000 ) );
    SW_EXPECT_FALSE( health.isLive( 6001 ) );
    health.markTick( 6001 );
    SW_EXPECT_TRUE( health.isLive( 6001 ) );
}

SW_TEST_CASE( ServiceHealthRegistryTest, ReadinessNeedsRequiredChecksAndNotDraining )
{
    ServiceHealthRegistry health;
    const int32           store = health.registerCheck( "service_store", true );
    const int32           cache = health.registerCheck( "ephemeral_store", false );
    SW_EXPECT_FALSE( health.isReady( 100 ) ); // 틱 없음
    health.markTick( 100 );
    SW_EXPECT_FALSE( health.isReady( 100 ) ); // 필수 검사가 처음엔 Failing
    health.setCheck( store, HealthState::Ok, "" );
    SW_EXPECT_TRUE( health.isReady( 100 ) ); // 필수 아닌 cache 는 Failing 이어도 된다
    health.setCheck( store, HealthState::Degraded, "slow" );
    SW_EXPECT_TRUE( health.isReady( 100 ) );
    health.setCheck( cache, HealthState::Failing, "unreachable" );
    SW_EXPECT_TRUE( health.isReady( 100 ) );
    health.setDraining( true );
    SW_EXPECT_TRUE( health.isDraining() );
    SW_EXPECT_FALSE( health.isReady( 100 ) );
    SW_EXPECT_TRUE( health.isLive( 100 ) );     // 비우는 중에도 살아 있다 — 감시자가 재시작하지 않게
    health.setCheck( 99, HealthState::Ok, "" ); // 범위 밖은 무시
}

SW_TEST_CASE( ServiceHealthRegistryTest, ReportListsEveryCheck )
{
    ServiceHealthRegistry health;
    const int32           store = health.registerCheck( "service_store", true );
    (void)health.registerCheck( "ephemeral_store", false );
    health.setCheck( store, HealthState::Degraded, "pending=12000" );
    health.markTick( 10 );
    string text;
    health.writeReport( text, 10 );
    SW_EXPECT_TRUE( sw::StringUtil::startsWith( text, "live 1\nready 1\ndraining 0\n" ) );
    SW_EXPECT_TRUE( text.find( "check service_store degraded pending=12000\n" ) != string::npos );
    SW_EXPECT_TRUE( text.find( "check ephemeral_store failing\n" ) != string::npos );
}
