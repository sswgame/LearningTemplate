#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Runtime/Character/Input/InputCommandBuffer.h"

// InputCommandBufferTest — 커맨드 입력(격투 · 콤보): 철권 표기 읽기(줄임 낱말 · 누른 채 · 버튼 조합 · 모르는 낱말), 버퍼가 이번 틱에 끝난 커맨드를 찾는 규칙
// (바라보는 쪽 뒤집기, 같은 방향 두 번은 사이에 떼기, 단계 사이 최대 틱, 여럿이 끝나면 단계 · 구체성이 이긴다), 비우기가 칸까지 되돌리는지. 결정적 계산이다. nogpu.

using namespace sw;

namespace
{
    struct InputCommandTestUtil
    {
        static constexpr uint16 kButton1 = 1u << 0;
        static constexpr uint16 kButton2 = 1u << 1;
        static constexpr uint16 kButton4 = 1u << 3;

        static InputFrame makeFrame( uint8 direction, uint16 buttons = 0 )
        {
            InputFrame frame;
            frame._direction = direction;
            frame._buttons   = buttons;
            return frame;
        }

        static void pushAll( InputCommandBuffer& buffer, std::initializer_list<InputFrame> listFrame )
        {
            for ( const InputFrame& frame : listFrame )
            {
                buffer.push( frame );
            }
        }

        static InputCommand parse( string_view notation )
        {
            const InputCommandParser parser;
            InputCommand             command;
            SW_EXPECT_TRUE( parser.parse( notation, command ) );
            return command;
        }
    };
    using Util = InputCommandTestUtil;
} // namespace

/** @brief [InputCommandBufferTest] 표기 읽기: qcf 는 세 단계로 펼치고 버튼은 마지막 단계에, 대문자는 누른 채, 버튼만의 조합, 모르는 낱말 · 버튼은 거절 */
SW_TEST_CASE( InputCommandBufferTest, ParserReadsTekkenNotation )
{
    const InputCommand qcf = Util::parse( "qcf+1" );
    SW_REQUIRE_EQUAL( size_t{ 3 }, qcf._listStep.size() );
    SW_EXPECT_EQUAL( uint8{ 2 }, qcf._listStep[0]._direction );
    SW_EXPECT_EQUAL( uint8{ 3 }, qcf._listStep[1]._direction );
    SW_EXPECT_EQUAL( uint8{ 6 }, qcf._listStep[2]._direction );
    SW_EXPECT_EQUAL( uint16{ 0 }, qcf._listStep[1]._buttons );
    SW_EXPECT_EQUAL( Util::kButton1, qcf._listStep[2]._buttons );

    const InputCommand dash = Util::parse( "f,f+2" );
    SW_REQUIRE_EQUAL( size_t{ 2 }, dash._listStep.size() );
    SW_EXPECT_EQUAL( uint8{ 6 }, dash._listStep[0]._direction );
    SW_EXPECT_EQUAL( Util::kButton2, dash._listStep[1]._buttons );

    const InputCommand hold = Util::parse( "F+4" );
    SW_REQUIRE_EQUAL( size_t{ 1 }, hold._listStep.size() );
    SW_EXPECT_TRUE( hold._listStep[0]._bHold == SW_TRUE );
    SW_EXPECT_EQUAL( Util::kButton4, hold._listStep[0]._buttons );

    const InputCommand chord = Util::parse( "1+2" );
    SW_REQUIRE_EQUAL( size_t{ 1 }, chord._listStep.size() );
    SW_EXPECT_EQUAL( uint8{ 0 }, chord._listStep[0]._direction );
    SW_EXPECT_EQUAL( static_cast<uint16>( Util::kButton1 | Util::kButton2 ), chord._listStep[0]._buttons );

    const InputCommandParser parser;
    InputCommand             command;
    SW_EXPECT_FALSE( parser.parse( "x+1", command ) );
    SW_EXPECT_FALSE( parser.parse( "f+9", command ) );
    SW_EXPECT_FALSE( parser.parse( "", command ) );
}

/** @brief [InputCommandBufferTest] 앞은 상대 쪽이다 — 오른쪽을 보면 d,d/f,f, 왼쪽을 보면 d,d/b,b 가 qcf 다 */
SW_TEST_CASE( InputCommandBufferTest, QuarterCircleFollowsFacing )
{
    const InputCommand qcf = Util::parse( "qcf+1" );

    InputCommandBuffer right;
    Util::pushAll( right, { Util::makeFrame( 5 ), Util::makeFrame( 2 ), Util::makeFrame( 3 ), Util::makeFrame( 6, Util::kButton1 ) } );
    SW_EXPECT_TRUE( right.isCompleted( qcf, 1 ) );
    SW_EXPECT_FALSE( right.isCompleted( qcf, -1 ) );

    InputCommandBuffer left;
    Util::pushAll( left, { Util::makeFrame( 5 ), Util::makeFrame( 2 ), Util::makeFrame( 1 ), Util::makeFrame( 4, Util::kButton1 ) } );
    SW_EXPECT_TRUE( left.isCompleted( qcf, -1 ) );
    SW_EXPECT_FALSE( left.isCompleted( qcf, 1 ) );
    SW_EXPECT_EQUAL( uint8{ 4 }, InputCommandBuffer::mirrorDirection( 6, -1 ) );
    SW_EXPECT_EQUAL( uint8{ 8 }, InputCommandBuffer::mirrorDirection( 8, -1 ) ); // 위아래는 뒤집지 않는다
}

