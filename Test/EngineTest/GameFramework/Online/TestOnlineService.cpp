#include "pch.h"

#include "Core/Network/BitStream.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineReplyUtil.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"

#include "TestFramework/TestFramework.h"

// 서비스 메서드 번호 — 키트마다 256 칸 표, 같은 번호 · 같은 영역을 둘이 걸면 거절(조립 실수를 기동에서 잡는다).

using namespace sw;

namespace
{
    enum class SampleOnlineResult : uint8
    {
        Ok,
        NotSignedIn,
        Unavailable,
        Count
    };

    class NullRequestHandler final : public INetRequestHandler
    {
    public:
        void onNetRequest( NetRequestServer& server, const NetRequestContext& context ) override
        {
            (void)server;
            (void)context;
        }
    };

    class RangeService final : public IOnlineHostService
    {
    public:
        explicit RangeService( const OnlineProtocolEntry& protocol )
            : _pProtocol{ &protocol }
        {
        }

        const OnlineProtocolEntry& getProtocol() const override { return *_pProtocol; }
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override
        {
            (void)host;
            (void)context;
            (void)body;
        }

    private:
        const OnlineProtocolEntry* _pProtocol;
    };

    // 영역 확인용 항목들이다. 항목은 만들면 레지스트리에 오르므로 이름 공간 범위에 둔다.
    const OnlineProtocolEntry kAccountRangeProtocol{ "OnlineServiceTest.Account", OnlineMethodRange::kAccount, 1 };
    const OnlineProtocolEntry kSameRangeProtocol{ "OnlineServiceTest.SameRange", OnlineMethodRange::kAccount, 1 };
    const OnlineProtocolEntry kNotABaseProtocol{ "OnlineServiceTest.NotABase", 0x0910, 1 };
    const OnlineProtocolEntry kHostRangeProtocol{ "OnlineServiceTest.HostRange", OnlineMethodRange::kHost, 1 };

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
    RangeService      account( kAccountRangeProtocol );
    RangeService      sameRange( kSameRangeProtocol );
    RangeService      notABase( kNotABaseProtocol );
    RangeService      hostRange( kHostRangeProtocol );
    SW_EXPECT_TRUE( host.registerHostService( &account ) );
    {
        SW_TEST_DEFENSIVE_SCOPE( "overlapping or misaligned method ranges are refused" );
        SW_EXPECT_FALSE( host.registerHostService( &sameRange ) );
        SW_EXPECT_FALSE( host.registerHostService( &notABase ) );
        SW_EXPECT_FALSE( host.registerHostService( &hostRange ) );
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
        {
            SW_EXPECT_FALSE( OnlineMethodRange::isInRange( arrRange[other], arrRange[index] ) );
        }
    }
}

/**
 * @brief [OnlineProtocolTest] 오류 코드는 키트 표에서 결과로 바뀌고, 표에 없는 코드는 키트가 준 기본값이다
 * @details 키트마다 받는 코드가 달라 표와 기본값(대개 Unavailable)은 키트가 가진다. 키트마다 적던 switch 를 대신한다.
 */
SW_TEST_CASE( OnlineProtocolTest, ErrorCodeMapsThroughKitTable )
{
    static constexpr OnlineErrorResult<SampleOnlineResult> kArrErrorResult[] = {
        {             OnlineError::kOk,          SampleOnlineResult::Ok},
        {OnlineError::kUnauthenticated, SampleOnlineResult::NotSignedIn},
    };
    SW_EXPECT_TRUE( OnlineError::toResult( OnlineError::kOk, kArrErrorResult, SampleOnlineResult::Unavailable ) == SampleOnlineResult::Ok );
    SW_EXPECT_TRUE( OnlineError::toResult( OnlineError::kUnauthenticated, kArrErrorResult, SampleOnlineResult::Unavailable ) ==
                    SampleOnlineResult::NotSignedIn );
    SW_EXPECT_TRUE( OnlineError::toResult( OnlineError::kInternal, kArrErrorResult, SampleOnlineResult::Unavailable ) ==
                    SampleOnlineResult::Unavailable );
}

/**
 * @brief [OnlineProtocolTest] 응답 첫 칸의 결과(varuint)는 Count 미만만 읽고, 넘거나 몸이 모자라면 거절한다
 */
SW_TEST_CASE( OnlineProtocolTest, ReadEnumRejectsOutOfRangeAndShortBody )
{
    BitWriter writer;
    writer.writeVarUint( static_cast<uint64>( SampleOnlineResult::NotSignedIn ) );
    writer.writeVarUint( static_cast<uint64>( SampleOnlineResult::Count ) );

    BitReader          reader( writer.getBytes().data(), writer.getByteCount() );
    SampleOnlineResult result = SampleOnlineResult::Ok;
    SW_EXPECT_TRUE( OnlineReplyUtil::readEnum( reader, SampleOnlineResult::Count, result ) );
    SW_EXPECT_TRUE( result == SampleOnlineResult::NotSignedIn );
    SW_EXPECT_FALSE( OnlineReplyUtil::readEnum( reader, SampleOnlineResult::Count, result ) );
    SW_EXPECT_TRUE( result == SampleOnlineResult::NotSignedIn );

    BitReader empty( nullptr, 0 );
    SW_EXPECT_FALSE( OnlineReplyUtil::readEnum( empty, SampleOnlineResult::Count, result ) );
}
