#include "pch.h"

#include "Core/Testing/TestFramework.h"
#include "Core/Time/Clock.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"
#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"

// EphemeralStoreTest — 휘발성 저장의 메모리 구현과 라우터: 키 · 크기 · 점수 형식 검사, 조건 쓰기 · 비교 후 쓰기(임대), 게으른 만료, 정수 증감,
// 정렬 집합의 순서(큰 점수 · 같은 점수는 멤버 바이트 역순) · 순위 · 범위, 값과 정렬 집합의 종류 충돌, 발행/구독, 내린 뒤의 Unavailable,
// 라우터의 답 나눠 주기 · 주인 몫 · 채널 공유 · 내릴 때 정확히 한 번. 시계는 손으로 민다(nogpu).

namespace
{
    sw::vector<uint8> makeBytes( sw::string_view text )
    {
        return sw::vector<uint8>( text.begin(), text.end() );
    }

    /** @brief 데이터 하나 + 앞 하나입니다. 시각은 1000 ms 에서 시작합니다. */
    struct CacheRig
    {
        CacheRig()
            : _clock{ sw::MonotonicMilliseconds{ 1000 } }
            , _database{ _clock }
            , _store{ &_database }
        {
        }

        /** @brief 요청 하나를 맡기고 그 답을 꺼냅니다. 메모리 앞은 맡길 때 실행하므로 답이 바로 하나 있다. */
        sw::EphemeralReply run( const sw::EphemeralRequest& request )
        {
            const uint64                   requestID = _store.submit( request );
            sw::vector<sw::EphemeralReply> listReply;
            SW_EXPECT_EQUAL( 1, _store.pollReplies( listReply ) );
            if ( listReply.empty() )
                return sw::EphemeralReply{};
            SW_EXPECT_EQUAL( requestID, listReply.back()._requestID );
            return listReply.back();
        }

        sw::EphemeralResult result( const sw::EphemeralRequest& request ) { return run( request )._result; }

        sw::ManualClock             _clock;
        sw::MemoryEphemeralDatabase _database;
        sw::MemoryEphemeralStore    _store;
    };
} // namespace

/**
 * @brief [EphemeralStoreTest] 키는 `[0-9a-z_.:/-]` 1..256 B, 값은 64 KiB 이하, 점수는 ±2^53, 만료 · 범위는 음수가 아니어야 한다 — 어기면 실행하지 않고 Invalid
 */
SW_TEST_CASE( EphemeralStoreTest, MalformedRequestsAreInvalidAndNotExecuted )
{
    SW_EXPECT_TRUE( sw::EphemeralRequest::isValidKey( "presence:42/a_b.c-d" ) );
    SW_EXPECT_TRUE( sw::EphemeralRequest::isValidKey( sw::string( 256, 'k' ) ) );
    SW_EXPECT_FALSE( sw::EphemeralRequest::isValidKey( sw::string( 257, 'k' ) ) );
    SW_EXPECT_FALSE( sw::EphemeralRequest::isValidKey( "" ) );
    SW_EXPECT_FALSE( sw::EphemeralRequest::isValidKey( "Upper" ) );
    SW_EXPECT_FALSE( sw::EphemeralRequest::isValidKey( "has space" ) );

    CacheRig rig;
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeSet( "Bad Key", makeBytes( "v" ), 0 ) ) == sw::EphemeralResult::Invalid );
    sw::vector<uint8> listHuge( static_cast<size_t>( sw::IEphemeralStore::kMaxValueSize ) + 1, 'x' );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeSet( "big", listHuge, 0 ) ) == sw::EphemeralResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeSet( "neg", makeBytes( "v" ), -1 ) ) == sw::EphemeralResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeIncrement( "neg", 1, -5 ) ) == sw::EphemeralResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board", "", 1 ) ) == sw::EphemeralResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board", "m", sw::IEphemeralStore::kMaxAbsScore + 1 ) ) == sw::EphemeralResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRange( "board", -1, 5 ) ) == sw::EphemeralResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRange( "board", 0, -1 ) ) == sw::EphemeralResult::Invalid );

    // 실행하지 않았다 — 아무 키도 생기지 않았다
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "big" ) ) == sw::EphemeralResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "neg" ) ) == sw::EphemeralResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRank( "board", "m" ) ) == sw::EphemeralResult::NotFound );
    // 경계 값은 된다
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board", "m", sw::IEphemeralStore::kMaxAbsScore ) ) == sw::EphemeralResult::Ok );
}

