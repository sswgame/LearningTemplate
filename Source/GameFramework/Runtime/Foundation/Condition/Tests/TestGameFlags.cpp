// GameFlagsTest — 월드 플래그와 조건식: 값 · 지우기 · 리비전, 조건식 문법(우선순위 · 괄호 · 비교 · 오류), 저장 순서와 복원(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Behavior/ConditionExpr.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"

#include <limits>

using namespace sw;

namespace
{
    struct XTestUtil
    {
        /** @brief 글을 컴파일해 @p flags 로 평가합니다. 틀린 글이면 false 입니다. */
        static bool check( const GameFlags& flags, string_view text )
        {
            ConditionExpr expr;
            return ConditionExpr::compile( text, "test", expr ) && flags.evaluate( expr );
        }
    };
} // namespace

/** @brief [GameFlagsTest] 값 0 은 없는 것이다. 리비전은 값이 실제로 바뀔 때만 오른다. 더하기는 int32 범위에서 멈춘다 */
SW_TEST_CASE( GameFlagsTest, ValuesAndRevision )
{
    GameFlags flags;
    flags.setFlag( "door" );
    SW_EXPECT_EQUAL( 1, flags.getFlag( "door" ) );
    const uint32 revision = flags.getRevision();
    flags.setFlag( "door", 1 );
    SW_EXPECT_EQUAL( revision, flags.getRevision() );

    SW_EXPECT_EQUAL( 4, flags.addFlag( "door", 3 ) );
    SW_EXPECT_EQUAL( 0, flags.addFlag( "door", -4 ) );
    SW_EXPECT_FALSE( flags.hasFlag( "door" ) );
    SW_EXPECT_EQUAL( size_t( 0 ), flags.getCount() );
    SW_EXPECT_FALSE( flags.clearFlag( "door" ) );

    flags.setFlag( hashed_string{}, 5 ); // 이름 없는 값은 받지 않는다
    SW_EXPECT_EQUAL( size_t( 0 ), flags.getCount() );

    flags.setFlag( "big", std::numeric_limits<int32>::max() );
    SW_EXPECT_EQUAL( std::numeric_limits<int32>::max(), flags.addFlag( "big", 10 ) );
    flags.setFlag( "small", std::numeric_limits<int32>::min() );
    SW_EXPECT_EQUAL( std::numeric_limits<int32>::min(), flags.addFlag( "small", -10 ) );

    flags.clear();
    SW_EXPECT_EQUAL( size_t( 0 ), flags.getCount() );
}

/** @brief [GameFlagsTest] 조건식: 빈 식은 참, `&&` 가 `||` 보다 먼저, 괄호와 `!`, 비교의 오른쪽은 정수나 플래그, 음수와 공백 */
SW_TEST_CASE( GameFlagsTest, ConditionGrammar )
{
    GameFlags flags;
    flags.setFlag( "hasKey" );
    flags.setFlag( "count", 3 );
    flags.setFlag( "goal", 3 );
    flags.setFlag( "ns:name.part_1", -2 );

    SW_EXPECT_TRUE( XTestUtil::check( flags, "" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, " \t\r\n" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, "hasKey" ) );
    SW_EXPECT_FALSE( XTestUtil::check( flags, "unknown" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, "!unknown" ) );
    SW_EXPECT_FALSE( XTestUtil::check( flags, "!!unknown" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, "1" ) );
    SW_EXPECT_FALSE( XTestUtil::check( flags, "0" ) );

    SW_EXPECT_TRUE( XTestUtil::check( flags, "hasKey || unknown && unknown" ) ); // && 먼저
    SW_EXPECT_FALSE( XTestUtil::check( flags, "( hasKey || unknown ) && unknown" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, "!( unknown && hasKey )" ) );

    SW_EXPECT_TRUE( XTestUtil::check( flags, "count>=3" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, "count >= goal" ) );
    SW_EXPECT_FALSE( XTestUtil::check( flags, "count>3" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, "count<=3" ) );
    SW_EXPECT_FALSE( XTestUtil::check( flags, "count<3" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, "count==goal" ) );
    SW_EXPECT_FALSE( XTestUtil::check( flags, "count!=goal" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, "ns:name.part_1==-2" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, "-1<0" ) );
    SW_EXPECT_TRUE( XTestUtil::check( flags, "!count==0" ) ); // `!` 뒤는 비교 하나다
}

/** @brief [GameFlagsTest] 잘못된 식은 읽기 오류다 - 짝 없는 괄호 · 빠진 피연산자 · 남은 글 · 큰 수 · 숫자로 시작한 이름 */
SW_TEST_CASE( GameFlagsTest, InvalidConditionsFailToCompile )
{
    SW_EXPECT_LOG( "ConditionExpr.Invalid" );
    for ( const utf8* pBad : { "(a", "a)", "a &&", "|| a", "a >", "> 1", "a b", "()", "-", "-a", "3a", "99999999999", "a = 1", "a & a", "!" } )
    {
        ConditionExpr expr;
        SW_EXPECT_FALSE( ConditionExpr::compile( pBad, "test", expr ) );
    }
}

/** @brief [GameFlagsTest] 저장: 넣은 순서와 상관없이 같은 바이트이고 되돌아온다. 깨진 바이트는 거절하고 상태를 두며, 복원은 0 과 빈 이름을 버린다 */
SW_TEST_CASE( GameFlagsTest, StateIsCanonical )
{
    GameFlags first;
    first.setFlag( "beta", 2 );
    first.setFlag( "alpha", 1 );
    GameFlags second;
    second.setFlag( "alpha", 1 );
    second.setFlag( "beta", 2 );

    vector<GameFlagEntry> listEntry;
    first.fillEntries( listEntry );
    SW_REQUIRE_EQUAL( size_t( 2 ), listEntry.size() );
    SW_EXPECT_TRUE( listEntry[0]._name == hashed_string( "alpha" ) );

    Archive firstBytes;
    first.writeState( firstBytes );
    Archive secondBytes;
    second.writeState( secondBytes );
    SW_REQUIRE_EQUAL( firstBytes.getSize(), secondBytes.getSize() );
    for ( size_t index = 0; index < firstBytes.getSize(); ++index )
    {
        SW_REQUIRE_EQUAL( firstBytes.getData()[index], secondBytes.getData()[index] );
    }

    GameFlags restored;
    restored.setFlag( "stale" );
    Archive reading( firstBytes.getData(), firstBytes.getSize() );
    SW_REQUIRE_TRUE( restored.readState( reading ) );
    SW_EXPECT_EQUAL( 2, restored.getFlag( "beta" ) );
    SW_EXPECT_FALSE( restored.hasFlag( "stale" ) );

    Archive truncated( firstBytes.getData(), firstBytes.getSize() - 1 );
    SW_EXPECT_FALSE( restored.readState( truncated ) );
    SW_EXPECT_EQUAL( 2, restored.getFlag( "beta" ) );

    restored.restoreEntries( {
        GameFlagEntry{         "kept", 7},
        GameFlagEntry{         "zero", 0},
        GameFlagEntry{hashed_string{}, 5}
    } );
    SW_EXPECT_EQUAL( size_t( 1 ), restored.getCount() );
    SW_EXPECT_EQUAL( 7, restored.getFlag( "kept" ) );
}
