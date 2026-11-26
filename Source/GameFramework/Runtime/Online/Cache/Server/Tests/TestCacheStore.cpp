#include "pch.h"

#include "Core/TestFramework/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Runtime/Online/Cache/Server/CacheStore.h"
#include "GameFramework/Runtime/Online/Cache/Server/CacheStoreRouter.h"
#include "GameFramework/Runtime/Online/Cache/Server/MemoryCacheStore.h"
#include "GameFramework/Runtime/Online/Cache/Server/Tests/CacheStoreTestUtil.h"

// CacheStoreTest — 휘발성 저장의 메모리 구현과 라우터: 키 · 크기 · 점수 형식 검사, 조건 쓰기 · 비교 후 쓰기(임대), 게으른 만료, 정수 증감,
// 정렬 집합의 순서(큰 점수 · 같은 점수는 멤버 바이트 역순) · 순위 · 범위, 값과 정렬 집합의 종류 충돌, 발행/구독, 내린 뒤의 Unavailable,
// 라우터의 답 나눠 주기 · 주인 몫 · 채널 공유 · 내릴 때 정확히 한 번. 시계는 손으로 민다(nogpu).

namespace
{
    sw::vector<uint8> makeBytes( sw::string_view text )
    {
        return sw::vector<uint8>( text.begin(), text.end() );
    }

    sw::CacheValue makeValue( sw::string_view text )
    {
        return sw::CacheStoreTestAccess::makeTextValue( text );
    }

    /** @brief 데이터 하나 + 앞 하나입니다. 시각은 1000 ms 에서 시작합니다. */
    struct CacheFixture
    {

        sw::ManualClock         _clock;
        sw::MemoryCacheDatabase _database;
        sw::MemoryCacheStore    _store;
        CacheFixture()
            : _clock{ sw::MonotonicMilliseconds{ 1000 } }
            , _database{ _clock }
            , _store{ &_database }
        {
        }

        /** @brief 요청 하나를 맡기고 그 답을 꺼냅니다. 메모리 앞은 맡길 때 실행하므로 답이 바로 하나 있다. */
        sw::CacheReply run( const sw::CacheRequest& request )
        {
            const uint64               requestID = _store.submit( request );
            sw::vector<sw::CacheReply> listReply;
            SW_EXPECT_EQUAL( 1, _store.pollReplies( listReply ) );
            if ( listReply.empty() )
                return sw::CacheReply{};
            SW_EXPECT_EQUAL( requestID, listReply.back()._requestID );
            return listReply.back();
        }

        sw::CacheResult result( const sw::CacheRequest& request ) { return run( request )._result; }
    };
} // namespace

/**
 * @brief [CacheStoreTest] 키는 `[0-9a-z_.:/-]` 1..256 B, 값은 64 KiB 이하, 점수는 ±2^53, 만료 · 범위는 음수가 아니어야 한다 — 어기면 실행하지 않고 Invalid
 */
SW_TEST_CASE( CacheStoreTest, MalformedRequestsAreInvalidAndNotExecuted )
{
    SW_EXPECT_TRUE( sw::CacheRequest::isValidKey( "presence:42/a_b.c-d" ) );
    SW_EXPECT_TRUE( sw::CacheRequest::isValidKey( sw::string( 256, 'k' ) ) );
    SW_EXPECT_FALSE( sw::CacheRequest::isValidKey( sw::string( 257, 'k' ) ) );
    SW_EXPECT_FALSE( sw::CacheRequest::isValidKey( "" ) );
    SW_EXPECT_FALSE( sw::CacheRequest::isValidKey( "Upper" ) );
    SW_EXPECT_FALSE( sw::CacheRequest::isValidKey( "has space" ) );

    CacheFixture rig;
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeSet( "Bad Key", makeValue( "v" ), 0 ) ) == sw::CacheResult::Invalid );
    sw::vector<uint8> listHuge( static_cast<size_t>( sw::ICacheStore::kMaxValueSize ) + 1, 'x' );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeSet( "big", sw::CacheStoreTestAccess::makeValue( listHuge ), 0 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeSet( "neg", makeValue( "v" ), -1 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeIncrement( "neg", 1, -5 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board", "", 1 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board", "m", sw::ICacheStore::kMaxAbsScore + 1 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRange( "board", -1, 5 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRange( "board", 0, -1 ) ) == sw::CacheResult::Invalid );

    // 실행하지 않았다 — 아무 키도 생기지 않았다
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "big" ) ) == sw::CacheResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "neg" ) ) == sw::CacheResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRank( "board", "m" ) ) == sw::CacheResult::NotFound );
    // 경계 값은 된다
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board", "m", sw::ICacheStore::kMaxAbsScore ) ) == sw::CacheResult::Ok );
}

