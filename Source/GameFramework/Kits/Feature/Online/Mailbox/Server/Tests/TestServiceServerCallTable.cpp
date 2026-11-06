// ServiceServerCallTableTest — 서버 호출 표(`Runtime/Online/Service/ServiceServerCallTable`): 델리게이트 응답은 한 번이고 지표에 들며, 모르는 id 와 종료 때 남은 호출은 로그 하나씩이고,
// 응답 안에서 다시 올려도 되며, 키트 칸으로 찾는다. 지표 라벨에 닫힌 메서드 · 결과 열거형이 필요해 우편함 메시지로 본다. 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Observability/MetricRegistry.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"
#include "GameFramework/Runtime/Online/Service/ServiceServerCallTable.h"

using namespace sw;

namespace
{
    struct TicketExtra
    {
        uint64 _ticket{ 0 };
    };

    using MailboxCallTable = ServiceServerCallTable<MailboxReply, MailboxProtocol::MessageTypes>;
    using TicketCallTable  = ServiceServerCallTable<MailboxReply, MailboxProtocol::MessageTypes, TicketExtra>;

    struct ReplySink
    {
        vector<MailboxReply> _listReply{};
        MailboxCallTable*    _pReopenTable{ nullptr }; ///< 있으면 첫 응답 안에서 이 표에 호출 하나를 더 올린다

        void onReply( const MailboxReply& reply )
        {
            _listReply.push_back( reply );
            if ( _pReopenTable == nullptr || _listReply.size() != 1 )
                return;
            (void)_pReopenTable->open( MailboxCallTable::Origin::fromDelegate( MailboxCallTable::ReplyDelegate::create<&ReplySink::onReply>( this ) ), 0 );
        }
    };

    struct ServiceServerCallTableTestUtil
    {
        static MailboxReply makeReply( MailboxResult result )
        {
            MailboxReply reply;
            reply._result = result;
            return reply;
        }
    };
} // namespace

