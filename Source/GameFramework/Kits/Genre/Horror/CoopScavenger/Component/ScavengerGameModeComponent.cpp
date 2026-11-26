#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Component/ScavengerGameModeComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/FormatDef.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    namespace
    {
        struct ScavengerGameModeComponentInternal
        {
            static constexpr FormatDef kStateFormat{ "SCGM" };
            static constexpr int32     kPlayer = 0; ///< 늘 사람이 잡는 승무원(카메라 · 감시 값의 주인)
            /** @brief 축 방향과 이웃 방향이 이만큼은 맞아야 그 이웃으로 본다(내적). */
            static constexpr float32 kNeighborAlignment = 0.5f;
            static constexpr float32 kPercent           = 100.0f;

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventLanded   = "ScavengerLanded";
            static constexpr const utf8* kEventDeparted = "ScavengerDeparted";
            static constexpr const utf8* kEventDusk     = "ScavengerDusk";
            static constexpr const utf8* kEventThreat   = "ScavengerThreat";
            static constexpr const utf8* kEventDied     = "ScavengerDied";
            static constexpr const utf8* kEventPickup   = "ScavengerPickup";
            static constexpr const utf8* kEventLoaded   = "ScavengerLoaded";
            static constexpr const utf8* kEventUnlocked = "ScavengerUnlocked";
            static constexpr const utf8* kEventSold     = "ScavengerSold";
            static constexpr const utf8* kEventBought   = "ScavengerBought";
            static constexpr const utf8* kEventRefused  = "ScavengerRefused";
            static constexpr const utf8* kEventWon      = "ScavengerWon";
            static constexpr const utf8* kEventLost     = "ScavengerLost";
            static constexpr const utf8* kEventRouted   = "ScavengerRouted";
            static constexpr const utf8* kEventBody     = "ScavengerBody";
            static constexpr const utf8* kEventWiped    = "ScavengerWiped";
            static constexpr const utf8* kEventFine     = "ScavengerFine";

            /** @brief 원정의 알림 @p kind 에 낼 소리입니다. 소리가 없는 알림이면 nullptr 입니다(할당량은 결과가 정한다). */
            static const utf8* findSound( ScavengerEvent::Kind kind )
            {
                switch ( kind )
                {
                    case ScavengerEvent::Kind::Landed:
                    {
                        return kEventLanded;
                    }
                    case ScavengerEvent::Kind::ShipDeparted:
                    {
                        return kEventDeparted;
                    }
                    case ScavengerEvent::Kind::Dusk:
                    {
                        return kEventDusk;
                    }
                    case ScavengerEvent::Kind::ThreatSpawned:
                    {
                        return kEventThreat;
                    }
                    case ScavengerEvent::Kind::PlayerDied:
                    {
                        return kEventDied;
                    }
                    case ScavengerEvent::Kind::ScrapSold:
                    {
                        return kEventSold;
                    }
                    case ScavengerEvent::Kind::Routed:
                    {
                        return kEventRouted;
                    }
                    case ScavengerEvent::Kind::BodyRecovered:
                    {
                        return kEventBody;
                    }
                    case ScavengerEvent::Kind::CrewWiped:
                    {
                        return kEventWiped;
                    }
                    case ScavengerEvent::Kind::FinePaid:
                    {
                        return kEventFine;
                    }
                    case ScavengerEvent::Kind::QuotaMet: // 결과가 정한다(`settle`)
                    case ScavengerEvent::Kind::GameOver:
                    {
                        break;
                    }
                }
                return nullptr;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( scavengerResult, "Scavenger.Result", "Expedition result (0 ongoing, 1 win, 2 lose)", ScavengerGameModeComponent, &ScavengerGameModeComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( scavengerPhase, "Scavenger.Phase", "Expedition phase (0 in orbit, 1 landed, 2 game over)", ScavengerGameModeComponent, &ScavengerGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( scavengerQuota, "Scavenger.Quota", "Quota of this cycle", ScavengerGameModeComponent, &ScavengerGameModeComponent::getQuota );
    SW_AUTOMATION_GETTER_WATCH( scavengerFulfilled, "Scavenger.Fulfilled", "Credits sold towards the quota", ScavengerGameModeComponent, &ScavengerGameModeComponent::getFulfilled );
    SW_AUTOMATION_GETTER_WATCH( scavengerDaysLeft, "Scavenger.DaysLeft", "Days before the deadline day", ScavengerGameModeComponent, &ScavengerGameModeComponent::getDaysLeft );
    SW_AUTOMATION_GETTER_WATCH( scavengerShipValue, "Scavenger.ShipValue", "Value of the scrap on the ship", ScavengerGameModeComponent, &ScavengerGameModeComponent::getShipValue );
    SW_AUTOMATION_GETTER_WATCH( scavengerCredits, "Scavenger.Credits", "Credits of the crew", ScavengerGameModeComponent, &ScavengerGameModeComponent::getCredits );
    SW_AUTOMATION_GETTER_WATCH( scavengerGround, "Scavenger.GroundScrap", "Scrap left on the moon", ScavengerGameModeComponent, &ScavengerGameModeComponent::getGroundScrapCount );
    SW_AUTOMATION_GETTER_WATCH( scavengerCarried, "Scavenger.Carried", "Scrap in the hands of the crew", ScavengerGameModeComponent, &ScavengerGameModeComponent::countCarried );
    SW_AUTOMATION_GETTER_WATCH( scavengerThreats, "Scavenger.Threats", "Threats on the moon", ScavengerGameModeComponent, &ScavengerGameModeComponent::getThreatCount );
    SW_AUTOMATION_GETTER_WATCH( scavengerPlayerInShip, "Scavenger.PlayerInShip", "1 while crew member 0 is in the ship", ScavengerGameModeComponent, &ScavengerGameModeComponent::isPlayerInShip );

    SW_AUTOMATION_GETTER_WATCH( scavengerCanUnlock, "Scavenger.CanUnlock", "1 while the ship holds a lockpick", ScavengerGameModeComponent, &ScavengerGameModeComponent::canUnlockDoor );

    ScavengerGameModeComponent::ScavengerGameModeComponent()
        : _companyMoon{}
        , _threatTable{}
        , _weatherCatalog{}
        , _shopCatalog{}
        , _unlockItem{}
        , _speedItem{}
        , _speedItemStepScale{ 1.0f }
        , _seed{ 11 }
        , _crewCount{ 2 }
        , _cyclesToWin{ 1 }
        , _storageSlotCount{ 8 }
        , _stepSeconds{ 0.6f }
        , _areaSpacing{ 6.0f }
        , _threatStepSeconds{ 2.0f }
        , _threatLifeSeconds{ 60.0f }
        , _threatDamage{ 25.0f }
        , _pCatalog{}
        , _pThreatTable{}
        , _pShopCatalog{}
        , _pItemCatalog{}
        , _pWeatherCatalog{}
        , _wallet{}
        , _flags{}
        , _storage{}
        , _expedition{}
        , _listCommand{}
        , _listStep{}
        , _listEvent{}
        , _listThreat{}
        , _humanMask{ 0 }
        , _result{ ScavengerResult::Ongoing }
    {
    }

    ScavengerGameModeComponent::~ScavengerGameModeComponent() = default;

    bool ScavengerGameModeComponent::openExpedition()
    {
        _wallet.clear();
        _flags.clear();
        GameStateComponent* const pState = GameStateComponent::findOnOwner( *this );
        if ( pState == nullptr || _storage.initialize( *_pItemCatalog, _storageSlotCount ) == false )
            return false;
        const ScavengerExpeditionData data{ *_pCatalog, *_pWeatherCatalog, *_pThreatTable, *_pShopCatalog, *_pItemCatalog };
        return _expedition.initialize( data, _wallet, pState->getClock(), pState->getWeather(), _flags, _storage, _seed, _crewCount );
    }

    bool ScavengerGameModeComponent::validateKeys() const
    {
        const ScavengerMoonDef* const pCompany = _pCatalog->findMoon( _companyMoon.key() );
        if ( pCompany == nullptr || pCompany->_bCompany == false )
        {
            SW_LOG_ERROR_ID( "Scavenger.NoCompanyMoon", "Scavenger game mode: moon '%#' is not a company moon of the catalog - the expedition does not open", _companyMoon.c_str() );
            return false;
        }
        if ( findTerminal() == nullptr )
        {
            SW_LOG_ERROR_ID( "Scavenger.NoTerminal", "Scavenger game mode: '%#' has no shop '%#' - the expedition does not open", _shopCatalog.c_str(), _pCatalog->getTerminalShopID().c_str() );
            return false;
        }
        const expected<const ItemDef*> unlockItem = _pItemCatalog->require( _unlockItem );
        const expected<const ItemDef*> speedItem  = _pItemCatalog->require( _speedItem );
        if ( unlockItem.has_value() && speedItem.has_value() )
            return true;
        SW_LOG_ERROR_ID( "Scavenger.UnknownItem", "Scavenger game mode: the item catalog lacks the unlock item '%#' or the speed item '%#' - the expedition does not open", _unlockItem.c_str(), _speedItem.c_str() );
        return false;
    }

    bool ScavengerGameModeComponent::startGame()
    {
        const bool bCatalogs = acquireBoundCatalog( _pCatalog ) && acquireBoundCatalog( _pItemCatalog ) && acquireRequired( _threatTable, _pThreatTable ) && acquireRequired( _shopCatalog, _pShopCatalog ) &&
                               acquireRequiredSnapshot( _weatherCatalog, _pWeatherCatalog );
        if ( bCatalogs == false || validateKeys() == false )
            return false;
        const ScavengerDayDef& day = _pCatalog->getDaySettings();
        GameStateSettings      settings;
        settings._clock._secondsPerDay = day._secondsPerDay;
        settings._clock._startHour     = day._arrivalHour;
        settings._clock._duskHour      = day._duskHour;
        settings._pWeatherCatalog      = _pWeatherCatalog;
        settings._weatherSeed          = _seed;
        if ( GameStateComponent::openOnOwner( *this, settings ) == nullptr )
            return false; // 까닭은 월드 상태가 남겼다
        if ( openExpedition() == false )
            return false;
        updateClock();
        _listCommand.assign( static_cast<size_t>( _crewCount ), ScavengerCommand{} );
        _listStep.assign( static_cast<size_t>( _crewCount ), Countdown{} );
        return true;
    }

    void ScavengerGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "Scavenger.Started", "[Scavenger] crew of %#, quota %# in %# days - pull the lever to land", _expedition.getCrewCount(), getQuota(), getDaysLeft() );
    }

    void ScavengerGameModeComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored == false )
            SW_LOG_WARNING_ID( "Scavenger.StateRefused", "[Scavenger] the saved expedition does not fit this build - the current expedition goes on" );
    }

    bool ScavengerGameModeComponent::submitCrewCommand( int32 player, const ScavengerCommand& command )
    {
        const bool bKnown = 0 <= player && player < static_cast<int32>( _listCommand.size() );
        if ( isAcceptingInput() == false || bKnown == false )
            return false;
        if ( player != ScavengerGameModeComponentInternal::kPlayer && command.isIdle() == false )
            _humanMask |= 1u << static_cast<uint32>( player );
        ScavengerCommand& pending = _listCommand[static_cast<size_t>( player )];
        pending._moveTo           = command._moveTo;
        if ( command._buyItemID.empty() == false )
            pending._buyItemID = command._buyItemID;
        if ( command._bUseTriggered == SW_TRUE )
            pending._bUseTriggered = SW_TRUE;
        if ( command._bLeverTriggered == SW_TRUE )
            pending._bLeverTriggered = SW_TRUE;
        return true;
    }

    bool ScavengerGameModeComponent::isHuman( int32 player ) const
    {
        if ( isAutoPlayOn() )
            return false;
        return player == ScavengerGameModeComponentInternal::kPlayer || ( _humanMask & ( 1u << static_cast<uint32>( player ) ) ) != 0;
    }

    hashed_string ScavengerGameModeComponent::findNeighborToward( int32 player, const float2& direction ) const
    {
        const ScavengerCrewMember* const pMember = _expedition.findCrewMember( player );
        const float32                    length  = direction.getLength();
        if ( pMember == nullptr || length <= MathUtil::kEpsilon )
            return hashed_string{};
        const float3  from = computeAreaPosition( pMember->_areaID )._value;
        hashed_string best{};
        float32       bestAlignment = ScavengerGameModeComponentInternal::kNeighborAlignment;
        for ( const AreaLink& link : _expedition.getFacility().getGraph().getLinks() )
        {
            if ( link._from != pMember->_areaID && link._to != pMember->_areaID )
                continue;
            const hashed_string& other     = link._from == pMember->_areaID ? link._to : link._from;
            const float3         delta     = computeAreaPosition( other )._value - from;
            const float32        distance  = delta.getLength();
            const float32        alignment = distance > MathUtil::kEpsilon ? ( delta._x * direction._x + delta._z * direction._y ) / ( distance * length ) : 0.0f;
            if ( alignment <= bestAlignment )
                continue;
            bestAlignment = alignment;
            best          = other;
        }
        return best;
    }

    AbsolutePosition ScavengerGameModeComponent::computeAreaPosition( const hashed_string& areaID ) const
    {
        const AreaDef* const pArea = _expedition.getFacility().getGraph().findArea( areaID );
        return pArea != nullptr ? AbsolutePosition{
                                      float3{ pArea->_x * _areaSpacing, 0.0f, pArea->_y * _areaSpacing }
        }
                                : AbsolutePosition{};
    }

    hashed_string ScavengerGameModeComponent::findStepToward( int32 player, const hashed_string& targetID ) const
    {
        const ScavengerCrewMember* const pMember  = _expedition.findCrewMember( player );
        const ScavengerFacility&         facility = _expedition.getFacility();
        vector<hashed_string>            listArea;
        const bool                       bPath = pMember != nullptr && facility.getGraph().findPath( pMember->_areaID, targetID, facility.getFlags(), listArea );
        return bPath && listArea.size() > 1 ? listArea[1] : hashed_string{}; // 길의 첫 방은 지금 있는 곳이다
    }

    ScavengerCommand ScavengerGameModeComponent::decideBot( int32 player ) const
    {
        ScavengerCommand                 command;
        const ScavengerCrewMember* const pMember = _expedition.findCrewMember( player );
        if ( pMember == nullptr || pMember->isDead() )
            return command;
        const ScavengerFacility& facility = _expedition.getFacility();
        const hashed_string      ship( ScavengerExpedition::kShipAreaID );
        const bool               bInShip = pMember->_areaID == ship;
        // 레버는 승무원 0 만 당긴다(자동 플레이일 때만 봇이 승무원 0 이다).
        const bool bMayPullLever = player == ScavengerGameModeComponentInternal::kPlayer;
        if ( _expedition.getPhase() != ScavengerPhase::Landed )
        {
            // 자물쇠 따개가 없고 살 돈이 있으면 먼저 산다.
            const hashed_string unlockItemID = _unlockItem.key();
            const bool          bBuy         = bMayPullLever && canUnlockDoor() == false && _expedition.getShop().evaluateBuy( _pCatalog->getTerminalShopID(), unlockItemID, 1, _wallet, _storage ) == ShopResult::Ok;
            command._buyItemID               = bBuy ? unlockItemID : hashed_string{};
            command._bLeverTriggered         = bMayPullLever && bBuy == false ? SW_TRUE : SW_FALSE;
            return command;
        }
        const bool bCompany = _expedition.getMoon()->_bCompany;
        if ( bCompany )
        {
            command._moveTo          = bInShip ? hashed_string{} : findStepToward( player, ship );
            command._bUseTriggered   = bInShip && _expedition.getShipScrap().empty() == false && bMayPullLever ? SW_TRUE : SW_FALSE;
            command._bLeverTriggered = bInShip && _expedition.getShipScrap().empty() && bMayPullLever ? SW_TRUE : SW_FALSE;
            return command;
        }
        if ( pMember->_carry.evaluatePickup() != ScavengerPickupResult::Ok )
        {
            command._moveTo        = bInShip ? hashed_string{} : findStepToward( player, ship );
            command._bUseTriggered = bInShip ? SW_TRUE : SW_FALSE;
            return command;
        }
        // 지금 닿는 고철 가운데 가장 가까운(지나는 칸이 적은) 것으로 간다.
        vector<hashed_string> listArea;
        hashed_string         nearest{};
        size_t                nearestStep = 0;
        for ( const ScavengerScrap& scrap : facility.getGroundScrap() )
        {
            if ( facility.getGraph().findPath( pMember->_areaID, scrap._areaID, facility.getFlags(), listArea ) == false )
                continue;
            if ( nearest.empty() == false && listArea.size() >= nearestStep )
                continue;
            nearest     = scrap._areaID;
            nearestStep = listArea.size();
        }
        if ( nearest.empty() && canUnlockDoor() )
        {
            // 닿는 고철이 없다 — 남은 것이 잠긴 문 뒤면 그 문 앞으로 간다.
            for ( const AreaLink& link : facility.getGraph().getLinks() )
            {
                const bool bLocked = facility.getGroundScrap().empty() == false && facility.canTraverse( link._from, link._to ) == false;
                if ( bLocked && facility.getGraph().findPath( pMember->_areaID, link._from, facility.getFlags(), listArea ) )
                    nearest = link._from;
            }
        }
        if ( nearest.empty() == false )
        {
            command._bUseTriggered = pMember->_areaID == nearest ? SW_TRUE : SW_FALSE;
            command._moveTo        = findStepToward( player, nearest );
            return command;
        }
        command._moveTo          = bInShip ? hashed_string{} : findStepToward( player, ship );
        command._bUseTriggered   = bInShip && pMember->_carry.getCount() > 0 ? SW_TRUE : SW_FALSE;
        command._bLeverTriggered = bInShip && pMember->_carry.getCount() == 0 && countCarried() == 0 && bMayPullLever ? SW_TRUE : SW_FALSE;
        return command;
    }

    void ScavengerGameModeComponent::use( int32 player )
    {
        const ScavengerCrewMember& member   = *_expedition.findCrewMember( player );
        const ScavengerFacility&   facility = _expedition.getFacility();
        if ( member._areaID == hashed_string( ScavengerExpedition::kShipAreaID ) )
        {
            if ( _expedition.depositToShip( player ) > 0 )
            {
                getSoundQueue().queueEvent( ScavengerGameModeComponentInternal::kEventLoaded );
                return;
            }
            ScavengerActionResult result = ScavengerActionResult::Ok;
            (void)_expedition.sellAllShipScrap( result ); // 회사가 아니면 거절한다
            return;
        }
        for ( const ScavengerScrap& scrap : facility.getGroundScrap() )
        {
            if ( scrap._areaID != member._areaID )
                continue;
            const int32 uid = scrap._uid;
            if ( _expedition.pickUp( player, uid ) == ScavengerPickupResult::Ok ) // 손이 찼으면 거절한다
                getSoundQueue().queueEvent( ScavengerGameModeComponentInternal::kEventPickup );
            return;
        }
        for ( const AreaLink& link : facility.getGraph().getLinks() )
        {
            if ( canUnlockDoor() == false )
                return; // 자물쇠 따개가 없다
            const bool bBeside = link._from == member._areaID || link._to == member._areaID;
            if ( bBeside && facility.canTraverse( link._from, link._to ) == false )
            {
                const hashed_string roomID = link._to; // 잠금은 들어가는 방의 것이다
                if ( _expedition.unlockDoor( roomID ) )
                    getSoundQueue().queueEvent( ScavengerGameModeComponentInternal::kEventUnlocked );
                return;
            }
        }
    }

    void ScavengerGameModeComponent::pullLever( int32 player )
    {
        if ( _expedition.getPhase() == ScavengerPhase::InOrbit )
        {
            if ( getDaysLeft() == 0 )
                (void)_expedition.routeTo( _companyMoon.key() ); // 마감 날은 회사로 간다 — 비용이 모자라면 지금 위성에 내린다
            (void)_expedition.land();
            return;
        }
        const ScavengerCrewMember& member = *_expedition.findCrewMember( player );
        if ( member._areaID == hashed_string( ScavengerExpedition::kShipAreaID ) )
            (void)_expedition.takeOff();
    }

    void ScavengerGameModeComponent::applyCommand( int32 player, const ScavengerCommand& command, GameSeconds frameTime )
    {
        const ScavengerCrewMember* const pMember = _expedition.findCrewMember( player );
        Countdown&                       step    = _listStep[static_cast<size_t>( player )];
        (void)step.update( frameTime ); // 끝난 걸음인지는 아래 isActive 로 본다
        if ( pMember == nullptr || pMember->isDead() )
            return;
        const bool bStep = command._moveTo.empty() == false && step.isActive() == false;
        if ( bStep && _expedition.movePlayer( player, command._moveTo ) == ScavengerActionResult::Ok )
            step.start( _stepSeconds / pMember->_carry.computeSpeedScale() * ( _storage.hasItem( _speedItem.key() ) ? _speedItemStepScale : 1.0f ) );
        if ( command._buyItemID.empty() == false && _expedition.getPhase() == ScavengerPhase::InOrbit )
        {
            const ShopResult bought = _expedition.buyFromTerminal( command._buyItemID, 1 );
            getSoundQueue().queueEvent( bought == ShopResult::Ok ? ScavengerGameModeComponentInternal::kEventBought : ScavengerGameModeComponentInternal::kEventRefused );
            SW_LOG_INFO_ID( "Scavenger.Terminal", "[Scavenger] the terminal %# '%#' (shop result %#) - credits %#", bought == ShopResult::Ok ? "sold" : "refused", command._buyItemID.c_str(),
                            static_cast<int32>( bought ), getCredits() );
        }
        if ( command._bUseTriggered == SW_TRUE && _expedition.getPhase() == ScavengerPhase::Landed )
            use( player );
        if ( command._bLeverTriggered == SW_TRUE )
            pullLever( player );
    }

    void ScavengerGameModeComponent::updateThreats( float32 deltaTime )
    {
        if ( _expedition.getPhase() != ScavengerPhase::Landed )
        {
            _listThreat.clear(); // 떠났다 — 원정의 감독도 비었다
            return;
        }
        const ScavengerFacility& facility = _expedition.getFacility();
        const hashed_string      ship( ScavengerExpedition::kShipAreaID );
        const hashed_string      outside( ScavengerExpedition::kOutsideAreaID );
        for ( ScavengerThreat& threat : _listThreat )
        {
            threat._lifeRemaining -= deltaTime;
            threat._stepRemaining -= deltaTime;
            if ( threat._stepRemaining > 0.0f )
                continue;
            threat._stepRemaining += _threatStepSeconds;
            // 가장 가까운 산 승무원 쪽으로 한 칸 간다. 우주선에는 들지 않고, 실내 위협은 바깥으로, 실외 위협은 실내로 나가지 않는다.
            vector<hashed_string> listArea;
            hashed_string         next{};
            size_t                nearestStep = 0;
            for ( int32 player = 0; player < _expedition.getCrewCount(); ++player )
            {
                const ScavengerCrewMember& member = *_expedition.findCrewMember( player );
                if ( member.isDead() || member._areaID == ship || facility.getGraph().findPath( threat._areaID, member._areaID, facility.getFlags(), listArea ) == false )
                    continue;
                if ( nearestStep != 0 && listArea.size() >= nearestStep )
                    continue;
                nearestStep = listArea.size();
                next        = listArea.size() > 1 ? listArea[1] : threat._areaID;
            }
            const bool bStays = next.empty() || ( threat._bIndoor == SW_TRUE ) == ( next == outside );
            if ( bStays == false || next == threat._areaID )
                threat._areaID = next;
        }
        // 피해는 움직임이 끝난 뒤에 준다 — 모두 죽으면 원정이 떠나 위협 목록의 뜻이 바뀐다.
        for ( const ScavengerThreat& threat : _listThreat )
        {
            const bool bStepped = threat._stepRemaining > _threatStepSeconds - deltaTime;
            for ( int32 player = 0; bStepped && player < _expedition.getCrewCount(); ++player )
            {
                if ( _expedition.findCrewMember( player )->_areaID == threat._areaID )
                    (void)_expedition.applyDamage( player, _threatDamage ); // 죽은 사람 · 떠난 뒤에는 0 이다
            }
        }
        for ( const ScavengerThreat& threat : _listThreat )
        {
            if ( threat._lifeRemaining <= 0.0f )
                _expedition.notifyThreatDespawned( threat._bIndoor == SW_TRUE, threat._spawnID );
        }
        VectorUtil::removeIf( _listThreat, []( const ScavengerThreat& threat )
        { return threat._lifeRemaining <= 0.0f; } );
    }

    void ScavengerGameModeComponent::updateMatch( GameSeconds frameTime )
    {
        const float32 deltaTime = frameTime.count();
        for ( int32 player = 0; player < _expedition.getCrewCount(); ++player )
        {
            const ScavengerCommand command              = isHuman( player ) ? _listCommand[static_cast<size_t>( player )] : decideBot( player );
            _listCommand[static_cast<size_t>( player )] = ScavengerCommand{}; // 쓴 명령이다(클라이언트가 다음 프레임에 다시 넣는다)
            applyCommand( player, command, frameTime );
        }
        _expedition.update( frameTime );
        updateThreats( deltaTime );
        settle();
        updateClock();
    }

    void ScavengerGameModeComponent::updateClock()
    {
        if ( GameStateComponent* const pState = GameStateComponent::findOnOwner( *this ) )
            pState->getClock().setPaused( _expedition.getPhase() != ScavengerPhase::Landed || _result != ScavengerResult::Ongoing );
    }

    void ScavengerGameModeComponent::settle()
    {
        using Kind = ScavengerEvent::Kind;
        _listEvent.clear();
        _expedition.pollEvents( _listEvent );
        for ( const ScavengerEvent& event : _listEvent )
        {
            if ( const utf8* const pSound = ScavengerGameModeComponentInternal::findSound( event._kind ) )
                getSoundQueue().queueEvent( pSound );
            switch ( event._kind )
            {
                case Kind::ThreatSpawned:
                {
                    const vector<AreaDef>& listArea = _expedition.getFacility().getGraph().getAreas();
                    if ( listArea.empty() )
                        break; // 낸 뒤 같은 걸음에 떠났다
                    ScavengerThreat threat;
                    threat._areaID        = event._bIndoor == SW_TRUE ? listArea.back()._id : hashed_string( ScavengerExpedition::kOutsideAreaID ); // 실내 위협은 가장 나중 방에서 나온다
                    threat._spawnID       = static_cast<uint32>( event._value );
                    threat._stepRemaining = _threatStepSeconds;
                    threat._lifeRemaining = _threatLifeSeconds;
                    threat._bIndoor       = event._bIndoor;
                    _listThreat.push_back( threat );
                    break;
                }
                case Kind::Landed:
                {
                    SW_LOG_INFO_ID( "Scavenger.Landed", "[Scavenger] landed on '%#' - scrap worth %#", event._id.c_str(), event._value );
                    break;
                }
                case Kind::PlayerDied:
                {
                    SW_LOG_INFO_ID( "Scavenger.CrewDied", "[Scavenger] crew member %# died", event._player.value() );
                    break;
                }
                case Kind::ScrapSold:
                {
                    SW_LOG_INFO_ID( "Scavenger.Sold", "[Scavenger] sold for %# credits (quota %# of %#)", event._value, getFulfilled(), getQuota() );
                    break;
                }
                case Kind::QuotaMet:
                {
                    if ( _expedition.getQuota().getCycle() >= _cyclesToWin )
                        _result = ScavengerResult::Win;
                    break;
                }
                case Kind::GameOver:
                {
                    _result = ScavengerResult::Lose;
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
        if ( _result == ScavengerResult::Ongoing )
            return;
        endMatch( hashed_string( _result == ScavengerResult::Win ? kWinResult : kLoseResult ) );
        getSoundQueue().queueEvent( _result == ScavengerResult::Win ? ScavengerGameModeComponentInternal::kEventWon : ScavengerGameModeComponentInternal::kEventLost );
        SW_LOG_INFO_ID( "Scavenger.Ended", "[Scavenger] %# - credits %#", _result == ScavengerResult::Win ? "quota met" : "quota missed", getCredits() );
    }

    void ScavengerGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( _pCatalog != nullptr )
            addMoneyResult( outListValue, _expedition.getCredits() - _pCatalog->getQuotaSettings()._startCredits );
    }

    bool ScavengerGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject                            = GameObjectHandle{};
        const ScavengerCrewMember* const pPlayer  = _expedition.findCrewMember( ScavengerGameModeComponentInternal::kPlayer );
        const AbsolutePosition           position = pPlayer != nullptr ? computeAreaPosition( pPlayer->_areaID ) : AbsolutePosition{};
        outFocusPosition                          = position._value;
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( position );
        return isStarted();
    }

    bool ScavengerGameModeComponent::isPlayerInShip() const
    {
        const ScavengerCrewMember* const pPlayer = _expedition.findCrewMember( ScavengerGameModeComponentInternal::kPlayer );
        return pPlayer != nullptr && pPlayer->_areaID == hashed_string( ScavengerExpedition::kShipAreaID );
    }

    int32 ScavengerGameModeComponent::countCarried() const
    {
        int32 count = 0;
        for ( int32 player = 0; player < _expedition.getCrewCount(); ++player )
        {
            count += _expedition.findCrewMember( player )->_carry.getCount();
        }
        return count;
    }

    float32 ScavengerGameModeComponent::computeDayRatio() const
    {
        if ( _pCatalog == nullptr )
            return 0.0f;
        const ScavengerDayDef& day = _pCatalog->getDaySettings();
        return MathUtil::saturate( _expedition.getHoursOnMoon() / ( day._departHour - day._arrivalHour ) );
    }

    const ShopDef* ScavengerGameModeComponent::findTerminal() const
    {
        return _pCatalog != nullptr && _pShopCatalog != nullptr ? _pShopCatalog->find( _pCatalog->getTerminalShopID() ) : nullptr;
    }

    int32 ScavengerGameModeComponent::computeTerminalPrice( const hashed_string& itemID ) const
    {
        return _pCatalog != nullptr ? _expedition.getShop().computeBuyPrice( _pCatalog->getTerminalShopID(), itemID ).value_or( 0 ) : 0;
    }

    int32 ScavengerGameModeComponent::getHour() const
    {
        const GameStateComponent* const pState = GameStateComponent::findOnOwner( *this );
        return pState != nullptr ? pState->getClock().getHourInt() : 0;
    }

    int32 ScavengerGameModeComponent::computeWeatherPercent( const utf8* pValueName ) const
    {
        const GameStateComponent* const pState = GameStateComponent::findOnOwner( *this );
        const float32                   value  = pState != nullptr ? pState->getWeather().computeValue( hashed_string( pValueName ) ) : 0.0f;
        return static_cast<int32>( MathUtil::round( ( value > 0.0f ? value : 1.0f ) * ScavengerGameModeComponentInternal::kPercent ) );
    }

    void ScavengerGameModeComponent::serializeGameState( StateIO& io )
    {
        using Internal = ScavengerGameModeComponentInternal;
        io.header( Internal::kStateFormat );
        if ( io.isOk() == false )
            return;
        // 원정은 지갑 · 깃발 · 창고를 빌린다 — 지역 사본에 읽으면 빌린 주소가 사본을 가리킨다. 새로 연 원정에 바로 읽는다(거절되면 베이스가 앞 상태로 되돌린다).
        if ( io.isReading() && ( _pCatalog == nullptr || openExpedition() == false ) )
        {
            SW_LOG_ERROR_ID( "Scavenger.StateNeedsCatalog", "Scavenger game mode: state read before the catalog is bound - refused" );
            io.fail();
            return;
        }
        io.value( _wallet );
        io.value( _flags );
        io.value( _storage );
        io.value( _expedition ); // 위협 · 사람 비트 · 결과는 상태 칸이다(`GameModeComponent::serializeState`)
        for ( Countdown& step : _listStep )
        {
            io.value( step );
        }
        io.end();
    }
} // namespace sw