/**
 * @brief [CacheStoreTest] 요청 id 는 1 부터 차례로이고, 답은 맡긴 순서대로 쌓였다가 한 번에 꺼내진다
 */
SW_TEST_CASE( CacheStoreTest, RepliesKeepSubmitOrderAndIDsStartAtOne )
{
    CacheFixture rig;
    SW_EXPECT_EQUAL( uint64( 1 ), rig._store.submit( sw::CacheRequest::makeSet( "a", makeValue( "1" ), 0 ) ) );
    SW_EXPECT_EQUAL( uint64( 2 ), rig._store.submit( sw::CacheRequest::makeGet( "a" ) ) );
    SW_EXPECT_EQUAL( uint64( 3 ), rig._store.submit( sw::CacheRequest::makeGet( "missing" ) ) );
    SW_EXPECT_EQUAL( 3, rig._store.getPendingCount() );

    sw::vector<sw::CacheReply> listReply;
    SW_REQUIRE_EQUAL( 3, rig._store.pollReplies( listReply ) );
    SW_EXPECT_EQUAL( 0, rig._store.getPendingCount() );
    SW_EXPECT_TRUE( listReply[0]._operation == sw::CacheOperation::Set );
    SW_EXPECT_TRUE( listReply[1]._result == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( sw::CacheStoreTestAccess::getBytes( listReply[1] ) == makeBytes( "1" ) );
    SW_EXPECT_TRUE( listReply[2]._result == sw::CacheResult::NotFound );
    SW_EXPECT_EQUAL( uint64( 3 ), listReply[2]._requestID );
}

/**
 * @brief [CacheStoreTest] 조건 쓰기: IfAbsent 는 있으면 Conflict(값 그대로), IfPresent 는 없으면 Conflict, 지우기는 없으면 NotFound
 */
SW_TEST_CASE( CacheStoreTest, ConditionalSetAndErase )
{
    CacheFixture rig;
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeSet( "k", makeValue( "x" ), 0, sw::CacheCondition::IfPresent ) ) == sw::CacheResult::Conflict );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "k" ) ) == sw::CacheResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeSet( "k", makeValue( "first" ), 0, sw::CacheCondition::IfAbsent ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeSet( "k", makeValue( "second" ), 0, sw::CacheCondition::IfAbsent ) ) == sw::CacheResult::Conflict );
    SW_EXPECT_TRUE( sw::CacheStoreTestAccess::getBytes( rig.run( sw::CacheRequest::makeGet( "k" ) ) ) == makeBytes( "first" ) );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeSet( "k", makeValue( "third" ), 0, sw::CacheCondition::IfPresent ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( sw::CacheStoreTestAccess::getBytes( rig.run( sw::CacheRequest::makeGet( "k" ) ) ) == makeBytes( "third" ) );

    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeErase( "k" ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeErase( "k" ) ) == sw::CacheResult::NotFound );
}

/**
 * @brief [CacheStoreTest] 만료는 게으르다: 만료 시각 직전까지는 있고 `now >= 만료` 면 없다. 만료가 지난 키는 IfAbsent 로 다시 얻을 수 있다
 */
