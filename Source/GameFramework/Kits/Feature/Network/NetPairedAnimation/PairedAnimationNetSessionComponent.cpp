#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetPairedAnimation/PairedAnimationNetSessionComponent.h"

#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    SW_LOG_CALLER( "PairedAnimationNetSessionComponent" );

    PairedAnimationNetSessionComponent::PairedAnimationNetSessionComponent()
        : _server{}
        , _client{}
        , _routerSubscription{}
        , _bServer{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    void PairedAnimationNetSessionComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 세션 갱신(프레임 시스템 단계) 뒤에 보낸다 — 마지막 그룹에서 돈다.
        setTickGroup( TickGroup::PostUpdate );

        GameNetSession*    pNetSession = game::findSubsystem<GameNetSession>();
        GameObject*        pOwner      = getOwner();
        GameObjectManager* pManager    = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const bool         bNoSession  = pNetSession == nullptr || pNetSession->isActive() == false || pManager == nullptr;
        if ( bNoSession )
            return; // 혼자 하는 게임 — 복제할 상대가 없다

        _bServer                     = pNetSession->getRole() == GameNetRole::Server ? SW_TRUE : SW_FALSE;
        INetMessageHandler* pHandler = nullptr;
        if ( _bServer == SW_TRUE )
        {
            _server.initialize( &pNetSession->getPeers(), pManager );
            pHandler = &_server;
        }
        else
        {
            _client.initialize( pManager );
            pHandler = &_client;
        }
        _routerSubscription = pNetSession->getRouter().registerHandler( pHandler );
        if ( _routerSubscription.isActive() == false )
        {
            SW_LOG_ERROR( "Paired animation net session not started: another handler owns the paired animation message range" );
            return;
        }
        SW_LOG_INFO( "Paired animation net session started as %#", _bServer == SW_TRUE ? "server" : "client" );
    }

    void PairedAnimationNetSessionComponent::onEndPlay()
    {
        _routerSubscription.reset();
        Component::onEndPlay();
    }

    void PairedAnimationNetSessionComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const bool bSends = _routerSubscription.isActive() && _bServer == SW_TRUE;
        if ( bSends )
            _server.update();
    }
} // namespace sw
