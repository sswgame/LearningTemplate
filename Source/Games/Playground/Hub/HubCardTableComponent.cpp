#include "pch.h"

#include "Games/Playground/Hub/HubCardTableComponent.h"

#include "Core/Foundation/Constants.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Serialization/BitStream.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/Time/MonotonicClock.h"
#include "Core/UUID/UUID.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/GameObjectManagerResolve.inl"
#include "Engine/Object/GameObject/Subsystem/SceneNetLink.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "GameFramework/Runtime/Foundation/Presentation/IHUDViewModelBinder.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"

#include "Games/Playground/Hub/HubCardHUDViewModel.h"

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
                return pManager != nullptr ? HubCardTableComponent::findClientTable( *pManager ) : nullptr;
            }

            /** @brief 섞기의 비밀 키입니다(운영체제 난수 16 바이트). */
            static SipHashKey makeShuffleKey()
            {
                const UUID uuid = UUID::generate();
                ByteReader reader( uuid._arrBytes, sizeof( uuid._arrBytes ) );
                return SipHashKey{ reader.readUint64(), reader.readUint64() }; // 중괄호 초기화는 왼쪽부터 평가한다
            }

            static bool runCards( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 || ( listArgument[0] != "join" && listArgument[0] != "leave" && listArgument[0] != "play" && listArgument[0] != "draw" ) )
                    return false;
                HubCardTableComponent* const pTable = findTable();
                if ( pTable == nullptr )
                {
                    outReply = "hub.cards: no connected card table in the active scene";
                    return true;
                }
                bool bSent = false;
                if ( listArgument[0] == "join" )
                    bSent = pTable->requestSeat();
                else if ( listArgument[0] == "leave" )
                    bSent = pTable->requestLeave();
                else
                    bSent = pTable->requestPlay( listArgument[0] == "draw" );
                outReply = bSent ? "hub.cards: sent" : "hub.cards: refused - already seated, not seated, or no game yet";
                return true;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    HubCardTurnPolicy::HubCardTurnPolicy()
        : _shuffleKey{}
    {
    }

    bool HubCardTurnPolicy::replay( const TurnRoom& room, SheddingCardGame& outGame ) const
    {
        SheddingCardSettings settings;
        settings._playerCount = kSeatCount;
        if ( outGame.initializeSecret( settings, _shuffleKey, room._generation ) == false ) // 방 번호로 섞으면 다시 쓴 방의 판이 지난 판과 같다
            return false;
        for ( const TurnAction& turn : room._listAction )
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
        return replay( room, game ) && game.getWinner() < 0 && game.applyAction( seat, action );
    }

    void HubCardTurnPolicy::applyAction( TurnRoom& room, int32 seat, const vector<uint8>& actionBuffer ) const
    {
        (void)seat;
        (void)actionBuffer;
        SheddingCardGame game;
        if ( replay( room, game ) ) // 받은 행동은 이미 목록에 있다
            room._currentSeat = game.getCurrentPlayer();
    }

    bool HubCardTurnPolicy::isRoomFinished( const TurnRoom& room ) const
    {
        SheddingCardGame game;
        return replay( room, game ) && game.getWinner() >= 0;
    }

    bool HubCardTurnPolicy::writeSeatView( const TurnRoom& room, int32 seat, vector<uint8>& outView ) const
    {
        SheddingCardGame game;
        if ( replay( room, game ) == false )
            return false;
        HubCardSeatView view;
        for ( const Card& card : game.getHand( seat ).getCards() )
        {
            view._listHandCardID.push_back( card._id );
            CardAction play;
            play._kind             = static_cast<uint8>( SheddingCardActionKind::Play );
            play._cardID           = card._id;
            play._targetID         = static_cast<uint16>( SheddingCardColor::Red );
            SheddingCardGame trial = game; // 낼 수 있는지는 규칙 하나(`applyAction`)가 정한다
            if ( trial.applyAction( seat, play ) )
                view._listPlayableCardID.push_back( card._id );
        }
        view._topCardID         = game.getTopCard()._id;
        view._opponentCardCount = game.getHand( kSeatCount - 1 - seat ).getCount();
        view._currentSeat       = game.getCurrentPlayer();
        view._winner            = game.getWinner();
        view._color             = game.getCurrentColor();
        BitWriter writer;
        if ( FixedLayout::write( view, writer ) == false )
            return false; // 칸 범위 밖 — 오류 로그가 남았다
        outView = writer.getBytes();
        return true;
    }

    bool HubCardTurnPolicy::readSeatView( const vector<uint8>& bytes, HubCardSeatView& outView )
    {
        HubCardSeatView view;
        if ( FixedLayout::read( bytes.data(), static_cast<int32>( bytes.size() ), view ) != FixedReadResult::Ok )
            return false;
        bool bInDeck = view._topCardID < SheddingCardGame::kDeckCardCount;
        for ( const uint16 cardID : view._listHandCardID )
            bInDeck = bInDeck && cardID < SheddingCardGame::kDeckCardCount;
        for ( const uint16 cardID : view._listPlayableCardID )
            bInDeck = bInDeck && cardID < SheddingCardGame::kDeckCardCount;
        if ( bInDeck )
            outView = std::move( view );
        return bInDeck;
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
        , _view{}
        , _hudViewModel{}
        , _roomID{ kFirstRoomID }
        , _rejectedCount{ 0 }
        , _role{ Role::None }
        , _bStartTried{ SW_FALSE }
        , _bHUDShown{ SW_FALSE }
    {
        setCanEverTick( true );
    }

    HubCardTableComponent::~HubCardTableComponent() = default;

    HubCardTableComponent* HubCardTableComponent::findClientTable( GameObjectManager& manager )
    {
        return manager.getComponentRegistry().findUnique<HubCardTableComponent>( []( const HubCardTableComponent& table )
        { return table.isClient(); } );
    }

    void HubCardTableComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        updateHUD(); // HUD 문서는 첫 프레임부터 접힌 채여야 한다 — 앉기 전에도 건다
    }

    void HubCardTableComponent::onEndPlay()
    {
        stopRole();
        IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>();
        if ( pBinder != nullptr && _hudViewModel != nullptr && getOwner() != nullptr )
            (void)pBinder->bindHUDViewModel( *getOwner(), nullptr ); // 뷰모델은 이 컴포넌트와 함께 사라진다 — HUD 에서 먼저 뗀다
        _hudViewModel.reset();
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
                table.updateHUD();
                return;
            }
            if ( table._role != Role::None && table._routerSubscription.isActive() == false )
                table.stopRole(); // 틱 사이에 세션이 닫히고 새로 열렸다 — 새 세션에 다시 건다
            table.startRole( *pLink );
            if ( table._role == Role::Server )
                table._server.update( MonotonicClock::nowNanoseconds() );
            else if ( table._role == Role::Client )
                table.pollClientEvents();
            table.updateHUD();
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
            _policy.setShuffleKey( HubCardTableInternal::makeShuffleKey() );
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
            else if ( event._kind == TurnRelayEvent::Kind::RoomClosed || ( event._kind == TurnRelayEvent::Kind::Denied && event._reason == TurnRejectReason::RoomClosed ) )
            {
                clearTable();
                SW_LOG_INFO_ID( "Hub.CardsTableClosed", "[Hub] the card table was cleared - sit down again for a new game" );
            }
            else if ( event._kind == TurnRelayEvent::Kind::Denied )
                SW_LOG_WARNING_ID( "Hub.CardsNoSeat", "[Hub] no seat at the card tables (reason %#)", static_cast<int32>( event._reason ) );
            else if ( event._kind == TurnRelayEvent::Kind::ViewChanged && HubCardTurnPolicy::readSeatView( event._buffer, _view ) == false )
                SW_LOG_ERROR_ID( "Hub.CardsViewUnreadable", "[Hub] the card table view from the server is unreadable (%# bytes) - the hand keeps its last state", static_cast<uint32>( event._buffer.size() ) );
        }
    }

    void HubCardTableComponent::updateHUD()
    {
        IHUDViewModelBinder* const pBinder = game::findSubsystem<IHUDViewModelBinder>();
        if ( pBinder == nullptr )
            return;
        const bool bSeated = _role == Role::Client && _client.getSeat() >= 0 && _client.isStarted() && _client.getView().empty() == false;
        if ( _hudViewModel == nullptr )
            _hudViewModel = make_unique<HubCardHUDViewModel>();
        _bHUDShown = pBinder->bindHUDViewModel( *getOwner(), _hudViewModel.get() ) && bSeated ? SW_TRUE : SW_FALSE; // 같은 뷰모델이면 아무 일도 하지 않는다
        _hudViewModel->setTable( bSeated, _view, bSeated && getMyTurnValue() != 0 );
    }

    bool HubCardTableComponent::requestSeat()
    {
        if ( _role != Role::Client )
            return false;
        if ( _client.getSeat() >= 0 && ( _view._winner < 0 || requestLeave() == false ) )
            return false; // 두는 판이 있다
        _client.join( _roomID );
        return true;
    }

    bool HubCardTableComponent::requestLeave()
    {
        if ( _role != Role::Client || _client.getSeat() < 0 )
            return false;
        _client.leave();
        clearTable();
        return true;
    }

    void HubCardTableComponent::clearTable()
    {
        _view   = HubCardSeatView{};
        _roomID = kFirstRoomID;
    }

    bool HubCardTableComponent::requestPlay( bool bDrawOnly )
    {
        if ( _role != Role::Client || _client.isStarted() == false || _client.getSeat() < 0 || _client.getView().empty() )
            return false;
        CardAction chosen;
        chosen._kind = static_cast<uint8>( SheddingCardActionKind::Draw );
        if ( bDrawOnly == false && _view._listPlayableCardID.empty() == false )
        {
            chosen._kind     = static_cast<uint8>( SheddingCardActionKind::Play );
            chosen._cardID   = _view._listPlayableCardID.front();
            chosen._targetID = static_cast<uint16>( SheddingCardColor::Red );
            chosen._amount   = getHandCount() == 2 ? 1 : 0; // 한 장을 남기면 선언한다
        }
        vector<uint8> buffer;
        CardAction::encodeAction( chosen, buffer );
        return _client.submitAction( buffer ) >= 0;
    }

    int32 HubCardTableComponent::getMyTurnValue() const
    {
        const bool bPlaying = _role == Role::Client && _client.isStarted() && _client.getView().empty() == false;
        return bPlaying && _view._winner < 0 && _view._currentSeat == _client.getSeat() ? 1 : 0;
    }
} // namespace sw