SW_TEST_CASE( CacheStoreTest, ValuesExpireAtDeadlineAndCanBeReacquired )
{
    CacheFixture rig;
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "lease", makeValue( "owner1" ), 100, sw::CacheCondition::IfAbsent ) ) == sw::CacheResult::Ok );
    rig._clock.advanceMilliseconds( 99 );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "lease" ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeSet( "lease", makeValue( "owner2" ), 100, sw::CacheCondition::IfAbsent ) ) == sw::CacheResult::Conflict );
    rig._clock.advanceMilliseconds( 1 );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "lease" ) ) == sw::CacheResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeSet( "lease", makeValue( "owner2" ), 100, sw::CacheCondition::IfAbsent ) ) == sw::CacheResult::Ok );

    // 만료 0 은 영원하다
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "forever", makeValue( "v" ), 0 ) ) == sw::CacheResult::Ok );
    rig._clock.advanceMilliseconds( 1000000 );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "forever" ) ) == sw::CacheResult::Ok );
}

/**
 * @brief [CacheStoreTest] Expire: 없는 키는 NotFound, 양수는 지금부터 다시 재고, 0 이하는 바로 지운다(정렬 집합도 같다)
 */
SW_TEST_CASE( CacheStoreTest, ExpireResetsOrErasesValuesAndScoreSets )
{
    CacheFixture rig;
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeExpire( "none", 100 ) ) == sw::CacheResult::NotFound );

    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "k", makeValue( "v" ), 50 ) ) == sw::CacheResult::Ok );
    rig._clock.advanceMilliseconds( 40 );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeExpire( "k", 50 ) ) == sw::CacheResult::Ok ); // 이제 만료는 1090
    rig._clock.advanceMilliseconds( 40 );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "k" ) ) == sw::CacheResult::Ok ); // 처음 만료(1050)는 지났다
    rig._clock.advanceMilliseconds( 10 );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "k" ) ) == sw::CacheResult::NotFound );

    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "k2", makeValue( "v" ), 0 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeExpire( "k2", 0 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "k2" ) ) == sw::CacheResult::NotFound );

    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board", "a", 1 ) ) == sw::CacheResult::Ok );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeExpire( "board", 10 ) ) == sw::CacheResult::Ok );
    rig._clock.advanceMilliseconds( 10 );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRank( "board", "a" ) ) == sw::CacheResult::NotFound );
    SW_EXPECT_TRUE( rig.run( sw::CacheRequest::makeScoreRange( "board", 0, 10 ) )._listMember.empty() );
}

/**
 * @brief [CacheStoreTest] 비교 후 쓰기 · 지우기(임대 연장 · 해제): 기대 값이 맞을 때만 바꾸고, 다른 주인의 임대는 지우지 않는다
 */
SW_TEST_CASE( CacheStoreTest, CompareAndSetAndEraseGuardOwnership )
{
    CacheFixture rig;
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeCompareAndSet( "lease", makeValue( "me" ), makeValue( "me" ), 100 ) ) == sw::CacheResult::Conflict );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "lease", makeValue( "me" ), 100 ) ) == sw::CacheResult::Ok );

    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeCompareAndErase( "lease", makeValue( "other" ) ) ) == sw::CacheResult::Conflict );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "lease" ) ) == sw::CacheResult::Ok );

    rig._clock.advanceMilliseconds( 90 );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeCompareAndSet( "lease", makeValue( "me" ), makeValue( "me" ), 100 ) ) == sw::CacheResult::Ok ); // 연장: 1190 까지
    rig._clock.advanceMilliseconds( 90 );
    SW_EXPECT_TRUE( sw::CacheStoreTestAccess::getBytes( rig.run( sw::CacheRequest::makeGet( "lease" ) ) ) == makeBytes( "me" ) );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeCompareAndSet( "lease", makeValue( "other" ), makeValue( "other" ), 100 ) ) == sw::CacheResult::Conflict );

    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeCompareAndErase( "lease", makeValue( "me" ) ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "lease" ) ) == sw::CacheResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeCompareAndErase( "lease", makeValue( "me" ) ) ) == sw::CacheResult::Conflict );
}

/**
 * @brief [CacheStoreTest] 증감: 새 키는 0 에서 시작해 만료를 걸고(고정 창), 있는 키는 만료를 그대로 두며, 정수가 아니거나 넘치면 Invalid(값 그대로)
 */