/** @brief [InputCommandBufferTest] 같은 방향 두 번(f,f)은 사이에 한 번 떼야 한다 — 누르고 있기는 두 번 넣기가 아니다 */
SW_TEST_CASE( InputCommandBufferTest, RepeatedDirectionNeedsRelease )
{
    const InputCommand dash = Util::parse( "f,f+2" );

    InputCommandBuffer held;
    Util::pushAll( held, { Util::makeFrame( 5 ), Util::makeFrame( 6 ), Util::makeFrame( 6 ), Util::makeFrame( 6, Util::kButton2 ) } );
    SW_EXPECT_FALSE( held.isCompleted( dash, 1 ) );

    InputCommandBuffer tapped;
    Util::pushAll( tapped, { Util::makeFrame( 5 ), Util::makeFrame( 6 ), Util::makeFrame( 5 ), Util::makeFrame( 6, Util::kButton2 ) } );
    SW_EXPECT_TRUE( tapped.isCompleted( dash, 1 ) );
}

/** @brief [InputCommandBufferTest] 단계 사이가 최대 틱을 넘으면 끝나지 않는다 */
SW_TEST_CASE( InputCommandBufferTest, GapLongerThanMaxFails )
{
    InputCommand qcf = Util::parse( "qcf+1" );
    qcf._maxGap      = 2;

    InputCommandBuffer slow;
    Util::pushAll( slow, { Util::makeFrame( 5 ), Util::makeFrame( 2 ), Util::makeFrame( 3 ), Util::makeFrame( 5 ), Util::makeFrame( 5 ), Util::makeFrame( 5 ), Util::makeFrame( 6, Util::kButton1 ) } );
    SW_EXPECT_FALSE( slow.isCompleted( qcf, 1 ) );

    qcf._maxGap = 10;
    SW_EXPECT_TRUE( slow.isCompleted( qcf, 1 ) ); // 같은 입력도 시한 안이면 끝난다 — 사이의 중립은 단계를 끊지 않는다
}

/** @brief [InputCommandBufferTest] 여럿이 끝나면 우선도 → 단계 수 → 구체성 순으로 고른다(qcf+1 · d/f+1 이 1 을 이긴다) */
SW_TEST_CASE( InputCommandBufferTest, FindCompletedPrefersLongerAndSpecific )
{
    vector<InputCommand> listCommand;
    listCommand.push_back( Util::parse( "1" ) );
    listCommand.back()._id = hashed_string( "Jab" );
    listCommand.push_back( Util::parse( "qcf+1" ) );
    listCommand.back()._id = hashed_string( "Fireball" );

    InputCommandBuffer buffer;
    Util::pushAll( buffer, { Util::makeFrame( 5 ), Util::makeFrame( 2 ), Util::makeFrame( 3 ), Util::makeFrame( 6, Util::kButton1 ) } );
    const InputCommand* pBest = buffer.findCompleted( listCommand, 1 );
    SW_REQUIRE_TRUE( pBest != nullptr );
    SW_EXPECT_TRUE( pBest->_id == hashed_string( "Fireball" ) );

    vector<InputCommand> listSameLength;
    listSameLength.push_back( Util::parse( "1" ) );
    listSameLength.back()._id = hashed_string( "Jab" );
    listSameLength.push_back( Util::parse( "f+1" ) );
    listSameLength.back()._id     = hashed_string( "Forward" );
    const InputCommand* pSpecific = buffer.findCompleted( listSameLength, 1 );
    SW_REQUIRE_TRUE( pSpecific != nullptr );
    SW_EXPECT_TRUE( pSpecific->_id == hashed_string( "Forward" ) );

    listSameLength.front()._priority = 1; // 우선도가 먼저다
    const InputCommand* pPriority    = buffer.findCompleted( listSameLength, 1 );
    SW_REQUIRE_TRUE( pPriority != nullptr );
    SW_EXPECT_TRUE( pPriority->_id == hashed_string( "Jab" ) );
}

/** @brief [InputCommandBufferTest] 비우기는 쌓인 수와 칸을 모두 중립으로 되돌리고, 용량을 넘는 입력은 오래된 것부터 밀어낸다 */
SW_TEST_CASE( InputCommandBufferTest, ClearAndRingCapacity )
{
    InputCommandBuffer buffer( 4 );
    Util::pushAll( buffer, { Util::makeFrame( 1 ), Util::makeFrame( 2 ), Util::makeFrame( 3 ), Util::makeFrame( 4 ), Util::makeFrame( 6, Util::kButton1 ) } );
    SW_EXPECT_EQUAL( 4, buffer.getFrameCount() );
    SW_EXPECT_EQUAL( uint8{ 6 }, buffer.getFrame( 0 )._direction );
    SW_EXPECT_EQUAL( uint8{ 2 }, buffer.getFrame( 3 )._direction ); // 1 은 밀려났다
    SW_EXPECT_EQUAL( uint8{ 5 }, buffer.getFrame( 4 )._direction ); // 쌓인 수 밖은 중립

    buffer.clear();
    SW_EXPECT_EQUAL( 0, buffer.getFrameCount() );
    SW_EXPECT_EQUAL( uint8{ 5 }, buffer.getFrame( 0 )._direction );
    SW_EXPECT_FALSE( buffer.isCompleted( Util::parse( "1" ), 1 ) );
}