namespace sw
{
    SW_DEV_COMMAND( HubCards, "hub.cards", "hub.cards join | leave | play | draw", "Sit at or leave a card table of the connected hub, play the first legal card (draw without one), or draw", &HubCardTableInternal::runCards );
    SW_AUTOMATION_GETTER_WATCH( hubCardsSeat, "Hub.CardsSeat", "Seat at the card table (-1 without one)", HubCardTableComponent, &HubCardTableComponent::getSeat );
    SW_AUTOMATION_GETTER_WATCH( hubCardsStarted, "Hub.CardsStarted", "1 once both seats of the card table are taken", HubCardTableComponent, &HubCardTableComponent::getStartedValue );
    SW_AUTOMATION_GETTER_WATCH( hubCardsMyTurn, "Hub.CardsMyTurn", "1 while it is this client's turn at the card table", HubCardTableComponent, &HubCardTableComponent::getMyTurnValue );
    SW_AUTOMATION_GETTER_WATCH( hubCardsHand, "Hub.CardsHand", "Cards in this client's hand as dealt by the server", HubCardTableComponent, &HubCardTableComponent::getHandCount );
    SW_AUTOMATION_GETTER_WATCH( hubCardsOpponentHand, "Hub.CardsOpponentHand", "Number of cards the other seat holds (the cards themselves never reach this client)", HubCardTableComponent, &HubCardTableComponent::getOpponentCardCount );
    SW_AUTOMATION_GETTER_WATCH( hubCardsHUDShown, "Hub.CardsHUDShown", "1 while the HUD shows the hand dealt by the server", HubCardTableComponent, &HubCardTableComponent::getHUDShownValue );
    SW_AUTOMATION_GETTER_WATCH( hubCardsActions, "Hub.CardsActions", "Actions the server applied at this client's card table", HubCardTableComponent, &HubCardTableComponent::getActionCount );
    SW_AUTOMATION_GETTER_WATCH( hubCardsRejected, "Hub.CardsRejected", "Actions of this client the card table refused", HubCardTableComponent, &HubCardTableComponent::getRejectedCount );
} // namespace sw