SW_TEST_CASE( CacheStoreTest, IncrementStartsWindowAndRejectsNonIntegerOrOverflow )
{
    CacheFixture rig;
    SW_EXPECT_EQUAL( int64( 3 ), rig.run( sw::CacheRequest::makeIncrement( "rate", 3, 100 ) )._integer );
    rig._clock.advanceMilliseconds( 60 );
    SW_EXPECT_EQUAL( int64( -2 ), rig.run( sw::CacheRequest::makeIncrement( "rate", -5, 100 ) )._integer );                      // 창을 다시 열지 않는다
    SW_EXPECT_TRUE( sw::CacheStoreTestAccess::getBytes( rig.run( sw::CacheRequest::makeGet( "rate" ) ) ) == makeBytes( "-2" ) ); // 값은 10 진 글이다
    rig._clock.advanceMilliseconds( 40 );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "rate" ) ) == sw::CacheResult::NotFound ); // 처음 창(1100)에 끝난다
    SW_EXPECT_EQUAL( int64( 1 ), rig.run( sw::CacheRequest::makeIncrement( "rate", 1, 100 ) )._integer );

    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "text", makeValue( "12a" ), 0 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeIncrement( "text", 1, 0 ) ) == sw::CacheResult::Invalid );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "plus", makeValue( "+1" ), 0 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeIncrement( "plus", 1, 0 ) ) == sw::CacheResult::Invalid ); // RESP 처럼 `+` 는 아니다
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "dash", makeValue( "-" ), 0 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeIncrement( "dash", 1, 0 ) ) == sw::CacheResult::Invalid );

    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "max", makeValue( "9223372036854775807" ), 0 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeIncrement( "max", 1, 0 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_TRUE( sw::CacheStoreTestAccess::getBytes( rig.run( sw::CacheRequest::makeGet( "max" ) ) ) == makeBytes( "9223372036854775807" ) );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "min", makeValue( "-9223372036854775808" ), 0 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_EQUAL( int64( -9223372036854775807ll ), rig.run( sw::CacheRequest::makeIncrement( "min", 1, 0 ) )._integer );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "over", makeValue( "9223372036854775808" ), 0 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeIncrement( "over", 0, 0 ) ) == sw::CacheResult::Invalid ); // int64 에 들지 않는 글
}

/**
 * @brief [CacheStoreTest] 정렬 집합: 큰 점수부터, 같은 점수는 멤버 바이트 역순. 순위 · 범위(넘친 범위는 잘린다) · 더하기 · 빼기
 */
SW_TEST_CASE( CacheStoreTest, ScoreSetOrdersDescendingWithMemberTieBreak )
{
    CacheFixture rig;
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board", "alice", 10 ) ) == sw::CacheResult::Ok );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board", "bob", 30 ) ) == sw::CacheResult::Ok );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board", "carol", 10 ) ) == sw::CacheResult::Ok );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board", "dave", -5 ) ) == sw::CacheResult::Ok );

    const sw::CacheReply range = rig.run( sw::CacheRequest::makeScoreRange( "board", 0, 10 ) );
    SW_REQUIRE_EQUAL( size_t( 4 ), range._listMember.size() );
    SW_EXPECT_EQUAL( sw::string( "bob" ), range._listMember[0]._member );
    SW_EXPECT_EQUAL( sw::string( "carol" ), range._listMember[1]._member ); // 같은 10 점: "carol" > "alice"
    SW_EXPECT_EQUAL( sw::string( "alice" ), range._listMember[2]._member );
    SW_EXPECT_EQUAL( sw::string( "dave" ), range._listMember[3]._member );
    SW_EXPECT_EQUAL( int64( -5 ), range._listMember[3]._score );

    const sw::CacheReply rank = rig.run( sw::CacheRequest::makeScoreRank( "board", "alice" ) );
    SW_EXPECT_TRUE( rank._result == sw::CacheResult::Ok );
    SW_EXPECT_EQUAL( int64( 2 ), rank._integer );
    SW_EXPECT_EQUAL( int64( 10 ), rank._score );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRank( "board", "zed" ) ) == sw::CacheResult::NotFound );

    const sw::CacheReply page = rig.run( sw::CacheRequest::makeScoreRange( "board", 3, 10 ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), page._listMember.size() );
    SW_EXPECT_EQUAL( sw::string( "dave" ), page._listMember[0]._member );
    SW_EXPECT_TRUE( rig.run( sw::CacheRequest::makeScoreRange( "board", 9, 10 ) )._listMember.empty() );
    SW_EXPECT_TRUE( rig.run( sw::CacheRequest::makeScoreRange( "board", 0, 0 ) )._listMember.empty() );

    const sw::CacheReply added = rig.run( sw::CacheRequest::makeScoreAdd( "board", "alice", 25 ) );
    SW_EXPECT_EQUAL( int64( 35 ), added._integer );
    SW_EXPECT_EQUAL( int64( 35 ), added._score );
    SW_EXPECT_EQUAL( int64( 0 ), rig.run( sw::CacheRequest::makeScoreRank( "board", "alice" ) )._integer );
    SW_EXPECT_EQUAL( int64( 7 ), rig.run( sw::CacheRequest::makeScoreAdd( "board", "erin", 7 ) )._integer ); // 없는 멤버는 0 에서 더한다

    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRemove( "board", "bob" ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRemove( "board", "bob" ) ) == sw::CacheResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRemove( "nothing", "bob" ) ) == sw::CacheResult::NotFound );
}

