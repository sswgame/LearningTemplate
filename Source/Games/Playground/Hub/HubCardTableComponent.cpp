#include "pch.h"

#include "Games/Playground/Hub/HubCardTableComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Network/Message/NetMessage.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Service/SceneNetLink.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        /** @brief 개발 명령입니다. 탁자는 활성 씬에서 찾습니다. */
        struct HubCardTableInternal
        {
            [[nodiscard]] static HubCardTableComponent* findTable()
            {
                const SceneManager* const pScenes  = game::findSubsystem<SceneManager>();
                const Scene* const        pScene   = pScenes != nullptr ? pScenes->getActiveScene() : nullptr;
                GameObjectManager* const  pManager = pScene != nullptr ? pScene->getObjectManager() : nullptr;
                return pManager != nullptr ? pManager->getComponentRegistry().findUnique<HubCardTableComponent>( []( const HubCardTableComponent& table )
                { return table.isClient(); } )
                                           : nullptr;
            }

            static bool runCards( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 || ( listArgument[0] != "join" && listArgument[0] != "play" && listArgument[0] != "draw" ) )
                    return false;
                HubCardTableComponent* const pTable = findTable();
                if ( pTable == nullptr )
                {
                    outReply = "hub.cards: no connected card table in the active scene";
                    return true;
                }
                const bool bSent = listArgument[0] == "join" ? pTable->requestSeat() : pTable->requestPlay( listArgument[0] == "draw" );
                outReply         = bSent ? "hub.cards: sent" : "hub.cards: refused - already seated, or no game yet";
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool HubCardTurnPolicy::replay( uint32 roomID, const vector<TurnAction>& listAction, SheddingCardGame& outGame )
    {
        SheddingCardSettings settings;
        settings._playerCount = kSeatCount;
        if ( outGame.initialize( settings, roomID ) == false )
            return false;
        for ( const TurnAction& turn : listAction )
        {
            CardAction action;
            if ( CardAction::decodeAction( turn._buffer, action ) == false || outGame.applyAction( turn._seat, action ) == false )
                return false;
        }
        return true;
    }

    bool HubCardTurnPolicy::isActionAllowed( const TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const
    {
        // 클라이언트가 보낸 바이트다 — 형식 · 차례 · 손패 · 규칙을 모두 판이 본다. 한 판의 행동은 백 안팎이라 다시 돌린다.
        SheddingCardGame game;
        CardAction       action;
        if ( CardAction::decodeAction( actionBuffer, action ) == false )
            return false;
        const bool bOutOfTurn = seat != room._currentSeat && action._kind != static_cast<uint8>( SheddingCardActionKind::CallLastCard );
        if ( bOutOfTurn )
            return false; // 남의 차례에 퍼붓는 행동마다 판을 다시 돌리지 않는다
        return replay( room._roomID, room._listAction, game ) && game.getWinner() < 0 && game.applyAction( seat, action );
    }

    void HubCardTurnPolicy::applyAction( TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const
    {
        (void)seat;
        (void)actionBuffer;
        SheddingCardGame game;
        if ( replay( room._roomID, room._listAction, game ) ) // 받은 행동은 이미 목록에 있다
            room._currentSeat = game.getCurrentPlayer();
    }
} // namespace sw

namespace sw
{
    HubCardTableComponent::HubCardTableComponent()
        : _server{}
        , _client{}
        , _policy{}
        , _routerSubscription{}
        , _listEventScratch{}
        , _roomID{ kFirstRoomID }
        , _rejectedCount{ 0 }
        , _role{ Role::None }
        , _bStartTried{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    void HubCardTableComponent::onEndPlay()
    {
        stopRole();
        Component::onEndPlay();
    }

    void HubCardTableComponent::onTick( TickContext& context )
    {
        Component::onTick( context );
        // 소켓으로 보내는 일이라 틱 워커에서 돌지 않는다.
        context.deferPostTickOnComponent( *this, []( HubCardTableComponent& table )
        {
            const NetLinkValue* const pLink = SceneNetLink::findBoundFor( table );
            if ( pLink == nullptr )
            {
                table.stopRole();
                return;
            }
            if ( table._role != Role::None && table._routerSubscription.isActive() == false )
                table.stopRole(); // 틱 사이에 세션이 닫히고 새로 열렸다 — 새 세션에 다시 건다
            table.startRole( *pLink );
            if ( table._role == Role::Server )
                table._server.update();
            else if ( table._role == Role::Client )
                table.pollClientEvents();
        } );
    }

    void HubCardTableComponent::startRole( const NetLinkValue& link )
    {
        const bool bServer    = link._role == NetLinkRole::Server;
        const bool bNoAccount = bServer && link._bTicketRequired == SW_FALSE; // 계정 없는 세션(개발 · 리슨 호스트)에는 탁자를 세우지 않는다
        if ( _bStartTried == SW_TRUE || bNoAccount )
            return;
        _bStartTried = SW_TRUE;
        if ( bServer )
        {
            TurnRelayConfig config;
            config._seatCount         = HubCardTurnPolicy::kSeatCount;
            config._maxRoomCount      = static_cast<int32>( kMaxRoomID );
            config._maxActionsPerRoom = kMaxActionsPerRoom;
            if ( _server.initialize( link._pPeers, config, &_policy ) )
                _routerSubscription = link._pRouter->registerHandler( &_server );
        }
        else
        {
            _client.initialize( link._pPeers );
            _routerSubscription = link._pRouter->registerHandler( &_client );
        }
        if ( _routerSubscription.isActive() == false )
        {
            SW_LOG_ERROR_ID( "Hub.CardTableNotStarted", "[Hub] the card table did not start - the turn relay was refused or its message range is already handled" );
            return;
        }
        _role = bServer ? Role::Server : Role::Client;
        if ( _role == Role::Client && _client.getSeat() >= 0 )
            _client.join( _roomID ); // 끊겼다 돌아왔다 — 받은 토큰으로 제자리에 앉고 놓친 행동을 받는다
    }

    void HubCardTableComponent::stopRole()
    {
        _routerSubscription.reset();
        _role        = Role::None;
        _bStartTried = SW_FALSE;
    }

    void HubCardTableComponent::pollClientEvents()
    {
        _listEventScratch.clear();
        _client.pollEvents( _listEventScratch );
        for ( const TurnRelayEvent& event : _listEventScratch )
        {
            if ( event._kind == TurnRelayEvent::Kind::ActionRejected )
            {
                ++_rejectedCount;
                SW_LOG_INFO_ID( "Hub.CardsRejected", "[Hub] the card table refused the action (reason %#)", static_cast<int32>( event._reason ) );
            }
            else if ( event._kind == TurnRelayEvent::Kind::Denied && event._reason == TurnRejectReason::RoomFull && _roomID < kMaxRoomID )
            {
                ++_roomID; // 찬 탁자다 — 다음 탁자에 앉는다
                _client.join( _roomID );
            }
            else if ( event._kind == TurnRelayEvent::Kind::Denied )
                SW_LOG_WARNING_ID( "Hub.CardsNoSeat", "[Hub] no seat at the card tables (reason %#)", static_cast<int32>( event._reason ) );
        }
    }

    bool HubCardTableComponent::requestSeat()
    {
        if ( _role != Role::Client || _client.getSeat() >= 0 )
            return false;
        _client.join( _roomID );
        return true;
    }

    bool HubCardTableComponent::requestPlay( bool bDrawOnly )
    {
        SheddingCardGame game;
        const int32      seat = _client.getSeat();
        if ( _role != Role::Client || _client.isStarted() == false || seat < 0 || HubCardTurnPolicy::replay( _roomID, _client.getActions(), game ) == false )
            return false;
        CardAction chosen;
        chosen._kind          = static_cast<uint8>( SheddingCardActionKind::Draw );
        const CardPile& hand  = game.getHand( seat );
        const bool      bMine = game.getCurrentPlayer() == seat;
        for ( const Card& card : hand.getCards() )
        {
            CardAction play;
            play._kind     = static_cast<uint8>( SheddingCardActionKind::Play );
            play._cardID   = card._id;
            play._targetID = static_cast<uint16>( SheddingCardColor::Red );
            play._amount   = hand.getCount() == 2 ? 1 : 0; // 한 장을 남기면 선언한다
            SheddingCardGame trial;
            if ( bMine && bDrawOnly == false && HubCardTurnPolicy::replay( _roomID, _client.getActions(), trial ) && trial.applyAction( seat, play ) )
            {
                chosen = play;
                break;
            }
        }
        vector<uint8> buffer;
        CardAction::encodeAction( chosen, buffer );
        return _client.submitAction( buffer ) >= 0;
    }

    int32 HubCardTableComponent::getMyTurnValue() const
    {
        SheddingCardGame game;
        const bool       bPlaying = _role == Role::Client && _client.isStarted() && HubCardTurnPolicy::replay( _roomID, _client.getActions(), game );
        return bPlaying && game.getWinner() < 0 && game.getCurrentPlayer() == _client.getSeat() ? 1 : 0;
    }
} // namespace sw

namespace sw
{
    SW_DEV_COMMAND( HubCards, "hub.cards", "hub.cards join | play | draw", "Sit at a card table of the connected hub, play the first legal card (draw without one), or draw", &HubCardTableInternal::runCards );
    SW_AUTOMATION_GETTER_WATCH( hubCardsSeat, "Hub.CardsSeat", "Seat at the card table (-1 without one)", HubCardTableComponent, &HubCardTableComponent::getSeat );
    SW_AUTOMATION_GETTER_WATCH( hubCardsStarted, "Hub.CardsStarted", "1 once both seats of the card table are taken", HubCardTableComponent, &HubCardTableComponent::getStartedValue );
    SW_AUTOMATION_GETTER_WATCH( hubCardsMyTurn, "Hub.CardsMyTurn", "1 while it is this client's turn at the card table", HubCardTableComponent, &HubCardTableComponent::getMyTurnValue );
    SW_AUTOMATION_GETTER_WATCH( hubCardsActions, "Hub.CardsActions", "Actions the server applied at this client's card table", HubCardTableComponent, &HubCardTableComponent::getActionCount );
    SW_AUTOMATION_GETTER_WATCH( hubCardsRejected, "Hub.CardsRejected", "Actions of this client the card table refused", HubCardTableComponent, &HubCardTableComponent::getRejectedCount );
} // namespace sw
