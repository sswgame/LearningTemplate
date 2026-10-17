#include "pch.h"

#include "Core/String/hashed_string.h"

#include "EngineTest/GameFramework/Online/ServerBusTestTypes.h"

#include "GameFramework/Base/Online/Bus/MixedBuildCodec.h"
#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"

#include "TestFramework/TestFramework.h"

// 서버 버스의 혼합 빌드 읽기(docs/plans/ServerBusFormat.md 3.3, 6절). 옛 빌드와 새 빌드는 필드 이름이 겹치는 두 타입으로 흉내 낸다.
// 메시지는 받은 바이트 그대로 만든다(`ServerBusMessage` 생성자) — 전송은 바이트를 바꾸지 않는다(`ServerBusTest`).

using namespace sw;

namespace
{
    struct TestServerBusMixedBuildInternal
    {
        static constexpr ServerBusTopic<BusTestNoticeOld> kOldTopic{ "test.notice" };
        static constexpr ServerBusTopic<BusTestNoticeNew> kNewTopic{ "test.notice" };

        /** @brief @p value 를 그 타입의 의미 버전으로 쓴 바이트를 다른 서버(2)가 보낸 메시지로 만듭니다. */
        template <typename T>
        static ServerBusMessage makeMessage( const T& value )
        {
            vector<uint8> bytes;
            SW_EXPECT_TRUE( MixedBuildCodec::encode( T::kBusVersion, value, bytes ) );
            return ServerBusMessage( std::move( bytes ), string( "test.notice" ), 2, 1 );
        }

        static BusTestNoticeNew makeNewNotice()
        {
            BusTestNoticeNew notice;
            notice._label     = "kick";
            notice._listItem  = { BusTestItem{ 3 }, BusTestItem{ 4 } };
            notice._accountID = 77;
            notice._color     = BusTestColor::Blue;
            notice._extra     = 5;
            return notice;
        }
    };
} // namespace

/**
 * @brief [ServerBusMixedBuildTest] 새 빌드가 더한 필드는 옛 빌드가 건너뛰고 나머지를 읽는다 — 반대 방향은 새 필드가 기본값이다
 */
SW_TEST_CASE( ServerBusMixedBuildTest, AnAddedFieldIsSkippedByTheOldBuild )
{
    using Internal                    = TestServerBusMixedBuildInternal;
    const BusTestNoticeNew newNotice  = Internal::makeNewNotice();
    const uint64           rejectedAt = ServerBusReadLog::getRejectedCount();

    BusTestNoticeOld fromNew;
    {
        test::ScopedLogSuppressor suppressor; // 건너뛴 필드 경고(타입과 필드마다 한 번)
        SW_ASSERT_TRUE( Internal::kOldTopic.read( Internal::makeMessage( newNotice ), fromNew ) == ServerBusReadResult::Read );
    }
    SW_EXPECT_TRUE( fromNew._label == "kick" );
    SW_ASSERT_EQUAL( size_t( 2 ), fromNew._listItem.size() );
    SW_EXPECT_EQUAL( 4, fromNew._listItem[1]._id );
    SW_EXPECT_EQUAL( uint64( 77 ), fromNew._accountID );
    SW_EXPECT_TRUE( fromNew._color == BusTestColor::Blue );

    BusTestNoticeNew fromOld;
    SW_ASSERT_TRUE( Internal::kNewTopic.read( Internal::makeMessage( fromNew ), fromOld ) == ServerBusReadResult::Read );
    SW_EXPECT_EQUAL( uint64( 77 ), fromOld._accountID );
    SW_EXPECT_TRUE( fromOld._label == "kick" );
    SW_EXPECT_EQUAL( rejectedAt, ServerBusReadLog::getRejectedCount() );
}

/**
 * @brief [ServerBusMixedBuildTest] 옛 빌드가 모르는 열거자는 기본값으로 받지 않고 메시지를 거절한다
 * @details 같은 열거형으로 쓴 바이트에서 열거자 이름 해시를 모르는 이름의 해시로 덮어쓴다. 열거형 둘을 쓰면 기록 타입 해시가 달라 변환 경로를 타므로,
 *          실제 혼합 빌드 경로(`readEnumBinary` 가 false)를 보려면 이렇게 한다.
 */
