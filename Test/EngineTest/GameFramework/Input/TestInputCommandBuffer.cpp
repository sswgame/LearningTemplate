#include "pch.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Input/InputCommandBuffer.h"

#include "TestFramework/TestFramework.h"

// 장르 공통 조작 — 커맨드 입력(철권 표기 읽기, 방향을 새로 넣기 · 누른 채, 동시 버튼, 단계 사이 틱 한도, 바라보는 쪽 뒤집기, 우선도).

using namespace sw;

namespace
{
    constexpr uint16 kControlButton1 = 1u << 0;
    constexpr uint16 kControlButton2 = 1u << 1;

    void pushFrames( InputCommandBuffer& buffer, uint8 direction, uint16 buttons, int32 count )
    {
        for ( int32 index = 0; index < count; ++index )
        {
            buffer.push( InputFrame{ buttons, direction } );
        }
    }
} // namespace

SW_TEST_CASE( InputCommandBufferTest, ParserReadsTekkenNotationAndMotions )
{
    InputCommandParser parser;
    InputCommand       command;
    SW_ASSERT_TRUE( parser.parse( "f,f+2", command ) );
    SW_EXPECT_EQUAL( 2, static_cast<int32>( command._listStep.size() ) );
    SW_EXPECT_EQUAL( 6, command._listStep[0]._direction );
    SW_EXPECT_EQUAL( kControlButton2, command._listStep[1]._buttons );
    SW_ASSERT_TRUE( parser.parse( "qcf+1", command ) );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( command._listStep.size() ) );
    SW_EXPECT_EQUAL( 3, command._listStep[1]._direction );
    SW_ASSERT_TRUE( parser.parse( "1+2", command ) );
    SW_EXPECT_EQUAL( kControlButton1 | kControlButton2, command._listStep[0]._buttons );
    SW_ASSERT_TRUE( parser.parse( "D/F+1", command ) );
    SW_EXPECT_TRUE( command._listStep[0]._bHold != SW_FALSE && command._listStep[0]._direction == 3 );
    SW_EXPECT_FALSE( parser.parse( "f,x+9", command ) );
    parser.setButtonNames( "LP,HP,LK,HK" );
    SW_ASSERT_TRUE( parser.parse( "d,d/f,f+HP", command ) );
    SW_EXPECT_EQUAL( kControlButton2, command._listStep[2]._buttons );
}

SW_TEST_CASE( InputCommandBufferTest, BufferCompletesCommandsWithinGapsFacingAndPriority )
{
    InputCommandParser parser;
    InputCommand       dashPunch;
    InputCommand       jab;
    InputCommand       both;
    SW_ASSERT_TRUE( parser.parse( "f,f+2", dashPunch ) );
    SW_ASSERT_TRUE( parser.parse( "2", jab ) );
    SW_ASSERT_TRUE( parser.parse( "1+2", both ) );
    dashPunch._id = hashed_string( "dashPunch" );
    jab._id       = hashed_string( "jab" );
    vector<InputCommand> listCommand{ jab, dashPunch };

    // 앞 · 떼기 · 앞 + 2 — 오른쪽을 본다.
    InputCommandBuffer buffer;
    pushFrames( buffer, 6, 0, 2 );
    pushFrames( buffer, 5, 0, 2 );
    pushFrames( buffer, 6, 0, 1 );
    pushFrames( buffer, 6, kControlButton2, 1 );
    SW_EXPECT_TRUE( buffer.isCompleted( dashPunch, 1 ) );
    SW_EXPECT_TRUE( buffer.findCompleted( listCommand, 1 )->_id == hashed_string( "dashPunch" ) ); // 단계가 많은 쪽
    SW_EXPECT_FALSE( buffer.isCompleted( dashPunch, -1 ) );                                        // 왼쪽을 보면 6 은 뒤다
    pushFrames( buffer, 6, kControlButton2, 1 );
    SW_EXPECT_FALSE( buffer.isCompleted( jab, 1 ) ); // 누르고 있기는 새 누름이 아니다

    // 떼지 않고 앞을 계속 누르면 "f,f" 가 아니다.
    buffer.clear();
    pushFrames( buffer, 6, 0, 5 );
    pushFrames( buffer, 6, kControlButton2, 1 );
    SW_EXPECT_FALSE( buffer.isCompleted( dashPunch, 1 ) );
    SW_EXPECT_TRUE( buffer.isCompleted( jab, 1 ) );

    // 단계 사이가 너무 길면 끊긴다.
    buffer.clear();
    pushFrames( buffer, 6, 0, 1 );
    pushFrames( buffer, 5, 0, 20 );
    pushFrames( buffer, 6, kControlButton2, 1 );
    SW_EXPECT_FALSE( buffer.isCompleted( dashPunch, 1 ) );

    // 왼쪽을 볼 때는 4 가 앞.
    buffer.clear();
    pushFrames( buffer, 4, 0, 1 );
    pushFrames( buffer, 5, 0, 1 );
    pushFrames( buffer, 4, kControlButton2, 1 );
    SW_EXPECT_TRUE( buffer.isCompleted( dashPunch, -1 ) );

    // 동시 — 한 틱 차이는 함께로 본다, 세 틱이면 아니다.
    buffer.clear();
    pushFrames( buffer, 5, kControlButton1, 1 );
    pushFrames( buffer, 5, kControlButton1 | kControlButton2, 1 );
    SW_EXPECT_TRUE( buffer.isCompleted( both, 1 ) );
    buffer.clear();
    pushFrames( buffer, 5, kControlButton1, 3 );
    pushFrames( buffer, 5, kControlButton1 | kControlButton2, 1 );
    SW_EXPECT_FALSE( buffer.isCompleted( both, 1 ) );
    SW_EXPECT_EQUAL( 4, InputCommandBuffer::mirrorDirection( 6, -1 ) );
    SW_EXPECT_EQUAL( 9, InputCommandBuffer::mirrorDirection( 7, -1 ) );
    SW_EXPECT_EQUAL( 2, InputCommandBuffer::mirrorDirection( 2, -1 ) );
}