/**
 * @brief [EphemeralStoreTest] 요청 id 는 1 부터 차례로이고, 답은 맡긴 순서대로 쌓였다가 한 번에 꺼내진다
 */
SW_TEST_CASE( EphemeralStoreTest, RepliesKeepSubmitOrderAndIDsStartAtOne )
{
    CacheRig rig;
    SW_EXPECT_EQUAL( uint64( 1 ), rig._store.submit( sw::EphemeralRequest::makeSet( "a", makeBytes( "1" ), 0 ) ) );
    SW_EXPECT_EQUAL( uint64( 2 ), rig._store.submit( sw::EphemeralRequest::makeGet( "a" ) ) );
    SW_EXPECT_EQUAL( uint64( 3 ), rig._store.submit( sw::EphemeralRequest::makeGet( "missing" ) ) );
    SW_EXPECT_EQUAL( 3, rig._store.getPendingCount() );

    sw::vector<sw::EphemeralReply> listReply;
    SW_REQUIRE_EQUAL( 3, rig._store.pollReplies( listReply ) );
    SW_EXPECT_EQUAL( 0, rig._store.getPendingCount() );
    SW_EXPECT_TRUE( listReply[0]._operation == sw::EphemeralOperation::Set );
    SW_EXPECT_TRUE( listReply[1]._result == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( listReply[1]._value == makeBytes( "1" ) );
    SW_EXPECT_TRUE( listReply[2]._result == sw::EphemeralResult::NotFound );
    SW_EXPECT_EQUAL( uint64( 3 ), listReply[2]._requestID );
}

/**
 * @brief [EphemeralStoreTest] 조건 쓰기: IfAbsent 는 있으면 Conflict(값 그대로), IfPresent 는 없으면 Conflict, 지우기는 없으면 NotFound
 */
SW_TEST_CASE( EphemeralStoreTest, ConditionalSetAndErase )
{
    CacheRig rig;
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeSet( "k", makeBytes( "x" ), 0, sw::EphemeralCondition::IfPresent ) ) == sw::EphemeralResult::Conflict );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "k" ) ) == sw::EphemeralResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeSet( "k", makeBytes( "first" ), 0, sw::EphemeralCondition::IfAbsent ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeSet( "k", makeBytes( "second" ), 0, sw::EphemeralCondition::IfAbsent ) ) == sw::EphemeralResult::Conflict );
    SW_EXPECT_TRUE( rig.run( sw::EphemeralRequest::makeGet( "k" ) )._value == makeBytes( "first" ) );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeSet( "k", makeBytes( "third" ), 0, sw::EphemeralCondition::IfPresent ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.run( sw::EphemeralRequest::makeGet( "k" ) )._value == makeBytes( "third" ) );

    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeErase( "k" ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeErase( "k" ) ) == sw::EphemeralResult::NotFound );
}

/**
 * @brief [EphemeralStoreTest] 만료는 게으르다: 만료 시각 직전까지는 있고 `now >= 만료` 면 없다. 만료가 지난 키는 IfAbsent 로 다시 얻을 수 있다
 */
SW_TEST_CASE( EphemeralStoreTest, ValuesExpireAtDeadlineAndCanBeReacquired )
{
    CacheRig rig;
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "lease", makeBytes( "owner1" ), 100, sw::EphemeralCondition::IfAbsent ) ) == sw::EphemeralResult::Ok );
    rig._clock.advanceMilliseconds( 99 );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "lease" ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeSet( "lease", makeBytes( "owner2" ), 100, sw::EphemeralCondition::IfAbsent ) ) == sw::EphemeralResult::Conflict );
    rig._clock.advanceMilliseconds( 1 );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "lease" ) ) == sw::EphemeralResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeSet( "lease", makeBytes( "owner2" ), 100, sw::EphemeralCondition::IfAbsent ) ) == sw::EphemeralResult::Ok );

    // 만료 0 은 영원하다
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "forever", makeBytes( "v" ), 0 ) ) == sw::EphemeralResult::Ok );
    rig._clock.advanceMilliseconds( 1000000 );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "forever" ) ) == sw::EphemeralResult::Ok );
}

