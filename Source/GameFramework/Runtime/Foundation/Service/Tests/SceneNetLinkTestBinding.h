/**
 * @file SceneNetLinkTestBinding.h
 * @brief 단위 테스트가 월드 하나를 서버 · 클라이언트 넷 월드로 만드는 장치입니다 — 아무 데도 보내지 않는 창구와 빈 라우터를 씬 넷 링크(`SceneNetLink`)에 올립니다(nogpu, 연결 없음).
 * @details 판정 자리(`TriggerAuthority`)처럼 링크의 역할만 읽는 코드를 볼 때 쓴다. 메시지를 주고받는 테스트는 하니스(`NetSimWorld`)를 쓴다.
 */
#pragma once
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Tests/FakeNetPeers.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"

namespace sw
{
    /** @brief @p manager 의 월드에 역할 @p role 의 넷 링크를 올리고, 사라질 때 내립니다. 매니저보다 먼저 사라져야 한다(지역 변수는 매니저 뒤에 둔다). */
    class SceneNetLinkTestBinding
    {
    public:
        SceneNetLinkTestBinding( GameObjectManager& manager, NetLinkRole role, bool bTicketRequired = false )
            : _peers{ role == NetLinkRole::Server }
            , _router{}
            , _manager{ manager }
        {
            if ( role == NetLinkRole::Client )
                _peers._clientIndex = 0;
            NetLinkValue link;
            link._pPeers          = &_peers;
            link._pRouter         = &_router;
            link._role            = role;
            link._bTicketRequired = bTicketRequired ? SW_TRUE : SW_FALSE;
            SW_REQUIRE_TRUE( _manager.getSubsystem<SceneNetLink>().bind( link, 0 ) );
        }
        /** @brief 세션이 닫힌 것처럼 라우터를 닫습니다 — 링크는 저절로 빈 것이 된다. */
        void closeSession() { _router.closeSession(); }
        ~SceneNetLinkTestBinding() { _manager.getSubsystem<SceneNetLink>().unbind(); }

        SceneNetLinkTestBinding( const SceneNetLinkTestBinding& )            = delete;
        SceneNetLinkTestBinding& operator=( const SceneNetLinkTestBinding& ) = delete;

    private:
        test::FakeNetPeers _peers;
        NetMessageRouter   _router;
        GameObjectManager& _manager;
    };
} // namespace sw
