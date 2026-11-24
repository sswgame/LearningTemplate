#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/GhostHunt/Component/GhostHuntGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"

namespace sw
{
    namespace
    {
        struct GhostHuntGameModeComponentInternal
        {
            static constexpr uint32  kStateTag         = FourCCUtil::make( "GHGM" );
            static constexpr uint32  kStateVersion     = 1;
            static constexpr float32 kFurnitureSpacing = 2.0f;  ///< 안쪽 벽을 따라 놓은 가구 사이(m)
            static constexpr float32 kFurnitureWallGap = 1.0f;  ///< 안쪽 벽에서 가구까지(m)
            static constexpr float32 kGhostDepthRatio  = 0.25f; ///< 유령이 처음 숨는 자리 — 방 가운데에서 방 깊이의 이 비율만큼 안쪽
            static constexpr float32 kGhostWallGap     = 0.5f;  ///< 유령은 방의 벽에서 이만큼 안쪽에만 있다(m)

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventAppeared   = "GhostHuntAppeared";
            static constexpr const utf8* kEventHurt       = "GhostHuntHurt";
            static constexpr const utf8* kEventStunned    = "GhostHuntStunned";
            static constexpr const utf8* kEventSuction    = "GhostHuntSuction";
            static constexpr const utf8* kEventSurge      = "GhostHuntSurge";
            static constexpr const utf8* kEventEscaped    = "GhostHuntEscaped";
            static constexpr const utf8* kEventCaught     = "GhostHuntCaught";
            static constexpr const utf8* kEventRoomLit    = "GhostHuntRoomLit";
            static constexpr const utf8* kEventKey        = "GhostHuntKey";
            static constexpr const utf8* kEventDoorOpened = "GhostHuntDoorOpened";
            static constexpr const utf8* kEventCoins      = "GhostHuntCoins";
            static constexpr const utf8* kEventWon        = "GhostHuntWon";
            static constexpr const utf8* kEventLost       = "GhostHuntLost";

            /** @brief 유령의 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다(공격은 맞았을 때만 낸다). */
            static const utf8* findSound( GhostEventKind kind )
            {
                switch ( kind )
                {
                    case GhostEventKind::Appeared:
                    {
                        return kEventAppeared;
                    }
                    case GhostEventKind::Stunned:
                    {
                        return kEventStunned;
                    }
                    case GhostEventKind::SuctionStarted:
                    {
                        return kEventSuction;
                    }
                    case GhostEventKind::Surge:
                    {
                        return kEventSurge;
                    }
                    case GhostEventKind::Escaped:
                    {
                        return kEventEscaped;
                    }
                    case GhostEventKind::Caught:
                    {
                        return kEventCaught;
                    }
                    default:
                    {
                        return nullptr;
                    }
                }
            }

            /** @brief 저택의 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다. */
            static const utf8* findSound( GhostMansionEventKind kind )
            {
                switch ( kind )
                {
                    case GhostMansionEventKind::RoomLit:
                    {
                        return kEventRoomLit;
                    }
                    case GhostMansionEventKind::KeyAwarded:
                    {
                        return kEventKey;
                    }
                    case GhostMansionEventKind::DoorOpened:
                    {
                        return kEventDoorOpened;
                    }
                    case GhostMansionEventKind::BooRevealed:
                    {
                        return kEventAppeared;
                    }
                    case GhostMansionEventKind::BooEscaped:
                    {
                        return kEventEscaped;
                    }
                    case GhostMansionEventKind::BooCaught:
                    {
                        return kEventCaught;
                    }
                    case GhostMansionEventKind::CoinsCollected:
                    {
                        return kEventCoins;
                    }
                }
                return nullptr;
            }

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kLoseResult = "lose";