/**
 * @brief [CacheStoreTest] 점수 더하기가 ±2^53 을 넘으면 Invalid 이고 점수는 그대로다. 마지막 멤버를 빼면 집합 자체가 없다
 */
SW_TEST_CASE( CacheStoreTest, ScoreAddRefusesOutOfRangeAndEmptySetVanishes )
{
    CacheFixture rig;
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board", "a", sw::ICacheStore::kMaxAbsScore ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreAdd( "board", "a", 1 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_EQUAL( sw::ICacheStore::kMaxAbsScore, rig.run( sw::CacheRequest::makeScoreRank( "board", "a" ) )._score );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreAdd( "board", "b", -sw::ICacheStore::kMaxAbsScore ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreAdd( "board", "b", -1 ) ) == sw::CacheResult::Invalid );

    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreRemove( "board", "a" ) ) == sw::CacheResult::Ok );
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreRemove( "board", "b" ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeErase( "board" ) ) == sw::CacheResult::NotFound ); // 빈 집합은 없는 키다
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeExpire( "board", 10 ) ) == sw::CacheResult::NotFound );
}

/**
 * @brief [CacheStoreTest] 값 키에 정렬 집합 연산은 Invalid(WRONGTYPE), 정렬 집합 키에 Set 은 덮어쓰고 Get 은 NotFound, Erase 는 두 종류를 다 지운다
 */
SW_TEST_CASE( CacheStoreTest, ValueAndScoreSetKindsDoNotMix )
{
    CacheFixture rig;
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "k", makeValue( "v" ), 0 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "k", "m", 1 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRange( "k", 0, 5 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_TRUE( sw::CacheStoreTestAccess::getBytes( rig.run( sw::CacheRequest::makeGet( "k" ) ) ) == makeBytes( "v" ) );

    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board", "m", 1 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeGet( "board" ) ) == sw::CacheResult::NotFound ); // RESP 앞과 같다
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeSet( "board", makeValue( "plain" ), 0 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRank( "board", "m" ) ) == sw::CacheResult::Invalid ); // 이제 값 키다

    // 정렬 집합 키의 증감은 Invalid 이고 집합은 그대로다(RESP `INCRBY` 의 WRONGTYPE 과 같다)
    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board3", "m", 4 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeIncrement( "board3", 1, 0 ) ) == sw::CacheResult::Invalid );
    SW_EXPECT_EQUAL( int64( 4 ), rig.run( sw::CacheRequest::makeScoreRank( "board3", "m" ) )._score );

    SW_REQUIRE_TRUE( rig.result( sw::CacheRequest::makeScoreSet( "board2", "m", 1 ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeErase( "board2" ) ) == sw::CacheResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::CacheRequest::makeScoreRank( "board2", "m" ) ) == sw::CacheResult::NotFound );
}

/**
 * @brief [CacheStoreTest] 발행/구독: 구독한 앞마다 한 번 받고(받은 수가 답), 구독 전 · 해제 뒤의 메시지는 없으며, 데이터를 비워도 구독은 남는다
 */
