// 채팅 기록 모델 — 받은 메시지가 시간 순으로 쌓이고 상한에서 가장 오래된 줄부터 버려지는 길, 빈 글과 클라이언트 없음이 보내지 않고 이유를 남기는 길, 보내기 응답이 결과를 바꾸는 길, 결과 문구.
// 화면(ChatScreen)은 UI 시스템이 있어야 열려 EngineTest 의 UI 시나리오 쪽에서 본다.
#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatLogModel.h"
#include "GameFramework/Kits/Feature/Online/Chat/Shared/ChatScreen.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    ChatMessage makeMessage( string_view channelID, uint32 sequence )
    {
        ChatMessage message;
        message._channelID = string( channelID );
        message._sequence  = sequence;
        message._text      = "hello";
        return message;
    }
} // namespace

/**
 * @brief [ChatLogModelTest] 받은 메시지는 시간 순으로 쌓이고 상한을 넘으면 가장 오래된 줄부터 버려지며, 줄마다 판이 올라간다
 */
SW_TEST_CASE( ChatLogModelTest, MessagesAccumulateInOrderAndTheOldestFallOffAtTheCap )
{
    ChatLogModel model;
    SW_EXPECT_TRUE( model.getLines().empty() );

    const uint64 before = model.getRevision();
    for ( uint32 sequence = 0; sequence < ChatLogModel::kMaxLines + 5; ++sequence )
    {
        model.appendMessage( makeMessage( "world.1", sequence ) );
    }
    SW_EXPECT_TRUE( model.getRevision() > before );
    SW_ASSERT_EQUAL( ChatLogModel::kMaxLines, model.getLines().size() );
    SW_EXPECT_EQUAL( uint32( 5 ), model.getLines().front()._sequence ); // 앞의 다섯 줄이 버려졌다
    SW_EXPECT_EQUAL( uint32( ChatLogModel::kMaxLines + 4 ), model.getLines().back()._sequence );
}

/**
 * @brief [ChatLogModelTest] 빈 글과 클라이언트 없음은 보내지 않고 이유를 결과에 남기며, 보내기 응답은 결과와 기다릴 시간을 바꾼다
 */
SW_TEST_CASE( ChatLogModelTest, UnsendableTextKeepsItsReasonAndRepliesSetTheResult )
{
    ChatLogModel model;
    SW_EXPECT_FALSE( model.send( "world.1", "" ) );
    SW_EXPECT_TRUE( model.getLastResult() == ChatResult::Invalid );
    SW_EXPECT_FALSE( model.send( "world.1", "hi" ) ); // 클라이언트가 없다
    SW_EXPECT_TRUE( model.getLastResult() == ChatResult::Unavailable );

    ChatClient::Reply reply;
    reply._reply._result       = ChatResult::RateLimited;
    reply._reply._retryAfterMs = 1500;
    const uint64 before        = model.getRevision();
    model.onSendReply( reply );
    SW_EXPECT_TRUE( model.getLastResult() == ChatResult::RateLimited );
    SW_EXPECT_EQUAL( int64( 1500 ), model.getRetryAfterMs() );
    SW_EXPECT_TRUE( model.getRevision() > before );

    model.onSendReply( ChatClient::Reply{} );
    SW_EXPECT_TRUE( model.getLastResult() == ChatResult::Ok );
    SW_EXPECT_EQUAL( int64( 0 ), model.getRetryAfterMs() );
}

/**
 * @brief [ChatLogModelTest] 거절 이유마다 상태 문구가 있고(성공만 빈 글), 기다릴 시간은 문구에 들어간다
 */
SW_TEST_CASE( ChatLogModelTest, EveryRejectionHasAStatusText )
{
    SW_EXPECT_TRUE( ChatScreen::makeStatusText( ChatResult::Ok, 0 ).empty() );
    for ( const ChatResult result : { ChatResult::NotMember, ChatResult::NotJoinable, ChatResult::Muted, ChatResult::RateLimited, ChatResult::Repeated, ChatResult::Rejected, ChatResult::TargetOffline,
                                      ChatResult::Invalid, ChatResult::Unavailable, ChatResult::TooManyChannels, ChatResult::NotSignedIn, ChatResult::UpdateRequired, ChatResult::NotFound } )
    {
        SW_EXPECT_FALSE( ChatScreen::makeStatusText( result, 0 ).empty() );
    }
    SW_EXPECT_TRUE( ChatScreen::makeStatusText( ChatResult::RateLimited, 2500 ).find( "2500" ) != string::npos );
}
