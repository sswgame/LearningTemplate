#include "pch.h"

#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"

#include "TestFramework/TestFramework.h"

// 서비스 메서드 번호 — 키트마다 256 칸 표, 같은 번호 · 같은 영역을 둘이 걸면 거절(조립 실수를 기동에서 잡는다).

using namespace sw;

namespace
{
    class NullRequestHandler final : public INetRequestHandler
    {
    public:
        void onNetRequest( NetRequestServer& server, const NetRequestContext& context ) override
        {
            (void)server;
            (void)context;
        }
    };

    class RangeService final : public IOnlineService
    {
    public:
        explicit RangeService( uint16 range )
            : _range{ range }
        {
        }

        uint16 getMethodRange() const override { return _range; }
        uint32 getProtocolVersion() const override { return 1; }
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override
        {
            (void)host;
            (void)context;
            (void)body;
        }

    private:
        uint16 _range;
    };

    static_assert( OnlineMethodRange::isInRange( OnlineMethodRange::kAccount + 0x7F, OnlineMethodRange::kAccount ), "last method of a range" );
    static_assert( OnlineMethodRange::isInRange( OnlineMethodRange::kTrade, OnlineMethodRange::kAccount ) == false, "ranges do not overlap" );
    static_assert( OnlineMethodRange::isMethod( OnlineMethodRange::kChat + 0x7F ) && OnlineMethodRange::isMethod( OnlineMethodRange::kChat + 0x80 ) == false,
                   "0x80..0xFF are push kinds" );
} // namespace

SW_TEST_CASE( OnlineServiceTest, RegisterMethodRefusesADuplicate )
{
    NullRequestHandler first;
    NullRequestHandler second;
    NetRequestServer   server;
    SW_EXPECT_TRUE( server.registerMethod( 0x8001, &first ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "a second handler for one method number is refused" );
        SW_EXPECT_FALSE( server.registerMethod( 0x8001, &second ) );
    }
    server.unregisterMethod( 0x8001 );
    SW_EXPECT_TRUE( server.registerMethod( 0x8001, &second ) );

    OnlineServiceHost host;
    RangeService      account( OnlineMethodRange::kAccount );
    RangeService      sameRange( OnlineMethodRange::kAccount );
    RangeService      notABase( 0x0910 );
    RangeService      hostRange( OnlineMethodRange::kHost );
    SW_EXPECT_TRUE( host.registerService( &account ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "overlapping or misaligned method ranges are refused" );
        SW_EXPECT_FALSE( host.registerService( &sameRange ) );
        SW_EXPECT_FALSE( host.registerService( &notABase ) );
        SW_EXPECT_FALSE( host.registerService( &hostRange ) );
    }
}

SW_TEST_CASE( OnlineServiceTest, MethodRangeTableIsDisjoint )
{
    const uint16 arrRange[] = { OnlineMethodRange::kHost, OnlineMethodRange::kAccount, OnlineMethodRange::kServerDirectory, OnlineMethodRange::kTrade,
                                OnlineMethodRange::kEconomy, OnlineMethodRange::kMailbox, OnlineMethodRange::kAdmin, OnlineMethodRange::kChat,
                                OnlineMethodRange::kSocial, OnlineMethodRange::kLeaderboard, OnlineMethodRange::kMatchmaking, OnlineMethodRange::kLiveOps,
                                OnlineMethodRange::kGame };
    for ( size_t index = 0; index < sizeof( arrRange ) / sizeof( arrRange[0] ); ++index )
    {
        SW_EXPECT_EQUAL( OnlineMethodRange::getRangeBase( arrRange[index] ), arrRange[index] );
        for ( size_t other = index + 1; other < sizeof( arrRange ) / sizeof( arrRange[0] ); ++other )
            SW_EXPECT_FALSE( OnlineMethodRange::isInRange( arrRange[other], arrRange[index] ) );
    }
}
