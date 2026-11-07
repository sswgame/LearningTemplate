// FriendListRowTest — 친구 목록 화면의 줄 위젯: 계정 · 접속 상태 · 활동이 글에 보이고, 같은 줄을 다른 친구에 다시 이어도 앞 값이 남지 않는다(가상 목록이 줄을 다시 쓴다). 온라인 키트라 `UnitTests.OnlineKits` 가 돌린다(nogpu).
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Kits/Feature/Online/Social/Runtime/FriendListScreen.h"

using namespace sw;

/** @brief [FriendListRowTest] 활동이 있으면 함께 보이고, 없는 친구에 다시 이으면 사라진다 */
SW_TEST_CASE( FriendListRowTest, ReusedRowShowsTheFriendItIsBoundTo )
{
    unique_ptr<Widget> row = FriendListScreen::makeRow();
    FriendEntry        entry;
    entry._accountID = AccountID{ 7 };
    entry._status    = SocialPresenceStatus::InGame;
    entry._activity  = "arena";
    FriendListScreen::bindRow( *row, entry );
    SW_EXPECT_EQUAL( string( "#7  in game  arena" ), static_cast<const TextWidget&>( *row ).getText() );

    entry._status = SocialPresenceStatus::Offline;
    entry._activity.clear();
    FriendListScreen::bindRow( *row, entry );
    SW_EXPECT_EQUAL( string( "#7  offline" ), static_cast<const TextWidget&>( *row ).getText() );
}
