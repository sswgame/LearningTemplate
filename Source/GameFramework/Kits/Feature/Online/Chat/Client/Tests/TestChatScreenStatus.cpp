// ChatScreenStatusTest — 보내기 결과의 상태 글: 성공은 빈 글, 업무 결과마다 다른 글, 기다릴 시간은 글에 든다, 공통 오류가 업무 결과보다 앞선다(화면 없음).
#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Online/Chat/Client/ChatScreen.h"

using namespace sw;

/** @brief [ChatScreenStatusTest] 성공만 빈 글이고 나머지 업무 결과는 서로 다른 글이다 */
SW_TEST_CASE( ChatScreenStatusTest, EveryResultHasItsOwnText )
{
    SW_EXPECT_TRUE( ChatScreen::makeStatusText( ChatServiceResult{ ChatResult::Ok }, 0 ).empty() );
    vector<string> listText;
    for ( uint32 index = static_cast<uint32>( ChatResult::NotMember ); index < static_cast<uint32>( ChatResult::Count ); ++index )
    {
        const string text = ChatScreen::makeStatusText( ChatServiceResult{ static_cast<ChatResult>( index ) }, 1500 );
        SW_EXPECT_FALSE( text.empty() );
        for ( const string& other : listText )
            SW_EXPECT_FALSE( other == text );
        listText.push_back( text );
    }
    SW_EXPECT_TRUE( StringUtil::contains( ChatScreen::makeStatusText( ChatServiceResult{ ChatResult::Muted }, 12000 ), "12" ) );
    SW_EXPECT_TRUE( StringUtil::contains( ChatScreen::makeStatusText( ChatServiceResult{ ChatResult::RateLimited }, 250 ), "250" ) );
}

/** @brief [ChatScreenStatusTest] 공통 오류는 업무 결과와 무관한 글이고, 판이 다르면 갱신하라고, 모르는 오류는 쓸 수 없다고 한다 */
SW_TEST_CASE( ChatScreenStatusTest, TransportErrorsComeFirst )
{
    ChatServiceResult failed = ChatServiceResult::fail( OnlineError::kUnauthenticated );
    failed._result           = ChatResult::Muted;
    const string signIn      = ChatScreen::makeStatusText( failed, 5000 );
    SW_EXPECT_EQUAL( ChatScreen::makeStatusText( ChatServiceResult::fail( OnlineError::kUnauthenticated ), 0 ), signIn );
    SW_EXPECT_EQUAL( ChatScreen::makeStatusText( ChatServiceResult::fail( OnlineError::kVersionMismatch ), 0 ),
                     ChatScreen::makeStatusText( ChatServiceResult::fail( OnlineError::kUpdateRequired ), 0 ) );
    SW_EXPECT_EQUAL( ChatScreen::makeStatusText( ChatServiceResult::fail( OnlineError::kUnavailable ), 0 ), ChatScreen::makeStatusText( ChatServiceResult::fail( OnlineError::kNotFound ), 0 ) );
    SW_EXPECT_FALSE( signIn == ChatScreen::makeStatusText( ChatServiceResult::fail( OnlineError::kInvalidRequest ), 0 ) );
}