SW_TEST_CASE( ServerBusMixedBuildTest, AnUnknownEnumeratorRejectsTheMessage )
{
    using Internal = TestServerBusMixedBuildInternal;
    BusTestNoticeOld notice;
    notice._accountID = 9;
    notice._color     = BusTestColor::Green;
    vector<uint8> bytes;
    SW_ASSERT_TRUE( MixedBuildCodec::encode( BusTestNoticeOld::kBusVersion, notice, bytes ) );

    const uint32 greenHash   = hashed_string( "Green" ).getHash();
    const uint32 unknownHash = hashed_string( "BusTestColorAddedLater" ).getHash();
    int32        patchCount  = 0;
    for ( size_t index = 0; index + sizeof( uint32 ) <= bytes.size(); ++index )
    {
        uint32 word = 0;
        Memory::copy( &word, bytes.data() + index, sizeof( uint32 ) );
        if ( word != greenHash )
            continue;
        Memory::copy( bytes.data() + index, &unknownHash, sizeof( uint32 ) );
        ++patchCount;
    }
    SW_ASSERT_EQUAL( 1, patchCount );

    const uint64     rejectedAt = ServerBusReadLog::getRejectedCount();
    BusTestNoticeOld target;
    target._accountID = 1;
    {
        test::ScopedLogSuppressor suppressor; // 모르는 열거자 경고와 거절 오류
        SW_EXPECT_TRUE( Internal::kOldTopic.read( ServerBusMessage( std::move( bytes ), string( "test.notice" ), 2, 1 ), target ) == ServerBusReadResult::Unreadable );
    }
    SW_EXPECT_EQUAL( uint64( 1 ), target._accountID ); // 대상은 그대로다
    SW_EXPECT_EQUAL( rejectedAt + 1, ServerBusReadLog::getRejectedCount() );
}

/**
 * @brief [ServerBusMixedBuildTest] 의미 버전이 다르면 필드가 같아도 거절한다
 */
SW_TEST_CASE( ServerBusMixedBuildTest, AnotherSemanticVersionIsRejected )
{
    using Internal = TestServerBusMixedBuildInternal;
    BusTestNoticeNextMeaning next;
    next._accountID             = 5;
    const uint64     rejectedAt = ServerBusReadLog::getRejectedCount();
    BusTestNoticeOld target;
    {
        test::ScopedLogSuppressor suppressor; // 거절 오류(타입, 결과, 버전마다 한 번)
        SW_EXPECT_TRUE( Internal::kOldTopic.read( Internal::makeMessage( next ), target ) == ServerBusReadResult::VersionMismatch );
    }
    SW_EXPECT_EQUAL( uint64( 0 ), target._accountID );
    SW_EXPECT_EQUAL( rejectedAt + 1, ServerBusReadLog::getRejectedCount() );
}

/**
 * @brief [ServerBusMixedBuildTest] 선언된 범위를 넘는 값은 A 로는 읽혀도 거절한다(A 는 `MaxCount` 를 보지 않는다)
 */
SW_TEST_CASE( ServerBusMixedBuildTest, AValueOutsideTheDeclaredRangeIsRejected )
{
    using Internal = TestServerBusMixedBuildInternal;
    BusTestNoticeWideLabel wide;
    wide._label     = string( 40, 'w' );
    wide._accountID = 3;
    BusTestNoticeOld target;
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_TRUE( Internal::kOldTopic.read( Internal::makeMessage( wide ), target ) == ServerBusReadResult::OutOfRange );
    }
    SW_EXPECT_EQUAL( uint64( 0 ), target._accountID );

    // 쓰는 쪽도 범위 밖 값은 발행하지 않는다.
    BusTestNoticeOld tooLong;
    tooLong._label = string( 17, 'x' );
    vector<uint8> bytes;
    {
        test::ScopedLogSuppressor suppressor;
        SW_EXPECT_FALSE( MixedBuildCodec::encode( BusTestNoticeOld::kBusVersion, tooLong, bytes ) );
    }
    SW_EXPECT_TRUE( bytes.empty() );
}

/**
 * @brief [ServerBusMixedBuildTest] 잘린 바이트는 모든 접두에서 거절하고 대상은 그대로다
 */
SW_TEST_CASE( ServerBusMixedBuildTest, EveryTruncatedPrefixIsRejected )
{
    using Internal = TestServerBusMixedBuildInternal;
    BusTestNoticeOld notice;
    notice._label     = "trunc";
    notice._listItem  = { BusTestItem{ 1 } };
    notice._accountID = 42;
    vector<uint8> bytes;
    SW_ASSERT_TRUE( MixedBuildCodec::encode( BusTestNoticeOld::kBusVersion, notice, bytes ) );

    test::ScopedLogSuppressor suppressor;
    for ( size_t length = 0; length < bytes.size(); ++length )
    {
        BusTestNoticeOld target;
        target._accountID = 1;
        const ServerBusMessage message( vector<uint8>( bytes.begin(), bytes.begin() + static_cast<ptrdiff_t>( length ) ), string( "test.notice" ), 2, 1 );
        SW_EXPECT_TRUE_MSG( Internal::kOldTopic.read( message, target ) != ServerBusReadResult::Read, "잘린 바이트를 읽었습니다" );
        SW_EXPECT_EQUAL( uint64( 1 ), target._accountID );
    }
}