/**
 * @brief [EphemeralStoreTest] Expire: 없는 키는 NotFound, 양수는 지금부터 다시 재고, 0 이하는 바로 지운다(정렬 집합도 같다)
 */
SW_TEST_CASE( EphemeralStoreTest, ExpireResetsOrErasesValuesAndScoreSets )
{
    CacheRig rig;
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeExpire( "none", 100 ) ) == sw::EphemeralResult::NotFound );

    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "k", makeBytes( "v" ), 50 ) ) == sw::EphemeralResult::Ok );
    rig._clock.advanceMilliseconds( 40 );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeExpire( "k", 50 ) ) == sw::EphemeralResult::Ok ); // 이제 만료는 1090
    rig._clock.advanceMilliseconds( 40 );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "k" ) ) == sw::EphemeralResult::Ok ); // 처음 만료(1050)는 지났다
    rig._clock.advanceMilliseconds( 10 );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "k" ) ) == sw::EphemeralResult::NotFound );

    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "k2", makeBytes( "v" ), 0 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeExpire( "k2", 0 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "k2" ) ) == sw::EphemeralResult::NotFound );

    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board", "a", 1 ) ) == sw::EphemeralResult::Ok );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeExpire( "board", 10 ) ) == sw::EphemeralResult::Ok );
    rig._clock.advanceMilliseconds( 10 );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRank( "board", "a" ) ) == sw::EphemeralResult::NotFound );
    SW_EXPECT_TRUE( rig.run( sw::EphemeralRequest::makeScoreRange( "board", 0, 10 ) )._listMember.empty() );
}

/**
 * @brief [EphemeralStoreTest] 비교 후 쓰기 · 지우기(임대 연장 · 해제): 기대 값이 맞을 때만 바꾸고, 다른 주인의 임대는 지우지 않는다
 */
SW_TEST_CASE( EphemeralStoreTest, CompareAndSetAndEraseGuardOwnership )
{
    CacheRig rig;
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeCompareAndSet( "lease", makeBytes( "me" ), makeBytes( "me" ), 100 ) ) == sw::EphemeralResult::Conflict );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "lease", makeBytes( "me" ), 100 ) ) == sw::EphemeralResult::Ok );

    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeCompareAndErase( "lease", makeBytes( "other" ) ) ) == sw::EphemeralResult::Conflict );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "lease" ) ) == sw::EphemeralResult::Ok );

    rig._clock.advanceMilliseconds( 90 );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeCompareAndSet( "lease", makeBytes( "me" ), makeBytes( "me" ), 100 ) ) == sw::EphemeralResult::Ok ); // 연장: 1190 까지
    rig._clock.advanceMilliseconds( 90 );
    SW_EXPECT_TRUE( rig.run( sw::EphemeralRequest::makeGet( "lease" ) )._value == makeBytes( "me" ) );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeCompareAndSet( "lease", makeBytes( "other" ), makeBytes( "other" ), 100 ) ) == sw::EphemeralResult::Conflict );

    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeCompareAndErase( "lease", makeBytes( "me" ) ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "lease" ) ) == sw::EphemeralResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeCompareAndErase( "lease", makeBytes( "me" ) ) ) == sw::EphemeralResult::Conflict );
}

/**
 * @brief [EphemeralStoreTest] 증감: 새 키는 0 에서 시작해 만료를 걸고(고정 창), 있는 키는 만료를 그대로 두며, 정수가 아니거나 넘치면 Invalid(값 그대로)
 */
