/**
 * @file EphemeralStoreContract.h
 * @brief `IEphemeralStore` 계약 시험 — 메모리 · RESP 구현이 같은 케이스를 같은 결과로 통과해야 합니다.
 * @details 픽스처는 `IEphemeralStore& getStore()` · `IEphemeralStore& getSecondStore()`(같은 데이터의 두 번째 앞 — 발행/구독) ·
 *          `void advanceTimeMs( int64 )`(메모리: 시계를 민다, RESP: 그만큼 잔다) · `string makeKey( const utf8* )`(실행마다 다른 접두)를 준다.
 *          답은 시험 스레드가 `pollReplies` 로 거두므로 케이스 몸에서 단언해도 된다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Base/Online/Cache/EphemeralStore.h"

#include "TestFramework/TestFramework.h"

#include <thread>

namespace test
{
    /** @brief 계약 케이스 몸들입니다(시험 스레드). */
    struct EphemeralStoreContract
    {
        /** @brief 요청 하나를 맡기고 그 답을 기다립니다(RESP 는 소켓 왕복 — 10 초 상한). 답을 받았으면 true 입니다. */
        static bool executeRequest( sw::IEphemeralStore& store, const sw::EphemeralRequest& request, sw::EphemeralReply& outReply )
        {
            const uint64                   requestId = store.submit( request );
            const sw::Deadline             deadline  = sw::Deadline::afterMilliseconds( 10000 );
            sw::vector<sw::EphemeralReply> listReply;
            while ( deadline.isExpired() == false )
            {
                listReply.clear();
                if ( store.pollReplies( listReply ) == 0 )
                {
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
                    continue;
                }
                for ( sw::EphemeralReply& reply : listReply )
                {
                    if ( reply._requestId != requestId )
                        continue;
                    outReply = std::move( reply );
                    return true;
                }
            }
            return false;
        }

        /** @brief 답의 결과만 봅니다(답이 오지 않으면 Unavailable). */
        static sw::EphemeralResult executeResult( sw::IEphemeralStore& store, const sw::EphemeralRequest& request )
        {
            sw::EphemeralReply reply;
            if ( executeRequest( store, request, reply ) == false )
                return sw::EphemeralResult::Unavailable;
            return reply._result;
        }

        /** @brief 구독한 앞에 메시지가 올 때까지(최대 @p waitMs) 꺼냅니다. */
        static int32 collectMessages( sw::IEphemeralStore& store, int64 waitMs, sw::vector<sw::EphemeralMessage>& outListMessage )
        {
            const sw::Deadline deadline = sw::Deadline::afterMilliseconds( waitMs );
            int32              count    = store.pollMessages( outListMessage );
            while ( count == 0 && deadline.isExpired() == false )
            {
                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
                count += store.pollMessages( outListMessage );
            }
            return count;
        }

        static sw::vector<uint8> makeText( const utf8* pText )
        {
            const sw::string_view text{ pText };
            return sw::vector<uint8>( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
        }

        template <typename FixtureType>
        static void runSetGetAndExpire( FixtureType& fixture )
        {
            using namespace sw;
            IEphemeralStore& store     = fixture.getStore();
            const string     shortKey  = fixture.makeKey( "short" );
            const string     stableKey = fixture.makeKey( "stable" );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeSet( shortKey, makeText( "v1" ), 200 ) ) == EphemeralResult::Ok );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeSet( stableKey, makeText( "v2" ), 0 ) ) == EphemeralResult::Ok );
            EphemeralReply reply;
            SW_ASSERT_TRUE( executeRequest( store, EphemeralRequest::makeGet( shortKey ), reply ) );
            SW_EXPECT_TRUE( reply._result == EphemeralResult::Ok );
            SW_EXPECT_TRUE( reply._value == makeText( "v1" ) );
            fixture.advanceTimeMs( 250 );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeGet( shortKey ) ) == EphemeralResult::NotFound );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeGet( stableKey ) ) == EphemeralResult::Ok ); // ttl 0 은 남는다
        }

        template <typename FixtureType>
        static void runConditionalSet( FixtureType& fixture )
        {
            using namespace sw;
            IEphemeralStore& store = fixture.getStore();
            const string     key   = fixture.makeKey( "cond" );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeSet( key, makeText( "a" ), 0, EphemeralCondition::IfAbsent ) ) == EphemeralResult::Ok );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeSet( key, makeText( "b" ), 0, EphemeralCondition::IfAbsent ) ) == EphemeralResult::Conflict );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeSet( key, makeText( "c" ), 0, EphemeralCondition::IfPresent ) ) == EphemeralResult::Ok );
            const string missingKey = fixture.makeKey( "cond_missing" );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeSet( missingKey, makeText( "d" ), 0, EphemeralCondition::IfPresent ) ) == EphemeralResult::Conflict );
            EphemeralReply reply;
            SW_ASSERT_TRUE( executeRequest( store, EphemeralRequest::makeGet( key ), reply ) );
            SW_EXPECT_TRUE( reply._value == makeText( "c" ) );
        }

        template <typename FixtureType>
        static void runLeaseAcquireExtendRelease( FixtureType& fixture )
        {
            using namespace sw;
            IEphemeralStore& store = fixture.getStore();
            const string     key   = fixture.makeKey( "lease" );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeSet( key, makeText( "owner-a" ), 1000, EphemeralCondition::IfAbsent ) ) == EphemeralResult::Ok );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeCompareAndSet( key, makeText( "owner-b" ), makeText( "owner-b" ), 1000 ) ) ==
                            EphemeralResult::Conflict );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeCompareAndSet( key, makeText( "owner-a" ), makeText( "owner-a" ), 1000 ) ) ==
                            EphemeralResult::Ok );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeCompareAndErase( key, makeText( "owner-b" ) ) ) == EphemeralResult::Conflict );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeCompareAndErase( key, makeText( "owner-a" ) ) ) == EphemeralResult::Ok );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeGet( key ) ) == EphemeralResult::NotFound );
        }

        template <typename FixtureType>
        static void runIncrementCreatesWithWindowTtl( FixtureType& fixture )
        {
            using namespace sw;
            IEphemeralStore& store = fixture.getStore();
            const string     key   = fixture.makeKey( "counter" );
            for ( int64 expected = 1; expected <= 3; ++expected )
            {
                EphemeralReply reply;
                SW_ASSERT_TRUE( executeRequest( store, EphemeralRequest::makeIncrement( key, 1, 300 ), reply ) );
                SW_EXPECT_TRUE( reply._result == EphemeralResult::Ok );
                SW_EXPECT_EQUAL( expected, reply._integer );
            }
            fixture.advanceTimeMs( 350 );
            EphemeralReply renewed;
            SW_ASSERT_TRUE( executeRequest( store, EphemeralRequest::makeIncrement( key, 1, 300 ), renewed ) );
            SW_EXPECT_EQUAL( int64( 1 ), renewed._integer ); // 창이 지나 새로 센다
            const string textKey = fixture.makeKey( "counter_text" );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeSet( textKey, makeText( "abc" ), 0 ) ) == EphemeralResult::Ok );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeIncrement( textKey, 1, 0 ) ) == EphemeralResult::Invalid );
        }

        template <typename FixtureType>
        static void runScoreRankingOrdersTiesByMemberDescending( FixtureType& fixture )
        {
            using namespace sw;
            IEphemeralStore& store = fixture.getStore();
            const string     key   = fixture.makeKey( "rank" );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeScoreSet( key, "a", 10 ) ) == EphemeralResult::Ok );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeScoreSet( key, "b", 20 ) ) == EphemeralResult::Ok );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeScoreSet( key, "c", 20 ) ) == EphemeralResult::Ok );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeScoreSet( key, "d", 5 ) ) == EphemeralResult::Ok );
            EphemeralReply range;
            SW_ASSERT_TRUE( executeRequest( store, EphemeralRequest::makeScoreRange( key, 0, 3 ), range ) );
            SW_ASSERT_EQUAL( size_t( 3 ), range._listMember.size() );
            SW_EXPECT_TRUE( range._listMember[0]._member == "c" && range._listMember[0]._score == 20 );
            SW_EXPECT_TRUE( range._listMember[1]._member == "b" && range._listMember[1]._score == 20 );
            SW_EXPECT_TRUE( range._listMember[2]._member == "a" && range._listMember[2]._score == 10 );
            EphemeralReply rank;
            SW_ASSERT_TRUE( executeRequest( store, EphemeralRequest::makeScoreRank( key, "a" ), rank ) );
            SW_EXPECT_EQUAL( int64( 2 ), rank._integer );
            EphemeralReply added;
            SW_ASSERT_TRUE( executeRequest( store, EphemeralRequest::makeScoreAdd( key, "d", 100 ), added ) );
            SW_EXPECT_EQUAL( int64( 105 ), added._integer );
            SW_ASSERT_TRUE( executeRequest( store, EphemeralRequest::makeScoreRank( key, "d" ), rank ) );
            SW_EXPECT_EQUAL( int64( 0 ), rank._integer );
            SW_EXPECT_EQUAL( int64( 105 ), rank._score );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeScoreRemove( key, "d" ) ) == EphemeralResult::Ok );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeScoreRank( key, "d" ) ) == EphemeralResult::NotFound );
        }

        template <typename FixtureType>
        static void runPublishReachesOnlySubscribers( FixtureType& fixture )
        {
            using namespace sw;
            IEphemeralStore& first   = fixture.getStore();
            IEphemeralStore& second  = fixture.getSecondStore();
            const string     channel = fixture.makeKey( "chan:x" );
            second.subscribe( channel );
            EphemeralReply published;
            SW_ASSERT_TRUE( executeRequest( first, EphemeralRequest::makePublish( channel, makeText( "hello" ) ), published ) );
            SW_EXPECT_TRUE( published._integer >= 1 );
            vector<EphemeralMessage> listMessage;
            SW_EXPECT_EQUAL( 1, collectMessages( second, 2000, listMessage ) );
            SW_ASSERT_EQUAL( size_t( 1 ), listMessage.size() );
            SW_EXPECT_TRUE( listMessage[0]._bytes == makeText( "hello" ) );
            SW_EXPECT_TRUE( listMessage[0]._channel == channel );
            listMessage.clear();
            SW_EXPECT_EQUAL( 0, first.pollMessages( listMessage ) ); // 구독하지 않은 앞에는 오지 않는다
            second.unsubscribe( channel );
            SW_ASSERT_TRUE( executeRequest( first, EphemeralRequest::makePublish( channel, makeText( "again" ) ), published ) );
            SW_EXPECT_EQUAL( int64( 0 ), published._integer );
            SW_EXPECT_EQUAL( 0, collectMessages( second, 50, listMessage ) );
        }

        template <typename FixtureType>
        static void runBinaryValuesAndLimits( FixtureType& fixture )
        {
            using namespace sw;
            IEphemeralStore& store = fixture.getStore();
            const string     key   = fixture.makeKey( "binary" );
            vector<uint8>    bytes( static_cast<size_t>( IEphemeralStore::kMaxValueSize ) );
            for ( size_t index = 0; index < bytes.size(); ++index )
                bytes[index] = static_cast<uint8>( index * 31u ); // 0x00 · 0xFF 모두 든다
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeSet( key, bytes, 0 ) ) == EphemeralResult::Ok );
            EphemeralReply reply;
            SW_ASSERT_TRUE( executeRequest( store, EphemeralRequest::makeGet( key ), reply ) );
            SW_EXPECT_TRUE( reply._value == bytes );
            bytes.push_back( 0 );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeSet( key, bytes, 0 ) ) == EphemeralResult::Invalid );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeGet( "Upper" ) ) == EphemeralResult::Invalid );
            SW_EXPECT_TRUE( executeResult( store, EphemeralRequest::makeScoreSet( fixture.makeKey( "binary_rank" ), "m", IEphemeralStore::kMaxAbsScore + 1 ) ) ==
                            EphemeralResult::Invalid );
        }

        template <typename FixtureType>
        static void runRepliesComeInSubmitOrder( FixtureType& fixture )
        {
            using namespace sw;
            IEphemeralStore& store = fixture.getStore();
            const string     key   = fixture.makeKey( "order" );
            vector<uint64>   listRequestId;
            for ( int32 index = 0; index < 50; ++index )
                listRequestId.push_back( store.submit( EphemeralRequest::makeIncrement( key, 1, 0 ) ) );
            vector<EphemeralReply> listReply;
            const Deadline         deadline = Deadline::afterMilliseconds( 10000 );
            while ( listReply.size() < listRequestId.size() && deadline.isExpired() == false )
            {
                if ( store.pollReplies( listReply ) == 0 )
                    std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }
            SW_ASSERT_EQUAL( listRequestId.size(), listReply.size() );
            for ( size_t index = 0; index < listReply.size(); ++index )
            {
                SW_EXPECT_EQUAL( listRequestId[index], listReply[index]._requestId );
                SW_EXPECT_EQUAL( static_cast<int64>( index + 1 ), listReply[index]._integer );
            }
        }
    };
} // namespace test

