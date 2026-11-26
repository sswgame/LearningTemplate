#include "pch.h"

#include "Games/Playground/Hub/HubWorldComponent.h"

#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Subsystem/SceneNetLink.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Fixed/ReplicatedLayout.h"

#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

namespace sw
{
    HubWorldComponent::HubWorldComponent()
        : _rules{}
        , _interestGrid{}
        , _pServer{}
        , _client{}
        , _linkSessionSerial{ 0 }
        , _bRulesValid{ SW_FALSE }
        , _bLinkTried{ SW_FALSE }
    {
        setCanEverTick( true );
        _interestGrid._cellSize      = 32.0f;
        _interestGrid._loadingRadius = 96.0f;
    }

    int32 HubWorldComponent::getNetRoleValue() const
    {
        if ( _pServer != nullptr )
            return 1;
        return _client.isActive() ? 2 : 0;
    }

    void HubWorldComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        HubWorld rulesCheck;
        _bRulesValid = rulesCheck.initialize( _rules ) ? SW_TRUE : SW_FALSE;
        _bLinkTried  = SW_FALSE;
        tryStartLink();
    }

    void HubWorldComponent::tryStartLink()
    {
        // 세션은 기동 때(전용 서버) 열리거나 플레이 중에(클라이언트 `net.ticketconnect` · 온라인 흐름) 열린다 — 링크가 걸리는 첫 틱에 한 번 세운다.
        const NetLinkValue* pLink = SceneNetLink::findBoundFor( *this );
        if ( _bLinkTried != SW_FALSE && ( pLink == nullptr || pLink->_sessionSerial != _linkSessionSerial ) )
        {
            stopLink(); // 매치 서버로 옮기며 허브 세션을 떠났거나 서버가 세션을 닫았다(틱 사이에 새 세션이 열린 것까지 번호로 가린다) — 다시 걸리면 새로 세운다
            _bLinkTried = SW_FALSE;
        }
        if ( _bLinkTried != SW_FALSE || _bRulesValid == SW_FALSE || pLink == nullptr )
            return;
        _bLinkTried              = SW_TRUE;
        _linkSessionSerial       = pLink->_sessionSerial;
        const NetLinkValue& link = *pLink;
        if ( link._role == NetLinkRole::Server )
        {
            // 허브는 계정마다 진행을 저장한다 — 티켓을 요구하지 않는 세션(개발 · 리슨 호스트)에는 계정이 없어 허브 서버를 세우지 않는다. 접속한 클라이언트는 아바타 없이 같은 씬에 있다.
            if ( link._bTicketRequired == SW_FALSE )
            {
                SW_LOG_INFO_ID( "Hub.NoTicketSession", "[Hub] this session does not require game connect tickets (server config _bGameConnectTicket) - the hub server does not start" );
                return;
            }
            if ( startServer( *link._pPeers, *link._pRouter ) == false )
                SW_LOG_ERROR_ID( "Hub.ServerNotStarted", "[Hub] the hub server did not start - see the errors above" );
            return;
        }
        if ( _client.initialize( *link._pPeers, *link._pRouter ) == false )
            SW_LOG_ERROR_ID( "Hub.ClientNotStarted", "[Hub] the hub client did not start - see the errors above" );
    }

    bool HubWorldComponent::startServer( INetPeers& peers, NetMessageRouter& router )
    {
        // 서버 링크는 서버 전용 코드다 — 클라이언트 구성에는 공장이 없다. 진행 저장소는 전용 서버의 온라인 조립 저장소다(공장이 찾는다).
        IHubServerFactory* const pFactory = game::findSubsystem<IHubServerFactory>();
        if ( pFactory == nullptr )
        {
            SW_LOG_ERROR_ID( "Hub.ServerCodeMissing", "[Hub] this build has no hub server code (client configuration) - the server role cannot start" );
            return false;
        }
        _pServer = pFactory->createServer( peers, router, _rules, _interestGrid );
        return _pServer != nullptr;
    }

    void HubWorldComponent::stopLink()
    {
        if ( _pServer != nullptr )
            _pServer->shutdown();
        _pServer.reset();
        _client.shutdown();
    }

    void HubWorldComponent::onEndPlay()
    {
        stopLink();
        Component::onEndPlay();
    }

    void HubWorldComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        tryStartLink();
        if ( _pServer != nullptr )
            _pServer->update( context.getDeltaTime().count() );
        _client.update();
    }
} // namespace sw