SW_TEST_CASE( EphemeralStoreTest, IncrementStartsWindowAndRejectsNonIntegerOrOverflow )
{
    CacheRig rig;
    SW_EXPECT_EQUAL( int64( 3 ), rig.run( sw::EphemeralRequest::makeIncrement( "rate", 3, 100 ) )._integer );
    rig._clock.advanceMilliseconds( 60 );
    SW_EXPECT_EQUAL( int64( -2 ), rig.run( sw::EphemeralRequest::makeIncrement( "rate", -5, 100 ) )._integer ); // 창을 다시 열지 않는다
    SW_EXPECT_TRUE( rig.run( sw::EphemeralRequest::makeGet( "rate" ) )._value == makeBytes( "-2" ) );           // 값은 10 진 글이다
    rig._clock.advanceMilliseconds( 40 );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "rate" ) ) == sw::EphemeralResult::NotFound ); // 처음 창(1100)에 끝난다
    SW_EXPECT_EQUAL( int64( 1 ), rig.run( sw::EphemeralRequest::makeIncrement( "rate", 1, 100 ) )._integer );

    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "text", makeBytes( "12a" ), 0 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeIncrement( "text", 1, 0 ) ) == sw::EphemeralResult::Invalid );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "plus", makeBytes( "+1" ), 0 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeIncrement( "plus", 1, 0 ) ) == sw::EphemeralResult::Invalid ); // RESP 처럼 `+` 는 아니다
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "dash", makeBytes( "-" ), 0 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeIncrement( "dash", 1, 0 ) ) == sw::EphemeralResult::Invalid );

    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "max", makeBytes( "9223372036854775807" ), 0 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeIncrement( "max", 1, 0 ) ) == sw::EphemeralResult::Invalid );
    SW_EXPECT_TRUE( rig.run( sw::EphemeralRequest::makeGet( "max" ) )._value == makeBytes( "9223372036854775807" ) );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "min", makeBytes( "-9223372036854775808" ), 0 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_EQUAL( int64( -9223372036854775807ll ), rig.run( sw::EphemeralRequest::makeIncrement( "min", 1, 0 ) )._integer );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "over", makeBytes( "9223372036854775808" ), 0 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeIncrement( "over", 0, 0 ) ) == sw::EphemeralResult::Invalid ); // int64 에 들지 않는 글
}

/**
 * @brief [EphemeralStoreTest] 정렬 집합: 큰 점수부터, 같은 점수는 멤버 바이트 역순. 순위 · 범위(넘친 범위는 잘린다) · 더하기 · 빼기
 */
SW_TEST_CASE( EphemeralStoreTest, ScoreSetOrdersDescendingWithMemberTieBreak )
{
    CacheRig rig;
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board", "alice", 10 ) ) == sw::EphemeralResult::Ok );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board", "bob", 30 ) ) == sw::EphemeralResult::Ok );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board", "carol", 10 ) ) == sw::EphemeralResult::Ok );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board", "dave", -5 ) ) == sw::EphemeralResult::Ok );

    const sw::EphemeralReply range = rig.run( sw::EphemeralRequest::makeScoreRange( "board", 0, 10 ) );
    SW_REQUIRE_EQUAL( size_t( 4 ), range._listMember.size() );
    SW_EXPECT_EQUAL( sw::string( "bob" ), range._listMember[0]._member );
    SW_EXPECT_EQUAL( sw::string( "carol" ), range._listMember[1]._member ); // 같은 10 점: "carol" > "alice"
    SW_EXPECT_EQUAL( sw::string( "alice" ), range._listMember[2]._member );
    SW_EXPECT_EQUAL( sw::string( "dave" ), range._listMember[3]._member );
    SW_EXPECT_EQUAL( int64( -5 ), range._listMember[3]._score );

    const sw::EphemeralReply rank = rig.run( sw::EphemeralRequest::makeScoreRank( "board", "alice" ) );
    SW_EXPECT_TRUE( rank._result == sw::EphemeralResult::Ok );
    SW_EXPECT_EQUAL( int64( 2 ), rank._integer );
    SW_EXPECT_EQUAL( int64( 10 ), rank._score );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRank( "board", "zed" ) ) == sw::EphemeralResult::NotFound );

    const sw::EphemeralReply page = rig.run( sw::EphemeralRequest::makeScoreRange( "board", 3, 10 ) );
    SW_REQUIRE_EQUAL( size_t( 1 ), page._listMember.size() );
    SW_EXPECT_EQUAL( sw::string( "dave" ), page._listMember[0]._member );
    SW_EXPECT_TRUE( rig.run( sw::EphemeralRequest::makeScoreRange( "board", 9, 10 ) )._listMember.empty() );
    SW_EXPECT_TRUE( rig.run( sw::EphemeralRequest::makeScoreRange( "board", 0, 0 ) )._listMember.empty() );

    const sw::EphemeralReply added = rig.run( sw::EphemeralRequest::makeScoreAdd( "board", "alice", 25 ) );
    SW_EXPECT_EQUAL( int64( 35 ), added._integer );
    SW_EXPECT_EQUAL( int64( 35 ), added._score );
    SW_EXPECT_EQUAL( int64( 0 ), rig.run( sw::EphemeralRequest::makeScoreRank( "board", "alice" ) )._integer );
    SW_EXPECT_EQUAL( int64( 7 ), rig.run( sw::EphemeralRequest::makeScoreAdd( "board", "erin", 7 ) )._integer ); // 없는 멤버는 0 에서 더한다

    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRemove( "board", "bob" ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRemove( "board", "bob" ) ) == sw::EphemeralResult::NotFound );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRemove( "nothing", "bob" ) ) == sw::EphemeralResult::NotFound );
}