SW_TEST_CASE( CacheStoreTest, PublishReachesEachSubscribedFrontOnce )
{
    sw::ManualClock              clock( sw::MonotonicMilliseconds{ 0 } );
    sw::MemoryCacheDatabase      database( clock );
    sw::MemoryCacheStore         storeA( &database );
    sw::MemoryCacheStore         storeB( &database );
    sw::vector<sw::CacheReply>   listReply;
    sw::vector<sw::CacheMessage> listMessage;

    (void)storeA.submit( sw::CacheStoreTestAccess::makePublish( "chan", makeBytes( "early" ) ) ); // 아무도 구독하지 않았다
    storeA.subscribe( "chan" );
    storeB.subscribe( "chan" );
    storeB.subscribe( "chan" );
    storeB.subscribe( "Bad Channel" ); // 키 규칙을 어긴 채널은 구독하지 않는다
    (void)storeA.submit( sw::CacheStoreTestAccess::makePublish( "chan", makeBytes( "hello" ) ) );
    SW_REQUIRE_EQUAL( 2, storeA.pollReplies( listReply ) );
    SW_EXPECT_EQUAL( int64( 0 ), listReply[0]._integer );
    SW_EXPECT_EQUAL( int64( 2 ), listReply[1]._integer );

    SW_REQUIRE_EQUAL( 1, storeB.pollMessages( listMessage ) );
    SW_EXPECT_EQUAL( sw::string( "chan" ), listMessage[0]._channel );
    SW_EXPECT_TRUE( sw::CacheStoreTestAccess::getBytes( listMessage[0] ) == makeBytes( "hello" ) );
    SW_EXPECT_EQUAL( 1, storeA.pollMessages( listMessage ) ); // 보낸 앞도 구독했으면 받는다

    storeB.unsubscribe( "chan" );
    database.clearData();
    (void)storeA.submit( sw::CacheStoreTestAccess::makePublish( "chan", makeBytes( "after" ) ) );
    listReply.clear();
    SW_REQUIRE_EQUAL( 1, storeA.pollReplies( listReply ) );
    SW_EXPECT_EQUAL( int64( 1 ), listReply[0]._integer ); // clearData 뒤에도 A 의 구독은 남았다
    listMessage.clear();
    SW_EXPECT_EQUAL( 0, storeB.pollMessages( listMessage ) );
}

/**
 * @brief [CacheStoreTest] 내린 앞: 뒤의 요청은 모두 Unavailable(실행하지 않음), 메시지도 더 받지 않고, 두 번 내려도 된다
 */
SW_TEST_CASE( CacheStoreTest, ShutdownAnswersUnavailableAndDropsSubscriptions )
{
    sw::ManualClock         clock( sw::MonotonicMilliseconds{ 0 } );
    sw::MemoryCacheDatabase database( clock );
    sw::MemoryCacheStore    storeA( &database );
    sw::MemoryCacheStore    storeB( &database );
    storeB.subscribe( "chan" );
    storeB.shutdown();
    storeB.shutdown();

    (void)storeB.submit( sw::CacheRequest::makeSet( "k", makeValue( "v" ), 0 ) );
    sw::vector<sw::CacheReply> listReply;
    SW_REQUIRE_EQUAL( 1, storeB.pollReplies( listReply ) );
    SW_EXPECT_TRUE( listReply[0]._result == sw::CacheResult::Unavailable );
    SW_EXPECT_TRUE( listReply[0]._operation == sw::CacheOperation::Set );

    (void)storeA.submit( sw::CacheRequest::makeGet( "k" ) );
    (void)storeA.submit( sw::CacheStoreTestAccess::makePublish( "chan", makeBytes( "x" ) ) );
    listReply.clear();
    SW_REQUIRE_EQUAL( 2, storeA.pollReplies( listReply ) );
    SW_EXPECT_TRUE( listReply[0]._result == sw::CacheResult::NotFound ); // 내린 앞의 Set 은 실행되지 않았다
    SW_EXPECT_EQUAL( int64( 0 ), listReply[1]._integer );                // B 의 받은편지함은 지워졌다
    sw::vector<sw::CacheMessage> listMessage;
    SW_EXPECT_EQUAL( 0, storeB.pollMessages( listMessage ) );
}

