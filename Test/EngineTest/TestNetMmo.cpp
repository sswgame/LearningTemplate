#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Network/NetHost.h"
#include "Core/Network/NetTransport.h"

#include "GameFramework/Kits/Network/NetMmo/MmoReplicator.h"

#include "TestFramework/TestFramework.h"

// MMO 네트워크 키트 — 격자 반경 질의, 관심 영역의 들어옴 · 나감(히스테리시스), 대역폭 예산 안에서 가까운 것을 더 자주 갱신,
// 멀어도 굶지 않음, 늘 보이기 정책.

using namespace sw;

namespace
{
    class PartyPolicy final : public IInterestPolicy
    {
    public:
        bool hasAlwaysRelevant() const override { return true; }
        bool isAlwaysRelevant( int32 connectionId, const MmoEntity& entity ) const override
        {
            (void)connectionId;
            return entity._typeId == 9; // 파티원
        }
    };
} // namespace

SW_TEST_CASE( NetMmoTest, InterestGridQueriesOnlyNearbyCells )
{
    InterestGrid grid;
    grid.initialize( 10.0f );
    grid.setPosition( 1, float3{ 5.0f, 0.0f, 5.0f } );
    grid.setPosition( 2, float3{ 14.0f, 0.0f, 5.0f } );
    grid.setPosition( 3, float3{ 100.0f, 0.0f, 100.0f } );
    grid.setPosition( 4, float3{ -3.0f, 0.0f, -3.0f } );
    vector<uint32> listFound;
    grid.queryRadius( float3{ 5.0f, 0.0f, 5.0f }, 12.0f, listFound );
    std::sort( listFound.begin(), listFound.end() );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( listFound.size() ) ); // 1 · 2 · 4(음수 칸)
    grid.setPosition( 2, float3{ 90.0f, 0.0f, 95.0f } );          // 칸을 옮긴다
    grid.queryRadius( float3{ 95.0f, 0.0f, 95.0f }, 10.0f, listFound );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( listFound.size() ) );
    grid.remove( 3 );
    grid.queryRadius( float3{ 95.0f, 0.0f, 95.0f }, 10.0f, listFound );
    SW_EXPECT_EQUAL( 1, static_cast<int32>( listFound.size() ) );
}