/**
 * @brief [EphemeralStoreTest] 점수 더하기가 ±2^53 을 넘으면 Invalid 이고 점수는 그대로다. 마지막 멤버를 빼면 집합 자체가 없다
 */
SW_TEST_CASE( EphemeralStoreTest, ScoreAddRefusesOutOfRangeAndEmptySetVanishes )
{
    CacheRig rig;
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board", "a", sw::IEphemeralStore::kMaxAbsScore ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreAdd( "board", "a", 1 ) ) == sw::EphemeralResult::Invalid );
    SW_EXPECT_EQUAL( sw::IEphemeralStore::kMaxAbsScore, rig.run( sw::EphemeralRequest::makeScoreRank( "board", "a" ) )._score );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreAdd( "board", "b", -sw::IEphemeralStore::kMaxAbsScore ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreAdd( "board", "b", -1 ) ) == sw::EphemeralResult::Invalid );

    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeScoreRemove( "board", "a" ) ) == sw::EphemeralResult::Ok );
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeScoreRemove( "board", "b" ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeErase( "board" ) ) == sw::EphemeralResult::NotFound ); // 빈 집합은 없는 키다
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeExpire( "board", 10 ) ) == sw::EphemeralResult::NotFound );
}

/**
 * @brief [EphemeralStoreTest] 값 키에 정렬 집합 연산은 Invalid(WRONGTYPE), 정렬 집합 키에 Set 은 덮어쓰고 Get 은 NotFound, Erase 는 두 종류를 다 지운다
 */
SW_TEST_CASE( EphemeralStoreTest, ValueAndScoreSetKindsDoNotMix )
{
    CacheRig rig;
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "k", makeBytes( "v" ), 0 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "k", "m", 1 ) ) == sw::EphemeralResult::Invalid );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRange( "k", 0, 5 ) ) == sw::EphemeralResult::Invalid );
    SW_EXPECT_TRUE( rig.run( sw::EphemeralRequest::makeGet( "k" ) )._value == makeBytes( "v" ) );

    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board", "m", 1 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeGet( "board" ) ) == sw::EphemeralResult::NotFound ); // RESP 앞과 같다
    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeSet( "board", makeBytes( "plain" ), 0 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRank( "board", "m" ) ) == sw::EphemeralResult::Invalid ); // 이제 값 키다

    SW_REQUIRE_TRUE( rig.result( sw::EphemeralRequest::makeScoreSet( "board2", "m", 1 ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeErase( "board2" ) ) == sw::EphemeralResult::Ok );
    SW_EXPECT_TRUE( rig.result( sw::EphemeralRequest::makeScoreRank( "board2", "m" ) ) == sw::EphemeralResult::NotFound );
}

/**
 * @brief [EphemeralStoreTest] 발행/구독: 구독한 앞마다 한 번 받고(받은 수가 답), 구독 전 · 해제 뒤의 메시지는 없으며, 데이터를 비워도 구독은 남는다
 */
