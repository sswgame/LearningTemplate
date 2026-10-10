#include "pch.h"

#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"

#include "TestFramework/TestFramework.h"

// 캐시 앞 나눠 쓰기 — 답은 맡긴 쪽 델리게이트로만, 같은 채널 구독 둘은 앞에 한 번, 델리게이트 안에서 구독 해지, 내릴 때 기다리던 요청은 Unavailable 한 번,
// 취소한 요청은 답이 와도 · 내릴 때도 부르지 않는다.

using namespace sw;

namespace
{
    struct RouterRecorder
    {
        vector<EphemeralReply>   _listReply{};
        vector<EphemeralMessage> _listMessage{};
        EphemeralStoreRouter*    _pRouter{ nullptr };
        uint64                   _subscriptionToDrop{ 0 };

        void onReply( const EphemeralReply& reply ) { _listReply.push_back( reply ); }
        void onMessage( const EphemeralMessage& message )
        {
            _listMessage.push_back( message );
            if ( _pRouter != nullptr && _subscriptionToDrop != 0 )
            {
                _pRouter->unsubscribe( _subscriptionToDrop );
                _subscriptionToDrop = 0;
            }
        }
    };
} // namespace

SW_TEST_CASE( EphemeralStoreRouterTest, RepliesGoOnlyToTheirOwnDelegate )
{
    MemoryEphemeralDatabase database;
    MemoryEphemeralStore    store( &database );
    EphemeralStoreRouter    router;
    router.initialize( &store );

    RouterRecorder first;
    RouterRecorder second;
    (void)router.submit( EphemeralRequest::makeSet( "k1", vector<uint8>{ 1 }, 0 ), EphemeralStoreRouter::ReplyDelegate::create<&RouterRecorder::onReply>( &first ) );
    (void)router.submit( EphemeralRequest::makeGet( "k1" ), EphemeralStoreRouter::ReplyDelegate::create<&RouterRecorder::onReply>( &second ) );
    (void)router.submit( EphemeralRequest::makeErase( "nothing" ), EphemeralStoreRouter::ReplyDelegate{} );
    SW_EXPECT_EQUAL( router.getPendingCount(), 3 );

    (void)router.pump();
    SW_ASSERT_EQUAL( first._listReply.size(), size_t( 1 ) );
    SW_ASSERT_EQUAL( second._listReply.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( first._listReply[0]._operation == EphemeralOperation::Set );
    SW_EXPECT_TRUE( second._listReply[0]._operation == EphemeralOperation::Get );
    SW_EXPECT_EQUAL( second._listReply[0]._value.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( router.getPendingCount(), 0 );
}

SW_TEST_CASE( EphemeralStoreRouterTest, TwoSubscribersShareOneChannelAndCanLeaveDuringDispatch )
{
    MemoryEphemeralDatabase database;
    MemoryEphemeralStore    publisher( &database );
    MemoryEphemeralStore    store( &database );
    EphemeralStoreRouter    router;
    router.initialize( &store );

    RouterRecorder first;
    RouterRecorder second;
    const uint64   firstID = router.subscribe( "chan:a", EphemeralStoreRouter::MessageDelegate::create<&RouterRecorder::onMessage>( &first ) );
    (void)router.subscribe( "chan:a", EphemeralStoreRouter::MessageDelegate::create<&RouterRecorder::onMessage>( &second ) );
    second._pRouter            = &router;
    second._subscriptionToDrop = firstID; // 두 번째가 받는 동안 첫 번째를 뺀다 — 이번 메시지는 둘 다 받는다(복사본)

    (void)publisher.submit( EphemeralRequest::makePublish( "chan:a", vector<uint8>{ 7 } ) );
    (void)router.pump();
    SW_EXPECT_EQUAL( first._listMessage.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( second._listMessage.size(), size_t( 1 ) );

    (void)publisher.submit( EphemeralRequest::makePublish( "chan:a", vector<uint8>{ 8 } ) );
    (void)router.pump();
    SW_EXPECT_EQUAL( first._listMessage.size(), size_t( 1 ) );  // 빠졌다
    SW_EXPECT_EQUAL( second._listMessage.size(), size_t( 2 ) ); // 채널은 아직 열려 있다 — 앞에는 한 번만 구독했다
}

SW_TEST_CASE( EphemeralStoreRouterTest, ShutdownAnswersPendingRequestsOnceWithUnavailable )
{
    MemoryEphemeralDatabase database;
    MemoryEphemeralStore    store( &database );
    RouterRecorder          recorder;
    {
        EphemeralStoreRouter router;
        router.initialize( &store );
        (void)router.submit( EphemeralRequest::makeGet( "k" ), EphemeralStoreRouter::ReplyDelegate::create<&RouterRecorder::onReply>( &recorder ) );
        router.shutdown();
        router.shutdown();
    }
    SW_ASSERT_EQUAL( recorder._listReply.size(), size_t( 1 ) );
    SW_EXPECT_TRUE( recorder._listReply[0]._result == EphemeralResult::Unavailable );
}

SW_TEST_CASE( EphemeralStoreRouterTest, CancelledRequestsAreNeverAnswered )
{
    MemoryEphemeralDatabase database;
    MemoryEphemeralStore    store( &database );
    EphemeralStoreRouter    router;
    router.initialize( &store );
    RouterRecorder recorder;
    const uint64   answeredID = router.submit( EphemeralRequest::makeGet( "a" ), EphemeralStoreRouter::ReplyDelegate::create<&RouterRecorder::onReply>( &recorder ) );
    const uint64   droppedID  = router.submit( EphemeralRequest::makeGet( "b" ), EphemeralStoreRouter::ReplyDelegate::create<&RouterRecorder::onReply>( &recorder ) );
    router.cancel( droppedID );
    router.cancel( 9999 ); // 없는 요청 — 아무것도 하지 않는다
    (void)router.pump();
    SW_ASSERT_EQUAL( recorder._listReply.size(), size_t( 1 ) );
    SW_EXPECT_EQUAL( recorder._listReply[0]._requestID, answeredID );

    const uint64 pendingID = router.submit( EphemeralRequest::makeGet( "c" ), EphemeralStoreRouter::ReplyDelegate::create<&RouterRecorder::onReply>( &recorder ) );
    router.cancel( pendingID ); // 주인이 먼저 내려간다
    router.shutdown();
    SW_EXPECT_EQUAL( recorder._listReply.size(), size_t( 1 ) ); // 내릴 때의 Unavailable 도 오지 않는다
    SW_EXPECT_EQUAL( router.getPendingCount(), 0 );
}