/** @brief 계약 케이스 하나를 @p SuiteName 스위트에 만듭니다. */
#define SW_EPHEMERAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, CaseName, RunFunction ) \
    SW_TEST_CASE( SuiteName, CaseName )                                                   \
    {                                                                                     \
        FixtureType fixture;                                                              \
        test::EphemeralStoreContract::RunFunction( fixture );                             \
    }

/** @brief 계약 케이스 여덟을 @p SuiteName 스위트로 만듭니다. */
#define SW_EPHEMERAL_STORE_CONTRACT_SUITE( SuiteName, FixtureType )                                                                                   \
    SW_EPHEMERAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, SetGetAndExpire, runSetGetAndExpire )                                                   \
    SW_EPHEMERAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, ConditionalSet, runConditionalSet )                                                     \
    SW_EPHEMERAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, LeaseAcquireExtendRelease, runLeaseAcquireExtendRelease )                               \
    SW_EPHEMERAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, IncrementCreatesWithWindowTtl, runIncrementCreatesWithWindowTtl )                       \
    SW_EPHEMERAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, ScoreRankingOrdersTiesByMemberDescending, runScoreRankingOrdersTiesByMemberDescending ) \
    SW_EPHEMERAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, PublishReachesOnlySubscribers, runPublishReachesOnlySubscribers )                       \
    SW_EPHEMERAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, BinaryValuesAndLimits, runBinaryValuesAndLimits )                                       \
    SW_EPHEMERAL_STORE_CONTRACT_CASE( SuiteName, FixtureType, RepliesComeInSubmitOrder, runRepliesComeInSubmitOrder )