SW_TEST_CASE( EphemeralStoreTest, PublishReachesEachSubscribedFrontOnce )
{
    sw::ManualClock                  clock( sw::MonotonicMilliseconds{ 0 } );
    sw::MemoryEphemeralDatabase      database( clock );
    sw::MemoryEphemeralStore         storeA( &database );
    sw::MemoryEphemeralStore         storeB( &database );
    sw::vector<sw::EphemeralReply>   listReply;
    sw::vector<sw::EphemeralMessage> listMessage;

    (void)storeA.submit( sw::EphemeralRequest::makePublish( "chan", makeBytes( "early" ) ) ); // 아무도 구독하지 않았다
    storeA.subscribe( "chan" );
    storeB.subscribe( "chan" );
    storeB.subscribe( "chan" );
    storeB.subscribe( "Bad Channel" ); // 키 규칙을 어긴 채널은 구독하지 않는다
    (void)storeA.submit( sw::EphemeralRequest::makePublish( "chan", makeBytes( "hello" ) ) );
    SW_REQUIRE_EQUAL( 2, storeA.pollReplies( listReply ) );
    SW_EXPECT_EQUAL( int64( 0 ), listReply[0]._integer );
    SW_EXPECT_EQUAL( int64( 2 ), listReply[1]._integer );

    SW_REQUIRE_EQUAL( 1, storeB.pollMessages( listMessage ) );
    SW_EXPECT_EQUAL( sw::string( "chan" ), listMessage[0]._channel );
    SW_EXPECT_TRUE( listMessage[0]._bytes == makeBytes( "hello" ) );
    SW_EXPECT_EQUAL( 1, storeA.pollMessages( listMessage ) ); // 보낸 앞도 구독했으면 받는다

    storeB.unsubscribe( "chan" );
    database.clearData();
    (void)storeA.submit( sw::EphemeralRequest::makePublish( "chan", makeBytes( "after" ) ) );
    listReply.clear();
    SW_REQUIRE_EQUAL( 1, storeA.pollReplies( listReply ) );
    SW_EXPECT_EQUAL( int64( 1 ), listReply[0]._integer ); // clearData 뒤에도 A 의 구독은 남았다
    listMessage.clear();
    SW_EXPECT_EQUAL( 0, storeB.pollMessages( listMessage ) );
}

/**
 * @brief [EphemeralStoreTest] 내린 앞: 뒤의 요청은 모두 Unavailable(실행하지 않음), 메시지도 더 받지 않고, 두 번 내려도 된다
 */
SW_TEST_CASE( EphemeralStoreTest, ShutdownAnswersUnavailableAndDropsSubscriptions )
{
    sw::ManualClock             clock( sw::MonotonicMilliseconds{ 0 } );
    sw::MemoryEphemeralDatabase database( clock );
    sw::MemoryEphemeralStore    storeA( &database );
    sw::MemoryEphemeralStore    storeB( &database );
    storeB.subscribe( "chan" );
    storeB.shutdown();
    storeB.shutdown();

    (void)storeB.submit( sw::EphemeralRequest::makeSet( "k", makeBytes( "v" ), 0 ) );
    sw::vector<sw::EphemeralReply> listReply;
    SW_REQUIRE_EQUAL( 1, storeB.pollReplies( listReply ) );
    SW_EXPECT_TRUE( listReply[0]._result == sw::EphemeralResult::Unavailable );
    SW_EXPECT_TRUE( listReply[0]._operation == sw::EphemeralOperation::Set );

    (void)storeA.submit( sw::EphemeralRequest::makeGet( "k" ) );
    (void)storeA.submit( sw::EphemeralRequest::makePublish( "chan", makeBytes( "x" ) ) );
    listReply.clear();
    SW_REQUIRE_EQUAL( 2, storeA.pollReplies( listReply ) );
    SW_EXPECT_TRUE( listReply[0]._result == sw::EphemeralResult::NotFound ); // 내린 앞의 Set 은 실행되지 않았다
    SW_EXPECT_EQUAL( int64( 0 ), listReply[1]._integer );                    // B 의 받은편지함은 지워졌다
    sw::vector<sw::EphemeralMessage> listMessage;
    SW_EXPECT_EQUAL( 0, storeB.pollMessages( listMessage ) );
}

/**
 * @brief [EphemeralStoreTest] 라우터: 답은 맡긴 델리게이트로 한 번 가고, 주인 없이 맡긴 답은 버리며, 주인 몫을 놓으면 기다리던 델리게이트가 불리지 않는다
 */
