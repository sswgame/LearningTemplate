#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetSequence/SequenceNetSessionComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"

namespace sw
{
    SequenceNetSessionComponent::SequenceNetSessionComponent()
        : _server{}
        , _client{}
        , _routerSubscription{}
        , _bServer{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    void SequenceNetSessionComponent::onBeginPlay()
    {
        Component::onBeginPlay();

        // 넷 링크는 이 컴포넌트의 월드에 올라 있다 — 전역 세션을 찾지 않는다(`SceneNetLink`). 링크가 비면 혼자 하는 게임이다.
        GameObject*         pOwner   = getOwner();
        GameObjectManager*  pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        const NetLinkValue* pLink    = SceneNetLink::findBoundFor( *this );
        if ( pLink == nullptr )
            return; // 혼자 하는 게임 — 복제할 상대가 없다
        const NetLinkValue& link     = *pLink;
        _bServer                     = link._role == NetLinkRole::Server ? SW_TRUE : SW_FALSE;
        INetMessageHandler* pHandler = nullptr;
        if ( _bServer == SW_TRUE )
        {
            _server.initialize( link._pPeers, pManager );
            pHandler = &_server;
        }
        else
        {
            _client.initialize( link._pPeers, pManager );
            pHandler = &_client;
        }
        _routerSubscription = link._pRouter->registerHandler( pHandler );
        if ( _routerSubscription.isActive() == false )
        {
            SW_LOG_ERROR_ID( "SequenceNetSession.RangeTaken", "Sequence net session not started: another handler owns the sequence message range" );
            return;
        }
        SW_LOG_INFO_ID( "SequenceNetSession.Started", "Sequence net session started as %#", _bServer == SW_TRUE ? "server" : "client" );
    }

    void SequenceNetSessionComponent::onEndPlay()
    {
        _routerSubscription.reset();
        Component::onEndPlay();
    }

    void SequenceNetSessionComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _routerSubscription.isActive() == false )
            return;
        // 보내기는 디렉터(다른 오브젝트)의 재생 상태를 읽고 서버에서는 건너뛰기로 바꾼다 — 틱 뒤 게임 스레드에서 한다.
        context.deferPostTickOnComponent( *this, []( SequenceNetSessionComponent& component, GameObjectManager& manager )
        {
            (void)manager;
            if ( component._bServer == SW_TRUE )
                component._server.update();
            else
                component._client.update();
        } );
    }
} // namespace sw
