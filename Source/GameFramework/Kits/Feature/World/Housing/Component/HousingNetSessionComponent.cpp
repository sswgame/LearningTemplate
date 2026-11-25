#include "pch.h"

#include "GameFramework/Kits/Feature/World/Housing/Component/HousingNetSessionComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Object/GameObject/Tick/TickContext.h"

#include "GameFramework/Kits/Feature/World/Housing/Component/HousingPlotComponent.h"

namespace sw
{
    HousingNetSessionComponent::HousingNetSessionComponent()
        : _maxRequestsPerTick{ 8 }
        , _server{}
        , _client{}
        , _locateCaller{}
        , _listPlot{}
        , _listPlotComponent{}
        , _listRequest{}
        , _listReply{}
        , _routerSubscription{}
        , _bServer{ SW_FALSE }
        , _bStarted{ SW_FALSE }
        , _reserved{ 0 }
    {
        setCanEverTick( true );
    }

    HousingNetSessionComponent::~HousingNetSessionComponent() = default;

    void HousingNetSessionComponent::setCallerLocator( const HousingCallerLocator& locator )
    {
        _locateCaller = locator;
        _server.setCallerLocator( locator );
    }

    void HousingNetSessionComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        _routerSubscription.reset();
        _bStarted = SW_FALSE;
        // 넷 링크는 이 컴포넌트의 월드에 올라 있다 - 전역 세션을 찾지 않는다(`SceneNetLink`). 링크가 비면 혼자 하는 게임이고 부지는 편집을 바로 적용한다.
        const NetLinkValue* pLink = SceneNetLink::findBoundFor( *this );
        if ( pLink == nullptr )
            return;
        const NetLinkValue& link = *pLink;
        _bServer                 = link._role == NetLinkRole::Server ? SW_TRUE : SW_FALSE;
        if ( _bServer == SW_TRUE )
        {
            HousingServerConfig config;
            config._maxRequestsPerTick = _maxRequestsPerTick;
            config._locateCaller       = _locateCaller;
            if ( _server.initialize( link._pPeers, config ) == false )
                return; // 오류는 남겼다 - 처리기를 달지 않아 세션이 멈춘 채다(`isActive` false)
            _routerSubscription = link._pRouter->registerHandler( &_server );
        }
        else
        {
            _client.initialize( link._pPeers );
            _routerSubscription = link._pRouter->registerHandler( &_client );
        }
        if ( _routerSubscription.isActive() )
            _bStarted = SW_TRUE;
        else
            SW_LOG_ERROR_ID( "HousingNetSession.HandlerRefused", "Housing net session: the router refused the housing handler (another handler holds its message range) - housing is not networked" );
    }

    void HousingNetSessionComponent::onEndPlay()
    {
        _routerSubscription.reset();
        _bStarted = SW_FALSE;
        _listPlot.clear();
        _listPlotComponent.clear();
        Component::onEndPlay();
    }

    void HousingNetSessionComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        if ( _bStarted == SW_FALSE )
            return;
        context.deferPostTickOnComponent( *this, []( HousingNetSessionComponent& session, GameObjectManager& manager )
        { session.poll( manager ); } );
    }

    void HousingNetSessionComponent::poll( GameObjectManager& manager )
    {
        const bool bSkipped = _bStarted == SW_FALSE || ( _bServer == SW_TRUE && isActive() == false );
        if ( bSkipped )
            return;
        _listPlot.clear();
        _listPlotComponent.clear();
        for ( HousingPlotComponent* pPlot : manager.getComponentRegistry().getAll<HousingPlotComponent>() )
        {
            if ( pPlot->isStarted() == false )
                continue; // 아직 열리지 않은 부지 - 클라이언트가 받은 상태는 열릴 때까지 들고 있는다
            _listPlot.push_back( pPlot->makeNetPlot() );
            _listPlotComponent.push_back( pPlot );
        }
        if ( _bServer == SW_TRUE )
        {
            _server.update( _listPlot );
            return;
        }
        if ( isActive() == false )
        {
            // 플레이 중에 세션이 닫혔다 — 닫힌 창구로 보내지 않고, 부지가 오지 않을 답을 기다리지 않게 한다(뒤에 낸 편집도 여기서 버린다).
            for ( HousingPlotComponent* pPlot : _listPlotComponent )
                pPlot->abandonNetRequests();
            return;
        }
        // 붙기 전에 낸 요청(첫 프레임의 차지)은 버리지 않고 부지에 남겨 둔다 — 버리면 답이 없어 남의 부지로 읽힌다(nethousing).
        const bool bAwaitingServer = _client.isAwaitingServer();
        for ( HousingPlotComponent* pPlot : _listPlotComponent )
        {
            if ( bAwaitingServer )
                break;
            _listRequest.clear();
            pPlot->takeOutgoingEdits( _listRequest );
            for ( const HousingEditRequest& request : _listRequest )
            {
                if ( _client.sendEdit( pPlot->getSettings()._plotID, request ) == 0 )
                    pPlot->abandonNetRequest(); // 서버와 끊겼다 - 답이 오지 않는다
            }
        }
        _listReply.clear();
        _client.update( _listPlot, _listReply );
        for ( const HousingEditReply& reply : _listReply )
        {
            _listPlotComponent[reply._plotIndex]->receiveNetReply( reply._result );
        }
        const bool bServerLost = _client.isServerLost();
        for ( HousingPlotComponent* pPlot : _listPlotComponent )
        {
            if ( bServerLost )
                pPlot->abandonNetRequests();
            pPlot->setReplicatedOwner( _client.findOwner( pPlot->getSettings()._plotID ) );
        }
    }
} // namespace sw