SW_TEST_CASE( EphemeralStoreTest, RouterDispatchesRepliesToLiveOwnersOnly )
{
    CacheRig                 rig;
    sw::EphemeralStoreRouter router;
    router.initialize( &rig._store );

    int32             callCount = 0;
    sw::vector<uint8> listGotValue;
    sw::Subscription  owner = router.openReplyOwner();
    const uint64      setID = router.submit( sw::EphemeralRequest::makeSet( "k", makeBytes( "v" ), 0 ),
                                             [&callCount]( const sw::EphemeralReply& reply )
         {
        ++callCount;
        SW_EXPECT_TRUE( reply._result == sw::EphemeralResult::Ok );
    }, owner );
    SW_EXPECT_EQUAL( uint64( 1 ), setID );
    router.submit( sw::EphemeralRequest::makeErase( "nobody.waits" ) ); // 답을 보지 않는 쓰기
    (void)router.submit( sw::EphemeralRequest::makeGet( "k" ), [&callCount, &listGotValue]( const sw::EphemeralReply& reply )
    {
        ++callCount;
        listGotValue = reply._value;
    }, owner );
    SW_EXPECT_EQUAL( 2, router.getPendingCount() );
    SW_EXPECT_EQUAL( 2, router.pump() );
    SW_EXPECT_EQUAL( 2, callCount );
    SW_EXPECT_TRUE( listGotValue == makeBytes( "v" ) );
    SW_EXPECT_EQUAL( 0, router.getPendingCount() );
    SW_EXPECT_EQUAL( 0, router.pump() ); // 같은 답이 두 번 오지 않는다

    // 주인이 몫을 놓으면 맡긴 답은 버린다(요청 자체는 이미 실행됐다)
    (void)router.submit( sw::EphemeralRequest::makeSet( "k", makeBytes( "w" ), 0 ), [&callCount]( const sw::EphemeralReply& )
    { ++callCount; }, owner );
    owner.reset();
    SW_EXPECT_EQUAL( 0, router.getPendingCount() );
    SW_EXPECT_EQUAL( 0, router.pump() );
    SW_EXPECT_EQUAL( 2, callCount );
    SW_EXPECT_TRUE( rig.run( sw::EphemeralRequest::makeGet( "k" ) )._value == makeBytes( "w" ) );
}

/**
 * @brief [EphemeralStoreTest] 라우터의 거절: 죽은 주인 몫 · 빈 델리게이트로 맡기면 0 이고 아무것도 맡기지 않으며, 초기화 전 · 빈 델리게이트 구독은 빈 구독이다
 */
SW_TEST_CASE( EphemeralStoreTest, RouterRefusesDeadOwnerEmptyDelegateAndEarlySubscribe )
{
    SW_EXPECT_LOG( "EphemeralStoreRouter.SubmitRefused" );
    SW_EXPECT_LOG( "EphemeralStoreRouter.SubscribeRefused" );
    CacheRig                 rig;
    sw::EphemeralStoreRouter router;
    const sw::Subscription   early = router.subscribe( "chan", []( const sw::EphemeralMessage& ) {} );
    SW_EXPECT_FALSE( early.isActive() );
    router.initialize( &rig._store );

    sw::EphemeralStoreRouter otherRouter;
    const sw::Subscription   foreignOwner = otherRouter.openReplyOwner(); // 다른 라우터의 몫
    sw::Subscription         deadOwner;
    SW_EXPECT_EQUAL( uint64( 0 ), router.submit( sw::EphemeralRequest::makeGet( "k" ), []( const sw::EphemeralReply& ) {}, deadOwner ) );
    SW_EXPECT_EQUAL( uint64( 0 ), router.submit( sw::EphemeralRequest::makeGet( "k" ), []( const sw::EphemeralReply& ) {}, foreignOwner ) );
    const sw::Subscription owner = router.openReplyOwner();
    SW_EXPECT_EQUAL( uint64( 0 ), router.submit( sw::EphemeralRequest::makeGet( "k" ), sw::EphemeralStoreRouter::ReplyDelegate{}, owner ) );
    SW_EXPECT_FALSE( router.subscribe( "chan", sw::EphemeralStoreRouter::MessageDelegate{} ).isActive() );
    SW_EXPECT_EQUAL( 0, rig._store.getPendingCount() ); // 앞에 아무것도 맡기지 않았다
    SW_EXPECT_EQUAL( 0, router.getPendingCount() );
}

