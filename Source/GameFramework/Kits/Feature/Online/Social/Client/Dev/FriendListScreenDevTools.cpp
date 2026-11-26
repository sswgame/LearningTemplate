/**
 * @file FriendListScreenDevTools.cpp
 * @brief `FriendListScreen` 의 개발 명령입니다. `Dev/` 라 배포 구성은 짓지 않는다.
 */
#include "pch.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Localization/LocText.h"
#include "Engine/UI/UISystem.h"
#include "Engine/UI/WidgetTree/WidgetTree.h"
#include "Engine/UI/Widgets/ListViewWidget.h"
#include "Engine/UI/Widgets/TextWidget.h"

#include "GameFramework/Kits/Feature/Online/Social/Client/FriendListScreen.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct FriendListCommandDevInternal
        {
            static bool runOpen( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                FriendList* const pFriends = game::findSubsystem<FriendList>();
                UISystem* const   pUI      = game::findSubsystem<UISystem>();
                if ( pFriends == nullptr || pUI == nullptr )
                    outReply = "social.friendlist: no friend list model in the game scope";
                else
                    outReply = FriendListScreen::open( *pUI, listArgument[0], *pFriends ) != kInvalidUIScreenHandle ? "social.friendlist: opened" : "social.friendlist: the document did not open";
                return true;
            }
        };
    } // namespace

    // 게임은 친구 목록 모델(`FriendList`)을 게임 범위에 건다 — 게임 모듈은 클라이언트 키트를 링크하지 않으므로 화면은 키트가 연다.
    SW_DEV_COMMAND( SocialFriendList, "social.friendlist", "social.friendlist <ui document>", "Open the friend list screen on the game's friend list model", &FriendListCommandDevInternal::runOpen );
} // namespace sw
