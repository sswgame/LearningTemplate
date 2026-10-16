#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Network/BitStream.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
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
        UpdateRequired,
        Count
    };

    /** @brief `OnlineReplyUtil::markError` 가 부르는 키트 프로토콜 모양입니다. 표에 없는 코드는 Unavailable 입니다. */
    struct SampleOnlineProtocol
    {
        static SampleOnlineResult fromErrorCode( uint16 errorCode )
        {
            return errorCode == OnlineError::kUnauthenticated ? SampleOnlineResult::NotSignedIn : SampleOnlineResult::Unavailable;
        }
    };

    struct SampleOnlineReply
    {
        SampleOnlineResult _result{ SampleOnlineResult::Ok };
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
    const OnlineProtocolEntry kAccountRangeProtocol{ "OnlineServiceTest.Account", OnlineMethodRange::kAccount, OnlineMessageTypes<>{} };
    const OnlineProtocolEntry kSameRangeProtocol{ "OnlineServiceTest.SameRange", OnlineMethodRange::kAccount, OnlineMessageTypes<>{} };
    const OnlineProtocolEntry kNotABaseProtocol{ "OnlineServiceTest.NotABase", 0x0910, OnlineMessageTypes<>{} };
    const OnlineProtocolEntry kHostRangeProtocol{ "OnlineServiceTest.HostRange", OnlineMethodRange::kHost, OnlineMessageTypes<>{} };

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
 * @brief [OnlineProtocolTest] 스키마 해시 불일치는 키트 표보다 먼저 UpdateRequired 가 되고, 나머지 코드는 키트 표를 따른다
 * @details 키트 표는 모르는 코드를 Unavailable(다시 시도)로 바꾸므로, 다시 접속하지 않는 오류가 재시도를 부르지 않게 공통 처리가 먼저 막는다.
 */
SW_TEST_CASE( OnlineProtocolTest, VersionMismatchBecomesUpdateRequiredBeforeTheKitTable )
{
    SampleOnlineReply reply;
    OnlineReplyUtil::markError<SampleOnlineProtocol>( OnlineError::kVersionMismatch, reply );
    SW_EXPECT_TRUE( reply._result == SampleOnlineResult::UpdateRequired );
    OnlineReplyUtil::markError<SampleOnlineProtocol>( OnlineError::kUnauthenticated, reply );
    SW_EXPECT_TRUE( reply._result == SampleOnlineResult::NotSignedIn );
    OnlineReplyUtil::markError<SampleOnlineProtocol>( OnlineError::kInternal, reply );
    SW_EXPECT_TRUE( reply._result == SampleOnlineResult::Unavailable );
}

/**
 * @brief [OnlineProtocolTest] 메시지 타입 목록의 해시는 타입마다의 스키마 해시를 목록 순서대로 섞은 값이고, 타입이나 순서가 다르면 달라진다
 * @details 키트는 이 목록 하나로 메시지를 쓰고 읽으며 같은 목록이 등록 항목에 섞이므로, 와이어에 오르는 타입의 배치가 바뀌면 온라인 스키마 해시가 바뀐다.
 */
SW_TEST_CASE( OnlineProtocolTest, MessageTypesHashFollowsTheTypes )
{
    using Balance      = OnlineMessageTypes<LedgerBalance>;
    using Attachment   = OnlineMessageTypes<ServiceMailAttachment>;
    using Both         = OnlineMessageTypes<LedgerBalance, ServiceMailAttachment>;
    using BothReversed = OnlineMessageTypes<ServiceMailAttachment, LedgerBalance>;
    SW_EXPECT_TRUE( Balance::computeSchemaHash() == HashUtil::combine( 0, FixedLayout::getSchemaHash<LedgerBalance>() ) );
    SW_EXPECT_TRUE( Balance::computeSchemaHash() != Attachment::computeSchemaHash() ); // 필드가 같아도 타입 이름이 정규 문자열에 들어간다
    SW_EXPECT_TRUE( Both::computeSchemaHash() != BothReversed::computeSchemaHash() );
    SW_EXPECT_TRUE( OnlineMessageTypes<>::computeSchemaHash() == 0 );
    SW_EXPECT_TRUE( Both::kContains<LedgerBalance> );
    SW_EXPECT_FALSE( Both::kContains<AccountIdentity> );
    SW_EXPECT_TRUE( OnlineProtocolRegistry::computeSchemaHash() == OnlineProtocolRegistry::computeSchemaHash() );
}