/**
 * @brief [EphemeralStoreTest] 라우터의 채널: 구독자 모두에게 나눠 주고, 앞의 구독은 마지막 구독자가 나갈 때만 닫는다
 */
SW_TEST_CASE( EphemeralStoreTest, RouterSharesChannelUntilLastSubscriberLeaves )
{
    CacheRig                 rig;
    sw::MemoryEphemeralStore publisher( &rig._database );
    sw::EphemeralStoreRouter router;
    router.initialize( &rig._store );

    int32            firstCount  = 0;
    int32            secondCount = 0;
    sw::Subscription first       = router.subscribe( "chan", [&firstCount]( const sw::EphemeralMessage& )
          { ++firstCount; } );
    sw::Subscription second      = router.subscribe( "chan", [&secondCount]( const sw::EphemeralMessage& )
         { ++secondCount; } );
    SW_REQUIRE_TRUE( first.isActive() );

    auto publishCount = [&publisher]() -> int64
    {
        (void)publisher.submit( sw::EphemeralRequest::makePublish( "chan", makeBytes( "m" ) ) );
        sw::vector<sw::EphemeralReply> listReply;
        (void)publisher.pollReplies( listReply );
        return listReply.empty() ? -1 : listReply.back()._integer;
    };

    SW_EXPECT_EQUAL( int64( 1 ), publishCount() ); // 앞에는 한 번만 구독했다
    SW_EXPECT_EQUAL( 2, router.pump() );
    SW_EXPECT_EQUAL( 1, firstCount );
    SW_EXPECT_EQUAL( 1, secondCount );

    first.reset();
    SW_EXPECT_EQUAL( int64( 1 ), publishCount() ); // 남은 구독자가 있어 앞의 구독은 그대로다
    SW_EXPECT_EQUAL( 1, router.pump() );
    SW_EXPECT_EQUAL( 1, firstCount );
    SW_EXPECT_EQUAL( 2, secondCount );

    second.reset();
    SW_EXPECT_EQUAL( int64( 0 ), publishCount() ); // 마지막 구독자가 나가 앞의 구독을 닫았다
    SW_EXPECT_EQUAL( 0, router.pump() );
}

/**
 * @brief [EphemeralStoreTest] 라우터를 내리면 기다리던 요청마다 Unavailable 답이 정확히 한 번 가고, 남은 구독은 무효가 되며, 두 번 내려도 된다
 */
SW_TEST_CASE( EphemeralStoreTest, RouterShutdownAnswersPendingExactlyOnce )
{
    CacheRig                 rig;
    sw::EphemeralStoreRouter router;
    router.initialize( &rig._store );
    const sw::Subscription owner   = router.openReplyOwner();
    sw::Subscription       channel = router.subscribe( "chan", []( const sw::EphemeralMessage& ) {} );

    int32 unavailableCount = 0;
    int32 otherCount       = 0;
    for ( int32 index = 0; index < 3; ++index )
    {
        (void)router.submit( sw::EphemeralRequest::makeGet( "k" ), [&unavailableCount, &otherCount]( const sw::EphemeralReply& reply )
        {
            if ( reply._result == sw::EphemeralResult::Unavailable )
                ++unavailableCount;
            else
                ++otherCount;
        }, owner );
    }
    router.shutdown();
    router.shutdown();
    SW_EXPECT_EQUAL( 3, unavailableCount );
    SW_EXPECT_EQUAL( 0, otherCount );
    SW_EXPECT_EQUAL( 0, router.getPendingCount() );
    SW_EXPECT_NULL( router.getStore() );
    SW_EXPECT_FALSE( channel.isActive() );
    SW_EXPECT_FALSE( owner.isActive() );
    SW_EXPECT_EQUAL( 0, router.pump() );
    channel.reset(); // 무효가 된 구독을 풀어도 내린 앞을 부르지 않는다
}
