// AndroidInputConnectionState — Android InputConnection 호출을 조합 · 확정 글과 지우기 키로 옮긴다: 한글 조합 뒤 확정, 일본어 변환 뒤 조합 끝, 조합 없는 확정, 커서 주변 지우기(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Window/AndroidInputConnectionState.h"

using namespace sw;

namespace
{
    /** @brief 동작 목록을 `C:글` · `T:글` · `B` · `D` 로 이은 글입니다(기대값을 한 줄로 적는다). */
    string describe( const vector<AndroidTextAction>& listAction )
    {
        string text;
        for ( const AndroidTextAction& action : listAction )
        {
            if ( text.empty() == false )
                text += " ";
            switch ( action._kind )
            {
                case AndroidTextActionKind::Composition:
                    text += "C:" + action._text;
                    break;
                case AndroidTextActionKind::Commit:
                    text += "T:" + action._text;
                    break;
                case AndroidTextActionKind::BackspaceKey:
                    text += "B";
                    break;
                case AndroidTextActionKind::DeleteKey:
                    text += "D";
                    break;
            }
        }
        return text;
    }
} // namespace

/**
 * @brief [AndroidInputConnectionStateTest] 한국어 입력기: 조합 글이 "ㅎ" → "하" → "한" 으로 바뀌고 commitText 가 조합을 끝내며 확정한다.
 *        결함이면 조합 중 글이 화면에 안 보이거나(조합 이벤트 없음) 확정 뒤 조합 밑줄이 남는다
 */
SW_TEST_CASE( AndroidInputConnectionStateTest, KoreanComposeThenCommit )
{
    AndroidInputConnectionState state;
    vector<AndroidTextAction>   listAction;
    state.setComposingText( "ㅎ", listAction );
    state.setComposingText( "하", listAction );
    state.setComposingText( "한", listAction );
    state.commitText( "한", listAction );
    SW_EXPECT_EQUAL( string( "C:ㅎ C:하 C:한 C: T:한" ), describe( listAction ) );
    SW_EXPECT_TRUE( state.getComposingText().empty() );
}

/**
 * @brief [AndroidInputConnectionStateTest] 일본어 입력기: 변환한 조합을 finishComposingText 가 그대로 확정하고 조합을 비운다. 조합이 없을 때 finish · 빈 조합은 아무것도 내지 않는다
 */
SW_TEST_CASE( AndroidInputConnectionStateTest, FinishCommitsTheComposition )
{
    AndroidInputConnectionState state;
    vector<AndroidTextAction>   listAction;
    state.finishComposingText( listAction );
    state.setComposingText( "", listAction );
    SW_EXPECT_TRUE( listAction.empty() );
    state.setComposingText( "かんじ", listAction );
    state.setComposingText( "漢字", listAction );
    state.finishComposingText( listAction );
    SW_EXPECT_EQUAL( string( "C:かんじ C:漢字 T:漢字 C:" ), describe( listAction ) );
}

/**
 * @brief [AndroidInputConnectionStateTest] 조합 없는 확정(영문 키보드 · 붙여 넣기)은 확정 하나, 빈 조합으로 취소하면 조합 끝, 포커스를 잃으면 확정 없이 조합만 끝난다
 */
SW_TEST_CASE( AndroidInputConnectionStateTest, PlainCommitAndCancel )
{
    AndroidInputConnectionState state;
    vector<AndroidTextAction>   listAction;
    state.commitText( "a", listAction );
    state.setComposingText( "ㄱ", listAction );
    state.setComposingText( "", listAction );
    state.setComposingText( "가", listAction );
    state.cancelComposition( listAction );
    SW_EXPECT_EQUAL( string( "T:a C:ㄱ C: C:가 C:" ), describe( listAction ) );
}

/**
 * @brief [AndroidInputConnectionStateTest] 주변 지우기는 조합을 그대로 두고 앞 · 뒤 수만큼 지우기 키를 낸다(각 상한까지 — 입력기가 큰 수를 보내도 키 폭주가 없다)
 */
SW_TEST_CASE( AndroidInputConnectionStateTest, DeleteSurroundingEmitsKeys )
{
    AndroidInputConnectionState state;
    vector<AndroidTextAction>   listAction;
    state.setComposingText( "한", listAction );
    listAction.clear();
    state.deleteSurroundingText( 2, 1, listAction );
    SW_EXPECT_EQUAL( string( "B B D" ), describe( listAction ) );
    SW_EXPECT_EQUAL( string( "한" ), state.getComposingText() );

    listAction.clear();
    state.deleteSurroundingText( 1000000, 0, listAction );
    SW_EXPECT_EQUAL( size_t{ AndroidInputConnectionState::kMaxDeleteKeys }, listAction.size() );
}