/**
 * @brief [CacheStoreTest] 라우터: 답은 맡긴 델리게이트로 한 번 가고, 주인 없이 맡긴 답은 버리며, 주인 몫을 놓으면 기다리던 델리게이트가 불리지 않는다
 */
SW_TEST_CASE( CacheStoreTest, RouterDispatchesRepliesToLiveOwnersOnly )
{
    CacheFixture         rig;
    sw::CacheStoreRouter router;
    router.initialize( rig._store );

    int32             callCount = 0;
    sw::vector<uint8> listGotValue;
    sw::Subscription  owner = router.openReplyOwner();
    router.submit( sw::CacheRequest::makeSet( "k", makeValue( "v" ), 0 ), owner, [&callCount]( const sw::CacheReply& reply )
    {
        ++callCount;
        SW_EXPECT_TRUE( reply._result == sw::CacheResult::Ok );
    } );
    router.submit( sw::CacheRequest::makeErase( "nobody.waits" ) ); // 답을 보지 않는 쓰기
    router.submit( sw::CacheRequest::makeGet( "k" ), owner, [&callCount, &listGotValue]( const sw::CacheReply& reply )
    {
        ++callCount;
        listGotValue = sw::CacheStoreTestAccess::getBytes( reply );
    } );
    SW_EXPECT_EQUAL( 2, router.getPendingCount() );
    SW_EXPECT_EQUAL( 2, router.getPendingCount( owner ) );
    SW_EXPECT_EQUAL( 2, router.poll() );
    SW_EXPECT_EQUAL( 2, callCount );
    SW_EXPECT_TRUE( listGotValue == makeBytes( "v" ) );
    SW_EXPECT_EQUAL( 0, router.getPendingCount() );
    SW_EXPECT_EQUAL( 0, router.poll() ); // 같은 답이 두 번 오지 않는다

    // 주인이 몫을 놓으면 맡긴 답은 버린다(요청 자체는 이미 실행됐다)
    router.submit( sw::CacheRequest::makeSet( "k", makeValue( "w" ), 0 ), owner, [&callCount]( const sw::CacheReply& )
    { ++callCount; } );
    owner.reset();
    SW_EXPECT_EQUAL( 0, router.getPendingCount() );
    SW_EXPECT_EQUAL( 0, router.poll() );
    SW_EXPECT_EQUAL( 2, callCount );
    SW_EXPECT_TRUE( sw::CacheStoreTestAccess::getBytes( rig.run( sw::CacheRequest::makeGet( "k" ) ) ) == makeBytes( "w" ) );
}

/**
 * @brief [CacheStoreTest] 라우터의 거절: 죽은 주인 몫 · 다른 라우터의 몫으로 맡기면 아무것도 맡기지 않고 그 자리에서 Unavailable 로 끝나며, 초기화 전 · 빈 델리게이트 구독은 빈 구독이다
 */
SW_TEST_CASE( CacheStoreTest, RouterRefusesDeadOwnerEmptyDelegateAndEarlySubscribe )
{
    SW_EXPECT_LOG( "CacheStoreRouter.SubmitRefused" );
    SW_EXPECT_LOG( "CacheStoreRouter.SubscribeRefused" );
    CacheFixture           rig;
    sw::CacheStoreRouter   router;
    const sw::Subscription early = router.subscribe( "chan", []( const sw::CacheMessage& ) {} );
    SW_EXPECT_FALSE( early.isActive() );
    router.initialize( rig._store );

    sw::CacheStoreRouter   otherRouter;
    const sw::Subscription foreignOwner = otherRouter.openReplyOwner(); // 다른 라우터의 몫
    sw::Subscription       deadOwner;
    int32                  unavailableCount = 0;
    for ( const sw::Subscription* pOwner : { static_cast<const sw::Subscription*>( &deadOwner ), &foreignOwner } )
    {
        router.submit( sw::CacheRequest::makeGet( "k" ), *pOwner, [&unavailableCount]( const sw::CacheReply& reply )
        { unavailableCount += reply._result == sw::CacheResult::Unavailable ? 1 : 0; } );
    }
    SW_EXPECT_EQUAL( 2, unavailableCount );
    SW_EXPECT_FALSE( router.subscribe( "chan", sw::CacheStoreRouter::MessageDelegate{} ).isActive() );
    SW_EXPECT_EQUAL( 0, rig._store.getPendingCount() ); // 앞에 아무것도 맡기지 않았다
    SW_EXPECT_EQUAL( 0, router.getPendingCount() );
}

