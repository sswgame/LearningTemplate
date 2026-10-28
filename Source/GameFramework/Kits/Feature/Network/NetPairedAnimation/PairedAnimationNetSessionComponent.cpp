#include "pch.h"

#include "GameFramework/Kits/Feature/Network/NetPairedAnimation/PairedAnimationNetSessionComponent.h"

#include "Engine/Net/GameNetSession.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"

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

        // 넷 링크는 이 컴포넌트의 월드(오브젝트 매니저)에 올라 있다 - 전역 세션을 찾지 않는다(`SceneNetLink`). 링크가 비면 혼자 하는 게임이다.
        GameObject*         pOwner     = getOwner();
        GameObjectManager*  pManager   = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SceneNetLink* pLink      = pManager != nullptr ? &pManager->getSubsystem<SceneNetLink>() : nullptr;
        const bool          bNoSession = pLink == nullptr || pLink->isBound() == false;
        if ( bNoSession )
            return; // 혼자 하는 게임 - 복제할 상대가 없다
        const NetLinkValue& link = pLink->get();

        _bServer                     = link._role == NetLinkRole::Server ? SW_TRUE : SW_FALSE;
        INetMessageHandler* pHandler = nullptr;
        if ( _bServer == SW_TRUE )
        {
            _server.initialize( link._pPeers, pManager );
            pHandler = &_server;
        }
        else
        {
            _client.initialize( pManager );
            pHandler = &_client;
        }
        _routerSubscription = link._pRouter->registerHandler( pHandler );
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

    void PairedAnimationNetSessionComponent::reportReplicaStats()
    {
        // 복제 표(프로파일러의 `replicas=`)는 전역 세션의 것이다 - 이 월드의 링크가 전역 세션의 창구일 때만 올린다(가상 서버 하니스의 월드는 올리지 않는다).
        GameNetSession*     pNetSession  = game::findSubsystem<GameNetSession>();
        GameObject*         pOwner       = getOwner();
        GameObjectManager*  pManager     = pOwner != nullptr ? pOwner->getManager() : nullptr;
        const SceneNetLink* pLink        = pManager != nullptr ? &pManager->getSubsystem<SceneNetLink>() : nullptr;
        const bool          bGlobalWorld = pNetSession != nullptr && pLink != nullptr && pLink->isBound() && pLink->get()._pPeers == &pNetSession->getPeers();
        if ( bGlobalWorld == false )
            return;
        // 이 키트는 바이트를 세지 않는다(0) — 갱신 수만 오른다. 서버는 보낸 상태 · 끝 메시지, 클라이언트는 복원 · 끝으로 중단한 수이다.
        const PairedAnimationReplicationStats& stats       = _bServer == SW_TRUE ? _server.getStats() : _client.getStats();
        const uint64                           updateCount = _bServer == SW_TRUE ? static_cast<uint64>( stats._stateSentCount ) + stats._endSentCount
                                                                                 : static_cast<uint64>( stats._appliedCount ) + stats._endAppliedCount;
        (void)pNetSession->setReplicaStats( "paired_animation", updateCount, 0 );
    }

    void PairedAnimationNetSessionComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        const bool bSends = _routerSubscription.isActive() && _bServer == SW_TRUE;
        if ( bSends )
            _server.update();
        if ( _routerSubscription.isActive() )
            reportReplicaStats();
        // 클라이언트: 클립을 읽는 중이라 보관한 상태는 준비된 뒤 복원한다 — 세션이 다른 오브젝트의 애니메이터를 바꾸므로 틱 뒤에 한다.
        const bool         bRetries = _routerSubscription.isActive() && _bServer == SW_FALSE && _client.getPendingCount() > 0;
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        if ( bRetries && pManager != nullptr )
        {
            pManager->executeOrDeferPostTickOnComponent( *this, []( PairedAnimationNetSessionComponent& component, GameObjectManager& manager )
            {
                (void)manager;
                component._client.retryPending();
            } );
        }
    }
} // namespace sw