SW_TEST_CASE( NetMmoTest, ObserversSeeNearbyEntitiesWithinBudgetAndHysteresis )
{
    LoopbackNetwork    network( 9u );
    LoopbackConditions conditions;
    conditions._latency  = 0.03f;
    conditions._lossRate = 0.05f;
    NetHostSettings hostSettings;
    hostSettings._sendInterval = 1.0f / 20.0f;
    NetHost serverHost;
    NetHost clientHost;
    serverHost.initialize( network.createEndpoint( 4000 ), hostSettings );
    clientHost.initialize( network.createEndpoint( 5000 ), hostSettings );
    (void)serverHost.listen();
    (void)clientHost.connect( NetAddress::makeLoopback( 4000 ) );
    float64 time = 0.0;
    for ( int32 frame = 0; frame < 60 && clientHost.getConnectionState( 0 ) != NetConnectionState::Connected; ++frame )
    {
        time += 1.0 / 60.0;
        serverHost.update( time );
        clientHost.update( time );
    }
    SW_ASSERT_TRUE( clientHost.getConnectionState( 0 ) == NetConnectionState::Connected );
    network.setConditions( conditions );

    PartyPolicy           policy;
    MmoReplicator         server;
    MmoClientView         view;
    MmoReplicatorSettings settings;
    settings._enterRadius       = 50.0f;
    settings._leaveRadius       = 60.0f;
    settings._updateBudgetBytes = 150;
    server.initialize( &serverHost, settings, &policy );

    // 400 개를 400 × 400 에 고르게. 플레이어(1000)는 (100, 100). 파티원(1001)은 멀리.
    for ( uint32 index = 0; index < 400; ++index )
        server.setEntity( MmoEntity{
            vector<uint8>{ 0 },
            float3{ static_cast<float32>( index % 20 ) * 20.0f, 0.0f, static_cast<float32>( index / 20 ) * 20.0f },
            index, 1, 1.0f
        } );
    server.setEntity( MmoEntity{
        vector<uint8>{},
        float3{ 100.0f, 0.0f, 100.0f },
        1000, 0, 1.0f
    } );
    server.setEntity( MmoEntity{
        vector<uint8>{},
        float3{ 380.0f, 0.0f, 380.0f },
        1001, 9, 1.0f
    } );
    server.setObserver( 0, 1000 );

    vector<uint8> buffer;
    vector<int32> listUpdateCount( 402, 0 );
    const auto    runFor = [&]( float64 seconds, bool bAnimate )
    {
        for ( float64 elapsed = 0.0; elapsed < seconds; elapsed += 1.0 / 20.0 )
        {
            time += 1.0 / 20.0;
            if ( bAnimate )
            {
                // 모두 상태가 매 틱 바뀐다(애니메이션 프레임).
                for ( uint32 index = 0; index < 400; ++index )
                    server.setEntity( MmoEntity{
                        vector<uint8>{ static_cast<uint8>( time * 20.0 ) },
                        float3{ static_cast<float32>( index % 20 ) * 20.0f, 0.0f, static_cast<float32>( index / 20 ) * 20.0f },
                        index, 1, 1.0f
                    } );
            }
            server.update( 1.0f / 20.0f );
            serverHost.update( time );
            clientHost.update( time );
            int32          connectionId = -1;
            NetChannelType channel      = NetChannelType::Unreliable;
            while ( clientHost.receiveMessage( connectionId, channel, buffer ) )
                SW_EXPECT_TRUE( view.handleMessage( buffer ) );
            vector<MmoClientEvent> listEvent;
            view.drainEvents( listEvent );
            for ( const MmoClientEvent& event : listEvent )
            {
                if ( event._kind == MmoClientEvent::Kind::Updated && event._entityId < listUpdateCount.size() )
                    ++listUpdateCount[event._entityId];
            }
        }
    };
    runFor( 2.0, false );
    // 반경 50 안(칸 20 간격 — 대략 π·50²/400 ≈ 20 개) + 자기 자신 + 파티원.
    const int32 visibleCount = view.getEntityCount();
    SW_EXPECT_TRUE( visibleCount > 15 && visibleCount < 40 );
    SW_EXPECT_EQUAL( visibleCount, server.getVisibleCount( 0 ) );
    SW_EXPECT_TRUE( view.findEntity( 1001 ) != nullptr ); // 멀어도 파티원
    SW_EXPECT_TRUE( view.findEntity( 399 ) == nullptr );  // (380, 380)

    // 모두 움직이면 예산 때문에 다 갱신하지 못한다 — 가까운 것이 더 자주.
    std::fill( listUpdateCount.begin(), listUpdateCount.end(), 0 );
    runFor( 4.0, true );
    const int32 nearCount = listUpdateCount[5 * 20 + 5]; // (100, 100) — 바로 옆
    const int32 farCount  = listUpdateCount[4 * 20 + 7]; // (140, 80) — 약 45 m, 반경 안 끝자락
    SW_EXPECT_TRUE( view.findEntity( 4 * 20 + 7 ) != nullptr );
    SW_EXPECT_TRUE( nearCount > 60 );
    SW_EXPECT_TRUE( farCount > 5 && farCount < nearCount / 2 );                                // 굶지는 않지만 덜 자주
    SW_EXPECT_TRUE( server.getSentUpdateCount() < static_cast<uint64>( visibleCount ) * 80u ); // 예산이 막았다

    // 히스테리시스 — 55 m 로 물러나도 나가지 않고(들어오는 반경 50 < 55 < 나가는 반경 60), 70 m 면 나간다.
    server.setEntity( MmoEntity{
        vector<uint8>{},
        float3{ 100.0f, 0.0f, 100.0f },
        1002, 1, 1.0f
    } );
    runFor( 0.5, false );
    SW_ASSERT_TRUE( view.findEntity( 1002 ) != nullptr );
    server.setEntity( MmoEntity{
        vector<uint8>{},
        float3{ 155.0f, 0.0f, 100.0f },
        1002, 1, 1.0f
    } );
    runFor( 0.5, false );
    SW_EXPECT_TRUE( view.findEntity( 1002 ) != nullptr );
    server.setEntity( MmoEntity{
        vector<uint8>{},
        float3{ 170.0f, 0.0f, 100.0f },
        1002, 1, 1.0f
    } );
    runFor( 0.5, false );
    SW_EXPECT_TRUE( view.findEntity( 1002 ) == nullptr );
    server.removeEntity( 1001 );
    runFor( 0.5, false );
    SW_EXPECT_TRUE( view.findEntity( 1001 ) == nullptr );
}