SW_TEST_CASE( InputCommandBufferTest, BufferCountsFramesSavesStateAndPrefersSpecificCommands )
{
    InputCommandBuffer buffer( 8 );
    SW_EXPECT_EQUAL( 0, buffer.getFrameCount() );
    SW_EXPECT_EQUAL( 8, buffer.getCapacity() );
    pushFrames( buffer, 5, 0, 3 );
    SW_EXPECT_EQUAL( 3, buffer.getFrameCount() ); // 쌓인 수 — 용량이 아니다
    pushFrames( buffer, 5, 0, 20 );
    SW_EXPECT_EQUAL( 8, buffer.getFrameCount() );

    // 상태 왕복 — 오래된 것부터 다시 쌓여 같은 판정이 난다.
    buffer.clear();
    pushFrames( buffer, 6, 0, 1 );
    pushFrames( buffer, 5, 0, 1 );
    pushFrames( buffer, 6, kControlButton2, 1 );
    BitWriter writer;
    buffer.writeState( writer );
    InputCommandBuffer restored( 8 );
    BitReader          reader( writer.getBytes().data(), writer.getByteCount() );
    SW_ASSERT_TRUE( restored.readState( reader ) );
    SW_EXPECT_EQUAL( buffer.getFrameCount(), restored.getFrameCount() );
    for ( int32 framesAgo = 0; framesAgo < buffer.getFrameCount(); ++framesAgo )
    {
        SW_EXPECT_EQUAL( buffer.getFrame( framesAgo )._buttons, restored.getFrame( framesAgo )._buttons );
        SW_EXPECT_EQUAL( buffer.getFrame( framesAgo )._direction, restored.getFrame( framesAgo )._direction );
    }
    InputCommandParser parser;
    InputCommand       dashPunch;
    SW_ASSERT_TRUE( parser.parse( "f,f+2", dashPunch ) );
    SW_EXPECT_TRUE( restored.isCompleted( dashPunch, 1 ) );
    BitReader shortReader( writer.getBytes().data(), 1 );
    SW_EXPECT_FALSE( restored.readState( shortReader ) ); // 모자란 바이트 — 그대로 둔다
    SW_EXPECT_EQUAL( 3, restored.getFrameCount() );

    // 우선도 · 단계 수가 같으면 방향 · 버튼을 더 적은 쪽("d/f+1")이 목록 순서와 상관없이 이긴다.
    vector<InputCommand> listCommand( 2 );
    SW_ASSERT_TRUE( parser.parse( "1", listCommand[0] ) );
    SW_ASSERT_TRUE( parser.parse( "d/f+1", listCommand[1] ) );
    listCommand[0]._id = hashed_string( "jab" );
    listCommand[1]._id = hashed_string( "uppercut" );
    InputCommandBuffer fight;
    pushFrames( fight, 3, 0, 2 );
    pushFrames( fight, 3, kControlButton1, 1 );
    SW_ASSERT_NOT_NULL( fight.findCompleted( listCommand, 1 ) );
    SW_EXPECT_TRUE( fight.findCompleted( listCommand, 1 )->_id == hashed_string( "uppercut" ) );
    const InputCommand swapped = listCommand[0];
    listCommand[0]             = listCommand[1];
    listCommand[1]             = swapped;
    SW_EXPECT_TRUE( fight.findCompleted( listCommand, 1 )->_id == hashed_string( "uppercut" ) );
}