            /** @brief 카탈로그에 유령 · 불이 정해진 방이면 true 입니다(복도는 그래프에만 있다). */
            static bool isHaunted( const GhostCatalog& catalog, const AreaDef& area ) { return catalog.findRoom( area._id ) != nullptr; }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( ghostHuntResult, "GhostHunt.Result", "Hunt result (0 ongoing, 1 win, 2 lose)", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntLitRooms, "GhostHunt.LitRooms", "Rooms whose light is on", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::countLitRooms );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntRemaining, "GhostHunt.Remaining", "Ghosts left in the current room", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getRemainingGhosts );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntCoins, "GhostHunt.Coins", "Coins collected", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getCoins );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntKeys, "GhostHunt.Keys", "Keys in the bag", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getKeyCount );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntHealth, "GhostHunt.Health", "Player health", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getHealth );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntPlayerX, "GhostHunt.PlayerX", "Absolute X of the player (m)", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getPlayerX );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntPlayerZ, "GhostHunt.PlayerZ", "Absolute Z of the player (m)", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::getPlayerZ );
    SW_AUTOMATION_GETTER_WATCH( ghostHuntSucking, "GhostHunt.Sucking", "1 while a ghost is in the vacuum", GhostHuntGameModeComponent, &GhostHuntGameModeComponent::isSucking );

    GhostHuntGameModeComponent::GhostHuntGameModeComponent()
        : _areaGraph{}
        , _loot{}
        , _startRoom{}
        , _seed{ 5 }
        , _bagSlotCount{ 4 }
        , _moveSpeed{ 4.0f }
        , _maxHealth{ 100.0f }
        , _useRadius{ 2.0f }
        , _ghostSpeed{ 0.6f }
        , _ghostReach{ 1.5f }
        , _ghostSpacing{ 1.5f }
        , _resultHoldSeconds{ 3.0f }
        , _pCatalog{}
        , _pGraphAsset{}
        , _pLoot{}
        , _pItemCatalog{}
        , _graph{}
        , _flags{}
        , _bag{}
        , _wallet{}
        , _mansion{}
        , _command{}
        , _listGhostEvent{}
        , _listMansionEvent{}
        , _resultHold{}
        , _playerPosition{ 0.0f, 0.0f }
        , _facing{ 0.0f, 1.0f }
        , _health{ 0.0f }
        , _bStrobeHeld{ SW_FALSE }
        , _result{ GhostHuntResult::Ongoing }
    {
    }

    GhostHuntGameModeComponent::~GhostHuntGameModeComponent() = default;

    bool GhostHuntGameModeComponent::openMansion()
    {
        _graph = *_pGraphAsset;
        _graph.resetState();
        _flags.clear();
        _wallet.clear();
        return _bag.initialize( *_pItemCatalog, _bagSlotCount ) && _mansion.initialize( *_pCatalog, *_pLoot, _graph, _flags, _bag, _wallet, _seed );
    }

    bool GhostHuntGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false || acquireBoundCatalog( _pItemCatalog ) == false || acquireRequired( _areaGraph, _pGraphAsset ) == false || acquireRequired( _loot, _pLoot ) == false )
            return false;
        const AreaDef* const pStart = _pGraphAsset->findArea( _startRoom );
        if ( pStart == nullptr )
        {
            SW_LOG_ERROR_ID( "GhostHunt.UnknownStartRoom", "Ghost hunt game mode: start room '%#' is not on the area graph - the mansion does not open", _startRoom.c_str() );
            return false;
        }
        if ( openMansion() == false )
            return false;
        if ( countRooms() == 0 )
        {
            SW_LOG_ERROR_ID( "GhostHunt.NoRoom", "Ghost hunt game mode: no area of the graph is a room of the catalog - the mansion does not open" );
            return false;
        }
        _command        = GhostHuntCommand{};
        _health         = _maxHealth;
        _playerPosition = pStart->getCenter();
        _resultHold.clear();
        enterRoom( _startRoom );
        return true;
    }

    void GhostHuntGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "GhostHunt.Started", "[GhostHunt] %# rooms to light, %# ghosts in '%#'", countRooms(), getRemainingGhosts(), _mansion.getCurrentRoom().c_str() );
    }

    void GhostHuntGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored )
            return;
        SW_LOG_WARNING_ID( "GhostHunt.StateRefused", "[GhostHunt] the saved hunt does not fit this build - a new hunt opens" );
        (void)openGame(); // 새 판도 열지 못하면 startGame 이 이유를 남겼다
    }

    bool GhostHuntGameModeComponent::submitPlayerInput( const GhostHuntCommand& command )
    {
        if ( isAcceptingInput() == false )
            return false;
        const bool bPending = _command._bUseTriggered == SW_TRUE;
        _command            = command;
        if ( bPending )
            _command._bUseTriggered = SW_TRUE;
        return true;
    }

    float2 GhostHuntGameModeComponent::computeFurniturePosition( const GhostFurnitureDef& furniture ) const
    {
        using Internal             = GhostHuntGameModeComponentInternal;
        const AreaDef* const pArea = _graph.findArea( furniture._room );
        if ( pArea == nullptr || _pCatalog == nullptr )
            return float2{ 0.0f, 0.0f };
        int32 order = 0;
        int32 count = 0;
        for ( const GhostFurnitureDef& other : _pCatalog->getFurniture() )
        {
            if ( other._room != furniture._room )
                continue;
            if ( other._id == furniture._id )
                order = count;
            ++count;
        }
        const float32 column = static_cast<float32>( order ) - static_cast<float32>( count - 1 ) * 0.5f;
        return float2{ pArea->getCenter()._x + column * Internal::kFurnitureSpacing, pArea->_y + pArea->_height - Internal::kFurnitureWallGap };
    }

    void GhostHuntGameModeComponent::enterRoom( const hashed_string& roomID )
    {
        (void)_mansion.enterRoom( roomID ); // 그래프에 있는 방이라 거절하지 않는다 — 나온 유령은 아래에서 본다
        const AreaDef* const pArea = _graph.findArea( roomID );
        if ( pArea == nullptr )
            return;
        const float2                 center     = pArea->getCenter();
        const vector<GhostInstance>& listGhost  = _mansion.getEncounter().getGhosts();
        const float32                ghostCount = static_cast<float32>( listGhost.size() );
        for ( size_t index = 0; index < listGhost.size(); ++index )
        {
            const float32 column = static_cast<float32>( index ) - ( ghostCount - 1.0f ) * 0.5f;
            _mansion.getEncounter().setGhostPosition( listGhost[index]._id, float3{ center._x + column * _ghostSpacing, 0.0f, center._y + pArea->_height * GhostHuntGameModeComponentInternal::kGhostDepthRatio } );
        }
    }

    void GhostHuntGameModeComponent::movePlayer( const float2& delta )
    {
        const AreaWalk walk = _graph.walk( _mansion.getCurrentRoom(), _playerPosition, delta, _flags );
        _playerPosition     = walk._position;
        if ( walk._pBlocked != nullptr )
        {
            const GhostDoorResult door = _mansion.unlockDoor( walk._pBlocked->_kind ); // 연결의 종류가 문 id 다
            if ( door == GhostDoorResult::Opened )
                SW_LOG_INFO_ID( "GhostHunt.DoorOpened", "[GhostHunt] door '%#' opened", walk._pBlocked->_kind.c_str() );
        }
        if ( walk._pEntered == nullptr )
            return;
        if ( isSucking() )
            _mansion.getEncounter().stopSuction();
        enterRoom( walk._pEntered->_id );
    }

    void GhostHuntGameModeComponent::applyVacuum( const GhostHuntCommand& command, float32 deltaTime )
    {
        GhostEncounter& encounter = _mansion.getEncounter();
        if ( command._bVacuum == SW_FALSE )
        {
            if ( isSucking() )
                encounter.stopSuction();
            return;
        }
        const float3 position = getPlayerPosition()._value;
        if ( isSucking() == false )
        {
            for ( const GhostInstance& ghost : encounter.getGhosts() )
            {
                if ( ghost._state == GhostState::Stunned && encounter.startSuction( ghost._id, position ) )
                    break;
            }
        }
        if ( isSucking() )
        {
            const GhostSuctionResult suction = encounter.updateSuction( float3{ command._move._x, 0.0f, command._move._y }, deltaTime );
            movePlayer( float2{ suction._drag._x, suction._drag._z } );
            return;
        }
        for ( const HidingGhostDef& boo : _pCatalog->getBoos() )
        {
            const HidingGhostRuntime* const pBoo       = _mansion.findBoo( boo._id );
            const GhostFurnitureDef* const  pFurniture = pBoo != nullptr ? _pCatalog->findFurniture( pBoo->_furniture ) : nullptr;
            const bool                      bHere      = pBoo != nullptr && pBoo->_state == HidingGhostState::Revealed && pBoo->_room == _mansion.getCurrentRoom();
            const bool                      bInReach   = bHere && ( pFurniture == nullptr || ( computeFurniturePosition( *pFurniture ) - _playerPosition ).getLength() <= _useRadius );
            if ( bInReach && _mansion.damageBoo( boo._id, encounter.computeVacuumPower() * deltaTime ) )
                SW_LOG_INFO_ID( "GhostHunt.BooCaught", "[GhostHunt] caught '%#'", boo._id.c_str() );
        }
    }

    void GhostHuntGameModeComponent::use()
    {
        if ( _mansion.getEncounter().triggerSurge() )
            return;
        for ( const GhostFurnitureDef& furniture : _pCatalog->getFurniture() )
        {
            const bool bInReach = furniture._room == _mansion.getCurrentRoom() && ( computeFurniturePosition( furniture ) - _playerPosition ).getLength() <= _useRadius;
            if ( bInReach == false )
                continue;
            ItemStackList           loot;
            const GhostSearchResult result = _mansion.searchFurniture( furniture._id, furniture._bShake ? GhostSearchMode::Shake : GhostSearchMode::Vacuum, loot );
            if ( loot.getItems().empty() == false )
                getSoundQueue().queueEvent( GhostHuntGameModeComponentInternal::kEventCoins );
            for ( const ItemStack& stack : loot.getItems() )
            {
                if ( stack._itemID == _pCatalog->getCurrency() )
                    _wallet.add( stack._itemID, stack._count );
                else
                    (void)_bag.addItem( stack._itemID, stack._count ); // 가방이 차면 남은 것은 잃는다(전리품은 열쇠가 아니다)
            }
            if ( result == GhostSearchResult::Found || result == GhostSearchResult::BooFound )
                SW_LOG_INFO_ID( "GhostHunt.Searched", "[GhostHunt] searched '%#' - %#", furniture._id.c_str(), result == GhostSearchResult::BooFound ? "a boo jumps out" : "done" );
            return;
        }
    }

    void GhostHuntGameModeComponent::applyTools( const GhostHuntCommand& command, float32 deltaTime )
    {
        GhostEncounter& encounter = _mansion.getEncounter();
        const float3    eye       = getPlayerPosition()._value;
        const float3    forward   = getFacing();
        if ( command._bStrobe == SW_TRUE )
            encounter.chargeStrobe( deltaTime );
        else if ( _bStrobeHeld == SW_TRUE )
            (void)encounter.releaseStrobe( eye, forward ); // 기절시킨 수는 이벤트로도 온다
        _bStrobeHeld = command._bStrobe;
        if ( command._bLight == SW_TRUE )
            (void)encounter.shineBeam( eye, forward );
        applyVacuum( command, deltaTime );
        if ( command._bUseTriggered == SW_TRUE )
            use();
    }

    void GhostHuntGameModeComponent::moveGhosts( float32 deltaTime )
    {
        const AreaDef* const pArea = _graph.findArea( _mansion.getCurrentRoom() );
        if ( pArea == nullptr )
            return;
        for ( const GhostInstance& ghost : _mansion.getEncounter().getGhosts() )
        {
            const bool   bAppeared = ghost._state == GhostState::Visible || ghost._state == GhostState::Attacking;
            const float2 position{ ghost._position._x, ghost._position._z };
            const float2 toPlayer = _playerPosition - position;
            float2       moved    = position;
            if ( bAppeared && toPlayer.getLength() > _ghostReach * 0.5f )
                moved = position + toPlayer * ( _ghostSpeed * deltaTime / toPlayer.getLength() );
            else if ( ghost._state == GhostState::Sucking )
                moved = position + float2{ ghost._fleeDirection._x, ghost._fleeDirection._z } * ( _ghostSpeed * deltaTime );
            else
                continue;
            moved = pArea->clamp( moved, GhostHuntGameModeComponentInternal::kGhostWallGap );
            _mansion.getEncounter().setGhostPosition( ghost._id, float3{ moved._x, 0.0f, moved._y } );
        }
    }

    void GhostHuntGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( _result != GhostHuntResult::Ongoing )
        {
            using Internal = GhostHuntGameModeComponentInternal;
            if ( _resultHold.update( deltaTime ) )
                (void)finishMinigame( hashed_string( _result == GhostHuntResult::Win ? Internal::kWinResult : Internal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
            return;
        }
        const GhostHuntCommand command = _command;
        _command._bUseTriggered        = SW_FALSE; // 쓴 누름이다
        const float32 moveLength       = command._move.getLength();
        if ( moveLength > MathUtil::kEpsilon && isSucking() == false )
        {
            // 줄다리기 동안 스틱은 당기는 방향이다 — 걷지도 돌아서지도 않는다.
            _facing = command._move * ( 1.0f / moveLength );
            movePlayer( command._move * ( _moveSpeed * deltaTime / MathUtil::max( 1.0f, moveLength ) ) );
        }
        applyTools( command, deltaTime );
        moveGhosts( deltaTime );
        _mansion.update( deltaTime );
        settle();
    }

    void GhostHuntGameModeComponent::settle()
    {
        _listGhostEvent.clear();
        _mansion.pollGhostEvents( _listGhostEvent );
        for ( const GhostEvent& event : _listGhostEvent )
        {
            const GhostInstance* const pGhost = _mansion.getEncounter().findGhost( event._ghostID );
            const bool                 bHit   = event._type == GhostEventKind::AttackLanded && pGhost != nullptr &&
                              float3::getHorizontalDistance( pGhost->_position, getPlayerPosition()._value ) <= _ghostReach;
            if ( bHit )
                (void)DepletingValue::take( _health, event._amount ); // 다했는지는 아래에서 본다
            const utf8* const pSound = bHit ? GhostHuntGameModeComponentInternal::kEventHurt : GhostHuntGameModeComponentInternal::findSound( event._type );
            if ( pSound != nullptr )
                getSoundQueue().queueEvent( pSound );
        }
        _listMansionEvent.clear();
        _mansion.pollEvents( _listMansionEvent );
        for ( const GhostMansionEvent& event : _listMansionEvent )
        {
            if ( const utf8* const pSound = GhostHuntGameModeComponentInternal::findSound( event._type ) )
                getSoundQueue().queueEvent( pSound );
            if ( event._type == GhostMansionEventKind::RoomLit )
                SW_LOG_INFO_ID( "GhostHunt.RoomLit", "[GhostHunt] '%#' is lit (%# of %#)", event._id.c_str(), countLitRooms(), countRooms() );
            else if ( event._type == GhostMansionEventKind::KeyAwarded )
                SW_LOG_INFO_ID( "GhostHunt.KeyAwarded", "[GhostHunt] got key '%#'", event._id.c_str() );
        }
        if ( _health <= 0.0f )
            _result = GhostHuntResult::Lose;
        else if ( countLitRooms() == countRooms() )
            _result = GhostHuntResult::Win;
        if ( _result == GhostHuntResult::Ongoing )
            return;
        _resultHold.start( _resultHoldSeconds );
        getSoundQueue().queueEvent( _result == GhostHuntResult::Win ? GhostHuntGameModeComponentInternal::kEventWon : GhostHuntGameModeComponentInternal::kEventLost );
        SW_LOG_INFO_ID( "GhostHunt.Ended", "[GhostHunt] %# - %# coins", _result == GhostHuntResult::Win ? "every room is lit" : "the hunter fell", getCoins() );
    }

    void GhostHuntGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        addMoneyResult( outListValue, getCoins() );
    }

    bool GhostHuntGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject   = GameObjectHandle{};
        outFocusPosition = getPlayerPosition()._value;
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( getPlayerPosition() );
        return isStarted();
    }

    int32 GhostHuntGameModeComponent::countLitRooms() const
    {
        int32 count = 0;
        for ( const AreaDef& area : _graph.getAreas() )
        {
            if ( _pCatalog != nullptr && GhostHuntGameModeComponentInternal::isHaunted( *_pCatalog, area ) )
                count += _mansion.isRoomLit( area._id ) ? 1 : 0;
        }
        return count;
    }

    int32 GhostHuntGameModeComponent::countRooms() const
    {
        int32 count = 0;
        for ( const AreaDef& area : _graph.getAreas() )
        {
            if ( _pCatalog != nullptr && GhostHuntGameModeComponentInternal::isHaunted( *_pCatalog, area ) )
                ++count;
        }
        return count;
    }

    int32 GhostHuntGameModeComponent::getCoins() const
    {
        return _pCatalog != nullptr ? static_cast<int32>( _wallet.getBalance( _pCatalog->getCurrency() ) ) : 0;
    }

    void GhostHuntGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`GhostHuntAvatarComponent`)이 읽어 그린다.
        (void)manager;
        (void)bRespawnViews;
    }

    void GhostHuntGameModeComponent::serializeGameState( StateIO& io )
    {
        using Internal = GhostHuntGameModeComponentInternal;
        io.header( Internal::kStateTag, Internal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 저택은 그래프 · 깃발 · 가방 · 지갑을 빌린다 — 지역 사본에 읽으면 빌린 주소가 사본을 가리킨다. 새로 연 저택에 바로 읽고, 거절되면 `onStateRestored` 가 새 판을 연다.
        const bool bBound = _pCatalog != nullptr && _pGraphAsset != nullptr && _pLoot != nullptr && _pItemCatalog != nullptr;
        if ( io.isReading() && ( bBound == false || openMansion() == false ) )
        {
            SW_LOG_ERROR_ID( "GhostHunt.StateNeedsCatalog", "Ghost hunt game mode: state read before the catalog and the area graph are bound - refused" );
            io.fail();
            return;
        }
        io.value( _flags );
        io.value( _bag );
        io.value( _wallet );
        vector<hashed_string> listVisited;
        vector<hashed_string> listDiscovered;
        _graph.fillVisitedAreas( listVisited );
        _graph.fillDiscoveredAreas( listDiscovered );
        io.list( listVisited );
        io.list( listDiscovered );
        io.value( _mansion ); // 자리 · 체력 · 결과는 상태 칸이다(`GameModeComponent::serializeState`)
        io.value( _resultHold );
        io.check( io.isWriting() || _graph.findArea( _mansion.getCurrentRoom() ) != nullptr );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _graph.restoreState( listVisited, listDiscovered );
    }
} // namespace sw