/** @brief [ServiceServerCallTableTest] 델리게이트 경로의 호출은 한 번 응답하고 표에서 빠지며, 요청 수와 지연 히스토그램에 든다 */
SW_TEST_CASE( ServiceServerCallTableTest, DelegateCallIsAnsweredOnceAndCounted )
{
    MetricRegistry   registry;
    MailboxCallTable table;
    table.initialize<MailboxMethodName, MailboxResult>( &registry, "mailbox" );
    ReplySink    sink;
    const int32  listIndex = MailboxMethod::toIndex( MailboxMethod::kList );
    const uint64 callID    = table.open( MailboxCallTable::Origin::fromDelegate( MailboxCallTable::ReplyDelegate::create<&ReplySink::onReply>( &sink ) ), listIndex );
    SW_EXPECT_EQUAL( 1, table.getCount() );

    SW_EXPECT_TRUE( table.finish( callID, ServiceServerCallTableTestUtil::makeReply( MailboxResult::NotSignedIn ) ) );
    SW_REQUIRE_EQUAL( sink._listReply.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( sink._listReply[0]._result == MailboxResult::NotSignedIn );
    SW_EXPECT_TRUE( table.isEmpty() );

    const vector<MetricLabel> listRequestLabel = {
        {"service",       "mailbox"},
        { "method",          "list"},
        { "result", "not_signed_in"}
    };
    const MetricCounter* pCounter = registry.registerCounter( "service_requests_total", "Service requests by method and result", listRequestLabel );
    SW_REQUIRE_TRUE( pCounter != nullptr );
    SW_EXPECT_EQUAL( pCounter->getValue(), uint64( 1 ) );
    const vector<MetricLabel> listLatencyLabel = {
        {"service", "mailbox"},
        { "method",    "list"}
    };
    const MetricHistogram* pLatency = registry.registerHistogram( "service_request_seconds", "Service request latency from receive to reply", MetricRegistry::makeLatencyBounds(), listLatencyLabel );
    SW_REQUIRE_TRUE( pLatency != nullptr );
    SW_EXPECT_EQUAL( pLatency->getCount(), uint64( 1 ) );
}

/** @brief [ServiceServerCallTableTest] 모르는 id 의 완료는 응답하지 않고 경고 하나로 끝난다(서비스마다 따로 두던 경고를 하나로 합쳤다) */
SW_TEST_CASE( ServiceServerCallTableTest, UnknownCallWarnsAndAnswersNobody )
{
    MailboxCallTable table;
    table.initialize<MailboxMethodName, MailboxResult>( nullptr, "mailbox" );
    ReplySink    sink;
    const uint64 callID = table.open( MailboxCallTable::Origin::fromDelegate( MailboxCallTable::ReplyDelegate::create<&ReplySink::onReply>( &sink ) ), 0 );

    SW_EXPECT_LOG( "ServiceServerCallTable.UnknownCall" );
    SW_EXPECT_FALSE( table.finish( callID + 100, ServiceServerCallTableTestUtil::makeReply( MailboxResult::Ok ) ) );
    SW_EXPECT_TRUE( sink._listReply.empty() );
    SW_EXPECT_EQUAL( 1, table.getCount() );
    SW_EXPECT_TRUE( table.finish( callID, ServiceServerCallTableTestUtil::makeReply( MailboxResult::Ok ) ) );
}

/** @brief [ServiceServerCallTableTest] 응답 델리게이트 안에서 다시 올린 호출은 남고, 끝난 호출은 두 번 응답하지 않는다 */
SW_TEST_CASE( ServiceServerCallTableTest, ReplyDelegateMayOpenAnotherCall )
{
    MailboxCallTable table;
    table.initialize<MailboxMethodName, MailboxResult>( nullptr, "mailbox" );
    ReplySink sink;
    sink._pReopenTable  = &table;
    const uint64 callID = table.open( MailboxCallTable::Origin::fromDelegate( MailboxCallTable::ReplyDelegate::create<&ReplySink::onReply>( &sink ) ), 0 );

    SW_EXPECT_TRUE( table.finish( callID, ServiceServerCallTableTestUtil::makeReply( MailboxResult::Ok ) ) );
    SW_EXPECT_EQUAL( sink._listReply.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( 1, table.getCount() ); // 응답 안에서 올린 호출이다
    SW_EXPECT_LOG( "ServiceServerCallTable.AbandonedCalls" );
    table.abandonAll();
}

/** @brief [ServiceServerCallTableTest] 키트 칸으로 호출을 찾고 고칠 수 있으며, 없으면 로그 없이 null 이다 */
SW_TEST_CASE( ServiceServerCallTableTest, ExtraFieldFindsTheCall )
{
    TicketCallTable table;
    table.initialize<MailboxMethodName, MailboxResult>( nullptr, "mailbox" );
    ReplySink                            sink;
    const TicketCallTable::ReplyDelegate onReply = TicketCallTable::ReplyDelegate::create<&ReplySink::onReply>( &sink );
    const uint64                         firstID = table.open( TicketCallTable::Origin::fromDelegate( onReply ), 0, TicketExtra{ 7 } );
    (void)table.open( TicketCallTable::Origin::fromDelegate( onReply ), 0, TicketExtra{ 9 } );

    TicketCallTable::Call* pCall = table.findIf( []( const TicketCallTable::Call& call )
    { return call._extra._ticket == 9; } );
    SW_REQUIRE_TRUE( pCall != nullptr );
    SW_EXPECT_TRUE( pCall->_callID != firstID );
    pCall->_extra._ticket = 0;
    SW_EXPECT_TRUE( table.findIf( []( const TicketCallTable::Call& call )
    { return call._extra._ticket == 9; } ) == nullptr );
    TicketCallTable::Call* pFirst = table.find( firstID );
    SW_REQUIRE_TRUE( pFirst != nullptr );
    SW_EXPECT_EQUAL( pFirst->_extra._ticket, uint64( 7 ) );

    SW_EXPECT_LOG( "ServiceServerCallTable.AbandonedCalls" );
    table.abandonAll();
    SW_EXPECT_TRUE( table.isEmpty() );
    SW_EXPECT_TRUE( sink._listReply.empty() ); // 버린 호출은 응답하지 않는다
    table.abandonAll();                        // 빈 표는 로그를 남기지 않는다
}