/**
 * @brief [CacheStoreTest] 라우터의 채널: 구독자 모두에게 나눠 주고, 앞의 구독은 마지막 구독자가 나갈 때만 닫는다
 */
SW_TEST_CASE( CacheStoreTest, RouterSharesChannelUntilLastSubscriberLeaves )
{
    CacheFixture         rig;
    sw::MemoryCacheStore publisher( &rig._database );
    sw::CacheStoreRouter router;
    router.initialize( rig._store );

    int32            firstCount  = 0;
    int32            secondCount = 0;
    sw::Subscription first       = router.subscribe( "chan", [&firstCount]( const sw::CacheMessage& )
          { ++firstCount; } );
    sw::Subscription second      = router.subscribe( "chan", [&secondCount]( const sw::CacheMessage& )
         { ++secondCount; } );
    SW_REQUIRE_TRUE( first.isActive() );

    auto publishCount = [&publisher]() -> int64
    {
        (void)publisher.submit( sw::CacheStoreTestAccess::makePublish( "chan", makeBytes( "m" ) ) );
        sw::vector<sw::CacheReply> listReply;
        (void)publisher.pollReplies( listReply );
        return listReply.empty() ? -1 : listReply.back()._integer;
    };

    SW_EXPECT_EQUAL( int64( 1 ), publishCount() ); // 앞에는 한 번만 구독했다
    SW_EXPECT_EQUAL( 2, router.poll() );
    SW_EXPECT_EQUAL( 1, firstCount );
    SW_EXPECT_EQUAL( 1, secondCount );

    first.reset();
    SW_EXPECT_EQUAL( int64( 1 ), publishCount() ); // 남은 구독자가 있어 앞의 구독은 그대로다
    SW_EXPECT_EQUAL( 1, router.poll() );
    SW_EXPECT_EQUAL( 1, firstCount );
    SW_EXPECT_EQUAL( 2, secondCount );

    second.reset();
    SW_EXPECT_EQUAL( int64( 0 ), publishCount() ); // 마지막 구독자가 나가 앞의 구독을 닫았다
    SW_EXPECT_EQUAL( 0, router.poll() );
}

/**
 * @brief [CacheStoreTest] 라우터를 내리면 기다리던 요청마다 Unavailable 답이 정확히 한 번 가고, 남은 구독은 무효가 되며, 두 번 내려도 된다
 */
SW_TEST_CASE( CacheStoreTest, RouterShutdownAnswersPendingExactlyOnce )
{
    CacheFixture         rig;
    sw::CacheStoreRouter router;
    router.initialize( rig._store );
    const sw::Subscription owner   = router.openReplyOwner();
    sw::Subscription       channel = router.subscribe( "chan", []( const sw::CacheMessage& ) {} );

    int32 unavailableCount = 0;
    int32 otherCount       = 0;
    for ( int32 index = 0; index < 3; ++index )
    {
        router.submit( sw::CacheRequest::makeGet( "k" ), owner, [&unavailableCount, &otherCount]( const sw::CacheReply& reply )
        {
            if ( reply._result == sw::CacheResult::Unavailable )
                ++unavailableCount;
            else
                ++otherCount;
        } );
    }
    router.shutdown();
    router.shutdown();
    SW_EXPECT_EQUAL( 3, unavailableCount );
    SW_EXPECT_EQUAL( 0, otherCount );
    SW_EXPECT_EQUAL( 0, router.getPendingCount() );
    SW_EXPECT_NULL( router.getStore() );
    SW_EXPECT_FALSE( channel.isActive() );
    SW_EXPECT_FALSE( owner.isActive() );
    SW_EXPECT_EQUAL( 0, router.poll() );
    channel.reset(); // 무효가 된 구독을 풀어도 내린 앞을 부르지 않는다
}
