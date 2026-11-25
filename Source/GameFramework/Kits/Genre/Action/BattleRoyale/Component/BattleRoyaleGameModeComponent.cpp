#include "pch.h"

#include "GameFramework/Kits/Genre/Action/BattleRoyale/Component/BattleRoyaleGameModeComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Minigame/MinigameStack.h"

namespace sw
{
    namespace
    {
        struct BattleRoyaleGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "BRGM" );
            static constexpr uint32 kStateVersion = 0;

            static constexpr const utf8* kWinResult  = "win";
            static constexpr const utf8* kLoseResult = "lose";

            // 한 씨앗에서 갈래마다 다른 수열을 뽑는 소금이다.
            static constexpr uint32 kFlightSalt = 1;
            static constexpr uint32 kPersonSalt = 2;
            static constexpr uint32 kLootSalt   = 3;

            static constexpr uint32 kMinPersonBytes     = 22; ///< 사람 하나(무기 상태를 뺀 최소): 시각(4) + 깃발(1) + 자리(12) + 단계(1) + 총(4)
            static constexpr uint32 kMinGroundItemBytes = 20; ///< 무더기 하나: 위치(8) + 이름(4) + 개수(4) + 지점(4)

            static constexpr float32 kAttackRangeRatio = 0.8f; ///< 컴퓨터는 사거리의 이 비율까지 다가간다(사거리 끝에서 멈칫거리지 않게)
            static constexpr float32 kMinDirection     = 1.0e-3f;
            static constexpr float32 kRouteReach       = 0.3f; ///< 길의 점에 닿았다고 보는 거리(m)

            // 사운드 이벤트 이름 — 클립 · 범위 · 상한은 게임의 사운드 이벤트 데이터(audioevents.xml)가 정한다.
            static constexpr const utf8* kEventJump          = "BattleRoyaleJump";
            static constexpr const utf8* kEventParachute     = "BattleRoyaleParachute";
            static constexpr const utf8* kEventArmorBroken   = "BattleRoyaleArmorBroken";
            static constexpr const utf8* kEventDowned        = "BattleRoyaleDowned";
            static constexpr const utf8* kEventKilled        = "BattleRoyaleKilled";
            static constexpr const utf8* kEventRevived       = "BattleRoyaleRevived";
            static constexpr const utf8* kEventWeaponTaken   = "BattleRoyaleWeaponTaken";
            static constexpr const utf8* kEventItemTaken     = "BattleRoyaleItemTaken";
            static constexpr const utf8* kEventSupplyDrop    = "BattleRoyaleSupplyDrop";
            static constexpr const utf8* kEventZoneRevealed  = "BattleRoyaleZoneRevealed";
            static constexpr const utf8* kEventZoneShrink    = "BattleRoyaleZoneShrink";
            static constexpr const utf8* kEventFinalZone     = "BattleRoyaleFinalZone";
            static constexpr const utf8* kEventWon           = "BattleRoyaleWon";
            static constexpr const utf8* kEventLost          = "BattleRoyaleLost";
            static constexpr const utf8* kEventReviveStarted = "BattleRoyaleReviveStarted";
            static constexpr const utf8* kEventReviveBroken  = "BattleRoyaleReviveBroken";

            /** @brief 판의 알림 @p kind 가 그 사람(`_player`)의 자리에서 내는 소리입니다. 소리가 없는 알림이면 nullptr 입니다. */
            static const utf8* findSound( BattleRoyaleEvent::Kind kind )
            {
                switch ( kind )
                {
                    case BattleRoyaleEvent::Kind::PlayerDowned:
                    {
                        return kEventDowned;
                    }
                    case BattleRoyaleEvent::Kind::PlayerKilled:
                    {
                        return kEventKilled;
                    }
                    case BattleRoyaleEvent::Kind::PlayerRevived:
                    {
                        return kEventRevived;
                    }
                    case BattleRoyaleEvent::Kind::ArmorBroken:
                    {
                        return kEventArmorBroken;
                    }
                    case BattleRoyaleEvent::Kind::ReviveStarted:
                    {
                        return kEventReviveStarted;
                    }
                    case BattleRoyaleEvent::Kind::ReviveInterrupted:
                    {
                        return kEventReviveBroken;
                    }
                    case BattleRoyaleEvent::Kind::TeamEliminated: // 사람의 자리가 없는 알림이다 — 보급 상자 · 판 끝은 `settle` 이 낸다
                    case BattleRoyaleEvent::Kind::SupplyDropLanded:
                    case BattleRoyaleEvent::Kind::MatchEnded:
                    {
                        break;
                    }
                }
                return nullptr;
            }

            /** @brief @p from 에서 @p to 쪽 방향(길이 1, 같은 자리면 0)입니다. */
            static float2 makeDirection( const float2& from, const float2& to ) { return ( to - from ).normalizeOr( float2{ 0.0f, 0.0f }, kMinDirection ); }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( battleRoyalePhase, "BattleRoyale.Phase", "Battle royale phase (0 drop, 1 battle, 2 ended)", BattleRoyaleGameModeComponent, &BattleRoyaleGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( battleRoyaleRemaining, "BattleRoyale.Remaining", "People not dead (downed included)", BattleRoyaleGameModeComponent, &BattleRoyaleGameModeComponent::getRemaining );
    SW_AUTOMATION_GETTER_WATCH( battleRoyaleZoneRadius, "BattleRoyale.ZoneRadius", "Radius of the safe zone (m)", BattleRoyaleGameModeComponent, &BattleRoyaleGameModeComponent::getZoneRadius );
    SW_AUTOMATION_GETTER_WATCH( battleRoyalePlayerMagazine, "BattleRoyale.PlayerMagazine", "Rounds in the player weapon", BattleRoyaleGameModeComponent, &BattleRoyaleGameModeComponent::getPlayerMagazine );
    SW_AUTOMATION_GETTER_WATCH( battleRoyalePlayerDropStage, "BattleRoyale.PlayerDropStage", "Player drop stage (0 on the plane, 1 falling, 2 landed)", BattleRoyaleGameModeComponent,
                                &BattleRoyaleGameModeComponent::getPlayerDropStage );

    BattleRoyaleGameModeComponent::BattleRoyaleGameModeComponent()
        : _squadCount{ 3 }
        , _squadSize{ 2 }
        , _seed{ 7 }
        , _matchSeed{ 1 }
        , _groundHeight{ 0.0f }
        , _playerSpeed{ 5.0f }
        , _botSpeed{ 4.0f }
        , _bodyRadius{ 0.35f }
        , _eyeHeight{ 1.2f }
        , _botRouteInterval{ 0.5f }
        , _listWeaponID{}
        , _startReserveAmmo{ 24 }
        , _playerHitChance{ 0.75f }
        , _botHitChance{ 0.3f }
        , _headChance{ 0.2f }
        , _pickupRadius{ 1.5f }
        , _reviveRange{ 1.5f }
        , _botRescueRange{ 20.0f }
        , _botLootRange{ 40.0f }
        , _lootDataPath{}
        , _lootSpotKind{}
        , _listLootPosition{}
        , _resultHoldSeconds{ 3.0f }
        , _victoryMoney{ 60 }
        , _pCatalog{}
        , _pWeaponCatalog{}
        , _pProjectileCatalog{}
        , _pItemCatalog{}
        , _pLootCatalog{}
        , _terrain{}
        , _match{}
        , _flightPath{}
        , _timer{}
        , _random{}
        , _resultHold{}
        , _listPerson{}
        , _listGroundItem{}
        , _listEvent{}
        , _listZoneEvent{}
        , _flightTime{ 0.0f }
        , _phase{ BattleRoyalePhase::Drop }
        , _groundItemRevision{ 0 }
        , _commandMove{ 0.0f, 0.0f }
        , _bFireHeld{ SW_FALSE }
        , _bUseHeld{ SW_FALSE }
        , _bUseQueued{ SW_FALSE }
        , _bFireQueued{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    BattleRoyaleGameModeComponent::~BattleRoyaleGameModeComponent() = default;

    bool BattleRoyaleGameModeComponent::openMatch( BattleRoyaleMatch& outMatch ) const
    {
        if ( outMatch.initialize( *_pCatalog, *_pItemCatalog, _pLootCatalog.get(), _matchSeed, _terrain ) == false )
        {
            SW_LOG_ERROR_ID( "BattleRoyale.OpenFailed", "Battle royale game mode: the match refused the catalog (map size or supply drop loot) - the match does not open" );
            return false;
        }
        for ( int32 squad = 0; squad < _squadCount; ++squad )
        {
            fixed_string<constant::buffer::kMax32> name;
            formatstring( name.data(), name.capacity(), "Squad%#", squad );
            const int32 team = outMatch.addTeam( hashed_string( name.view() ) );
            for ( int32 member = 0; member < _squadSize; ++member )
            {
                if ( outMatch.addPlayer( team ).has_value() )
                    continue;
                SW_LOG_ERROR_ID( "BattleRoyale.PersonRefused", "Battle royale game mode: the catalog player settings were refused - the match does not open" );
                return false;
            }
        }
        return true;
    }

    bool BattleRoyaleGameModeComponent::prepareDrop()
    {
        using Internal = BattleRoyaleGameModeComponentInternal;
        if ( BattleRoyaleFlightPath::makeRandom( _pCatalog->getFlightSettings(), _pCatalog->getMapSize(), GameRandom::deriveSeed( _matchSeed, Internal::kFlightSalt ), _flightPath ) == false )
            return false; // 비행 설정이 범위 밖이면 키트가 오류를 남긴다
        _listPerson.clear();
        for ( int32 person = 0; person < _match.getPlayerCount(); ++person )
        {
            Person entry;
            entry._jumpTime = _random.nextRange( _flightPath.computeJumpOpenTime(), _flightPath.computeJumpCloseTime() );
            if ( equipWeapon( entry, 0 ) == false )
                return false; // 총 목록은 여는 순간 이미 봤다
            entry._weapon.addReserveAmmo( _startReserveAmmo );
            _listPerson.push_back( std::move( entry ) );
        }
        _listGroundItem.clear();
        if ( _pLootCatalog != nullptr )
        {
            vector<BattleRoyaleLootSpot> listSpot;
            for ( const float2& position : _listLootPosition )
            {
                listSpot.push_back( BattleRoyaleLootSpot{ position, _lootSpotKind } );
            }
            (void)BattleRoyaleGroundItem::placeMapLoot( *_pCatalog, *_pLootCatalog, listSpot, GameRandom::deriveSeed( _matchSeed, Internal::kLootSalt ), _listGroundItem );
        }
        ++_groundItemRevision;
        return true;
    }

    bool BattleRoyaleGameModeComponent::startGame()
    {
        const bool bBound = acquireBoundCatalog( _pCatalog ) && acquireBoundCatalog( _pWeaponCatalog ) && acquireBoundCatalog( _pProjectileCatalog ) && acquireBoundCatalog( _pItemCatalog );
        if ( bBound == false || validateWeapons() == false )
            return false;
        const bool bNeedsLoot = _listLootPosition.empty() == false || _pCatalog->getSupplyDropSettings()._listTime.empty() == false;
        _pLootCatalog         = SharedAssetRef<LootCatalog>{};
        if ( bNeedsLoot && acquireRequired( _lootDataPath, _pLootCatalog ) == false )
            return false;
        if ( _listLootPosition.empty() == false && _pCatalog->findLootSpot( _lootSpotKind ) == nullptr )
        {
            SW_LOG_ERROR_ID( "BattleRoyale.UnknownLootSpot", "Battle royale game mode: loot spot '%#' is not in the catalog - the match does not open", _lootSpotKind.c_str() );
            return false;
        }
        _matchSeed                              = WorldSeed::makeForWorld( *this, _seed ).getValue(); // 다음 구역 · 비행 경로 · 전리품은 서버만 안다
        _random                                 = GameRandom( GameRandom::deriveSeed( _matchSeed, BattleRoyaleGameModeComponentInternal::kPersonSalt ) );
        const GameObjectManager* const pManager = getObjectManager();
        SW_ASSERT( pManager != nullptr && "a game mode starts inside a scene" );
        _terrain.initialize( *pManager, BattleRoyaleBodySettings{ _groundHeight, _bodyRadius, _eyeHeight } );
        if ( openMatch( _match ) == false || prepareDrop() == false )
            return false;
        _timer.reset();
        _resultHold.clear();
        _commandMove = float2{ 0.0f, 0.0f };
        _bFireHeld   = SW_FALSE;
        _bUseHeld    = SW_FALSE;
        _bUseQueued  = SW_FALSE;
        _bFireQueued = SW_FALSE;
        return true;
    }

    void BattleRoyaleGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "BattleRoyale.Started", "[BattleRoyale] %# squads of %# on the plane, %# item piles on the ground - the player input arrives as commands (move, fire, use)", _squadCount,
                        _squadSize, _listGroundItem.size() );
    }

    bool BattleRoyaleGameModeComponent::submitPlayerInput( const float2& move, bool bFireHeld, bool bUseHeld, bool bUsePressed )
    {
        if ( isAcceptingInput() == false )
            return false;
        if ( bFireHeld && _bFireHeld == SW_FALSE )
            _bFireQueued = SW_TRUE;
        _commandMove = move;
        _bFireHeld   = bFireHeld ? SW_TRUE : SW_FALSE;
        _bUseHeld    = bUseHeld ? SW_TRUE : SW_FALSE;
        if ( bUsePressed )
            _bUseQueued = SW_TRUE;
        return true;
    }

    AbsolutePosition BattleRoyaleGameModeComponent::getPersonPosition( int32 person ) const
    {
        const BattleRoyalePlayer* const pPlayer = _match.findPlayer( person );
        if ( pPlayer == nullptr || person >= getPersonCount() )
            return AbsolutePosition{};
        const Person& entry = _listPerson[static_cast<size_t>( person )];
        if ( entry._bJumped == SW_FALSE )
        {
            const float2 plane = _flightPath.computePosition( _flightTime );
            return AbsolutePosition{ float3( plane._x, _groundHeight + _flightPath._altitude, plane._y ) };
        }
        if ( entry._skydiver.isLanded() == false )
            return AbsolutePosition{ entry._skydiver.getPosition() };
        return toAbsolute( pPlayer->_position );
    }

    float32 BattleRoyaleGameModeComponent::computePlayerHealthRatio() const
    {
        const BattleRoyalePlayer* const pPlayer = _match.findPlayer( kPlayer );
        if ( pPlayer == nullptr || pPlayer->isAlive() == false )
            return 0.0f;
        return pPlayer->_healthMachine.getHealth() / pPlayer->_healthMachine.getSettings()._maxHealth;
    }

    bool BattleRoyaleGameModeComponent::isPlayerDowned() const
    {
        const BattleRoyalePlayer* const pPlayer = _match.findPlayer( kPlayer );
        return pPlayer != nullptr && pPlayer->isDowned();
    }

    int32 BattleRoyaleGameModeComponent::getPlayerDropStage() const
    {
        if ( _listPerson.empty() || _listPerson[kPlayer]._bJumped == SW_FALSE )
            return 0;
        return _listPerson[kPlayer]._skydiver.isLanded() ? 2 : 1;
    }

    bool BattleRoyaleGameModeComponent::readCameraFocus( GameObjectHandle& outFocusObject, float3& outFocusPosition ) const
    {
        outFocusObject   = GameObjectHandle{};
        outFocusPosition = getPersonPosition( kPlayer )._value;
        if ( const GameObjectManager* const pManager = getObjectManager() )
            outFocusPosition = pManager->getWorldOrigin().toLocal( outFocusPosition );
        return isStarted();
    }

    void BattleRoyaleGameModeComponent::landPerson( int32 person )
    {
        const float3  landing = _listPerson[static_cast<size_t>( person )]._skydiver.getPosition();
        const float32 mapSize = _pCatalog->getMapSize();
        const float2  inMap{ MathUtil::clamp( landing._x, 0.0f, mapSize ), MathUtil::clamp( landing._z, 0.0f, mapSize ) };
        _match.setPlayerPosition( person, _terrain.findStandingSpot( inMap, mapSize ) );
    }

    void BattleRoyaleGameModeComponent::stepDrop( float32 deltaTime )
    {
        using Internal = BattleRoyaleGameModeComponentInternal;
        _flightTime += deltaTime;
        const bool  bAutoPlay   = isAutoPlayOn();
        const bool  bForced     = _flightTime >= _flightPath.computeJumpCloseTime();
        const bool  bJumpOpen   = _flightPath.canJump( _flightTime );
        int32       landedCount = 0;
        const int32 personCount = getPersonCount();
        for ( int32 person = 0; person < personCount; ++person )
        {
            Person&    entry  = _listPerson[static_cast<size_t>( person )];
            const bool bHuman = person == kPlayer && bAutoPlay == false;
            if ( entry._bJumped == SW_FALSE )
            {
                const bool bWants = bHuman ? _bUseQueued == SW_TRUE : _flightTime >= entry._jumpTime;
                if ( bForced == false && ( bWants == false || bJumpOpen == false ) )
                    continue;
                const float2 plane = _flightPath.computePosition( _flightTime );
                entry._bJumped     = SW_TRUE;
                if ( person == kPlayer )
                    getSoundQueue().queueEvent( Internal::kEventJump );
                entry._skydiver.initialize( _pCatalog->getFallSettings(), float3( plane._x, _groundHeight + _flightPath._altitude, plane._y ), _groundHeight );
                continue;
            }
            if ( entry._skydiver.isLanded() )
            {
                ++landedCount;
                continue;
            }
            const float3                position    = entry._skydiver.getPosition();
            const float2                steer       = bHuman ? _commandMove : Internal::makeDirection( float2{ position._x, position._z }, _match.getZone().getNextCenter() );
            const BattleRoyaleFallStage stageBefore = entry._skydiver.getStage();
            if ( bHuman && _bUseQueued == SW_TRUE )
                (void)entry._skydiver.tryOpenParachute(); // 이미 폈으면 할 일이 없다
            entry._skydiver.update( deltaTime, steer );
            const bool bOpened = stageBefore != BattleRoyaleFallStage::Parachute && entry._skydiver.getStage() == BattleRoyaleFallStage::Parachute;
            if ( person == kPlayer && bOpened )
                getSoundQueue().queueEvent( Internal::kEventParachute );
            if ( entry._skydiver.isLanded() )
                landPerson( person );
        }
        _bUseQueued  = SW_FALSE;
        _bFireQueued = SW_FALSE;
        if ( landedCount < personCount )
            return;
        _phase = BattleRoyalePhase::Battle;
        _match.start();
        SW_LOG_INFO_ID( "BattleRoyale.BattleStarted", "[BattleRoyale] everyone landed - %# people, zone radius %#", _match.countRemainingPlayers(), getZoneRadius() );
    }

    template <typename TPredicate>
    optional<int32> BattleRoyaleGameModeComponent::findNearestPerson( int32 person, float32 range, TPredicate isCandidate ) const
    {
        const BattleRoyalePlayer& self        = *_match.findPlayer( person );
        float32                   bestSquared = range * range;
        optional<int32>           best{};
        for ( int32 other = 0; other < _match.getPlayerCount(); ++other )
        {
            if ( other == person )
                continue;
            const BattleRoyalePlayer& candidate       = *_match.findPlayer( other );
            const float32             distanceSquared = float2::getDistanceSquared( self._position, candidate._position );
            if ( distanceSquared > bestSquared || isCandidate( candidate ) == false )
                continue;
            bestSquared = distanceSquared;
            best        = other;
        }
        return best;
    }

    optional<int32> BattleRoyaleGameModeComponent::findNearestEnemy( int32 person ) const
    {
        const int32 team = _match.findPlayer( person )->_team;
        return findNearestPerson( person, _pCatalog->getMapSize() * 2.0f, [team]( const BattleRoyalePlayer& other )
        { return other._team != team && other.isAlive(); } );
    }

    optional<int32> BattleRoyaleGameModeComponent::findVisibleEnemy( int32 person, float32 range ) const
    {
        const BattleRoyalePlayer& self = *_match.findPlayer( person );
        return findNearestPerson( person, range, [this, &self]( const BattleRoyalePlayer& other )
        {
            return other._team != self._team && other.isAlive() && _terrain.hasLineOfSight( self._position, other._position );
        } );
    }

    optional<int32> BattleRoyaleGameModeComponent::findDownedMate( int32 person, float32 range ) const
    {
        const int32 team = _match.findPlayer( person )->_team;
        return findNearestPerson( person, range, [team]( const BattleRoyalePlayer& other )
        { return other._team == team && other.isDowned(); } );
    }

    void BattleRoyaleGameModeComponent::walkRoute( int32 person, const float2& goal, float32 deltaTime )
    {
        using Internal        = BattleRoyaleGameModeComponentInternal;
        Person&      entry    = _listPerson[static_cast<size_t>( person )];
        const float2 position = _match.findPlayer( person )->_position;
        (void)entry._routeHold.update( deltaTime ); // 끝났는지는 바로 아래에서 본다
        if ( entry._routeHold.isActive() == false )
        {
            _terrain.findRoute( position, goal, entry._listRoute );
            entry._routeHold.start( _botRouteInterval );
        }
        while ( entry._listRoute.size() > 1 && float2::getDistance( position, entry._listRoute.front() ) <= Internal::kRouteReach )
        {
            entry._listRoute.erase( entry._listRoute.begin() );
        }
        if ( entry._listRoute.empty() == false )
            movePerson( person, Internal::makeDirection( position, entry._listRoute.front() ), _botSpeed, deltaTime );
    }

    void BattleRoyaleGameModeComponent::movePerson( int32 person, const float2& direction, float32 speed, float32 deltaTime )
    {
        const float32 length = direction.getLength();
        if ( length < BattleRoyaleGameModeComponentInternal::kMinDirection )
            return;
        const float32 mapSize  = _pCatalog->getMapSize();
        const float2  step     = direction * ( speed * deltaTime / MathUtil::max( 1.0f, length ) ); // 대각선 입력이 더 빠르지 않게
        const float2  position = _terrain.walk( _match.findPlayer( person )->_position, step );
        _match.setPlayerPosition( person, float2{ MathUtil::clamp( position._x, 0.0f, mapSize ), MathUtil::clamp( position._y, 0.0f, mapSize ) } );
    }

    bool BattleRoyaleGameModeComponent::validateWeapons() const
    {
        bool bValid = _listWeaponID.empty() == false;
        for ( const CatalogKey<WeaponDef>& weaponID : _listWeaponID )
        {
            const WeaponDef* const     pWeapon     = _pWeaponCatalog->find( weaponID.key() );
            const ProjectileDef* const pProjectile = pWeapon != nullptr ? _pProjectileCatalog->find( pWeapon->_projectileID.key() ) : nullptr;
            const bool                 bUsable     = pProjectile != nullptr && pWeapon->canAccept( *pProjectile ) && _pItemCatalog->find( pWeapon->_itemID.key() ) != nullptr &&
                                 _pItemCatalog->find( pWeapon->_ammoID.key() ) != nullptr;
            if ( bUsable == false )
                SW_LOG_ERROR_ID( "BattleRoyale.WeaponRefused", "Battle royale game mode: weapon '%#' needs a projectile it accepts, an item and an ammo item in the tables - the match does not open",
                                 weaponID.c_str() );
            bValid = bValid && bUsable;
        }
        if ( _listWeaponID.empty() )
            SW_LOG_ERROR_ID( "BattleRoyale.NoWeapon", "Battle royale game mode: the weapon list is empty - the match does not open" );
        return bValid;
    }

    bool BattleRoyaleGameModeComponent::equipWeapon( Person& inoutPerson, int32 weaponIndex ) const
    {
        if ( weaponIndex < 0 || static_cast<size_t>( weaponIndex ) >= _listWeaponID.size() )
            return false;
        const WeaponDef* const     pWeapon     = _pWeaponCatalog->find( _listWeaponID[static_cast<size_t>( weaponIndex )].key() );
        const ProjectileDef* const pProjectile = pWeapon != nullptr ? _pProjectileCatalog->find( pWeapon->_projectileID.key() ) : nullptr;
        if ( pProjectile == nullptr || inoutPerson._weapon.equip( *pWeapon, *pProjectile, 0, WorldSeed::makeForWorld( *this, 1u ) ) == false )
            return false;
        inoutPerson._weaponIndex = weaponIndex;
        return true;
    }

    optional<int32> BattleRoyaleGameModeComponent::findWeaponIndex( const hashed_string& itemID ) const
    {
        for ( size_t weaponIndex = 0; weaponIndex < _listWeaponID.size(); ++weaponIndex )
        {
            const WeaponDef* const pWeapon = _pWeaponCatalog->find( _listWeaponID[weaponIndex].key() );
            if ( pWeapon != nullptr && pWeapon->_itemID.key() == itemID )
                return static_cast<int32>( weaponIndex );
        }
        return nullopt;
    }

    bool BattleRoyaleGameModeComponent::isAmmoItem( const hashed_string& itemID ) const
    {
        for ( const CatalogKey<WeaponDef>& weaponID : _listWeaponID )
        {
            const WeaponDef* const pWeapon = _pWeaponCatalog->find( weaponID.key() );
            if ( pWeapon != nullptr && pWeapon->_ammoID.key() == itemID )
                return true;
        }
        return false;
    }

    const ItemDef* BattleRoyaleGameModeComponent::findPlayerWeaponItem() const
    {
        return _listPerson.empty() ? nullptr : _pItemCatalog->find( _listPerson[kPlayer]._weapon.getDef()._itemID.key() );
    }

    int32 BattleRoyaleGameModeComponent::countSpareAmmo( int32 person ) const
    {
        const WeaponState& weapon = _listPerson[static_cast<size_t>( person )]._weapon;
        return weapon.getReserveAmmo() + _match.findPlayer( person )->_inventory.getItemCount( weapon.getDef()._ammoID.key() );
    }

    void BattleRoyaleGameModeComponent::updateWeapon( int32 person, float32 deltaTime )
    {
        WeaponState& weapon = _listPerson[static_cast<size_t>( person )]._weapon;
        weapon.update( deltaTime );
        BattleRoyalePlayer& player = *_match.findPlayerMutable( person );
        if ( weapon.getMagazineAmmo() == 0 )
            (void)player._loadout.tryReload( weapon, player._inventory ); // 재장전 중이거나 탄이 없으면 그대로다
    }

    void BattleRoyaleGameModeComponent::fire( int32 person, bool bTriggerJustPressed, float32 hitChance )
    {
        WeaponState& weapon = _listPerson[static_cast<size_t>( person )]._weapon;
        if ( weapon.isCoolingDown() || weapon.isReloading() )
            return;
        const optional<int32> target = findVisibleEnemy( person, weapon.getShot()._range );
        if ( target.has_value() == false )
            return;
        const float2  from     = _match.findPlayer( person )->_position;
        const float2  to       = _match.findPlayer( *target )->_position;
        const float32 distance = float2::getDistance( from, to );
        const float2  aim      = BattleRoyaleGameModeComponentInternal::makeDirection( from, to );
        Ray           ray;
        ray._origin    = float3{ from._x, _groundHeight + _eyeHeight, from._y };
        ray._direction = float3{ aim._x, 0.0f, aim._y };
        WeaponShot shot;
        if ( weapon.pullTrigger( ray, bTriggerJustPressed, shot ) != WeaponFireResult::Fired )
            return;
        for ( size_t pellet = 0; pellet < shot._listRay.size() && _match.findPlayer( *target )->isAlive(); ++pellet )
        {
            if ( _random.nextChance( hitChance ) == false )
                continue;
            const BattleRoyaleHitZone hitZone = _random.nextChance( _headChance ) ? BattleRoyaleHitZone::Head : BattleRoyaleHitZone::Body;
            (void)_match.applyDamage( *target, person, weapon.getShot().computeDamage( distance, hitZone == BattleRoyaleHitZone::Head ), hitZone ); // 결과는 알림으로 읽는다
        }
    }

    optional<float2> BattleRoyaleGameModeComponent::findWantedItem( int32 person, float32 range ) const
    {
        const BattleRoyalePlayer& player      = *_match.findPlayer( person );
        const Person&             entry       = _listPerson[static_cast<size_t>( person )];
        const WeaponDef&          weapon      = entry._weapon.getDef();
        const bool                bNeedsAmmo  = countSpareAmmo( person ) < weapon._magazineSize;
        float32                   bestSquared = range * range;
        optional<float2>          best{};
        for ( const BattleRoyaleGroundItem& item : _listGroundItem )
        {
            const float32 distanceSquared = float2::getDistanceSquared( player._position, item._position );
            if ( distanceSquared > bestSquared )
                continue;
            const optional<int32> weaponIndex = findWeaponIndex( item._itemID );
            const bool            bBetter     = weaponIndex.has_value() && *weaponIndex > entry._weaponIndex;
            const bool            bAmmo       = bNeedsAmmo && item._itemID == weapon._ammoID.key() && player._inventory.hasRoomFor( item._itemID, item._count );
            if ( bBetter == false && bAmmo == false )
                continue;
            bestSquared = distanceSquared;
            best        = item._position;
        }
        return best;
    }

    bool BattleRoyaleGameModeComponent::tryTakeItem( int32 person, const BattleRoyaleGroundItem& item )
    {
        BattleRoyalePlayer&   player      = *_match.findPlayerMutable( person );
        Person&               entry       = _listPerson[static_cast<size_t>( person )];
        const optional<int32> weaponIndex = findWeaponIndex( item._itemID );
        if ( weaponIndex.has_value() )
        {
            if ( *weaponIndex <= entry._weaponIndex )
                return false;
            // 놓는 총의 탄은 챙긴다(자리가 모자라면 그만큼 버린다).
            const hashed_string oldAmmoID = entry._weapon.getDef()._ammoID.key();
            const int32         oldAmmo   = entry._weapon.getMagazineAmmo() + entry._weapon.getReserveAmmo();
            if ( equipWeapon( entry, *weaponIndex ) == false )
                return false;
            if ( oldAmmo > 0 )
                (void)player._inventory.addItem( oldAmmoID, oldAmmo );
            SW_LOG_INFO_ID( "BattleRoyale.WeaponTaken", "[BattleRoyale] person %# takes weapon '%#'", person, entry._weapon.getDef()._id.c_str() );
            return true;
        }
        if ( isAmmoItem( item._itemID ) )
        {
            const bool bRoom = player._inventory.hasRoomFor( item._itemID, item._count );
            if ( bRoom )
                (void)player._inventory.addItem( item._itemID, item._count ); // 자리를 봤다
            return bRoom;
        }
        // 같은 슬롯을 이미 채웠으면 남겨 둔다 — 뒤에 오는 사람이 줍는다.
        const BattleRoyaleArmorDef* const pArmor = _pCatalog->findArmor( item._itemID );
        if ( pArmor == nullptr )
            return player._loadout.getBackpackID().empty() && player._loadout.tryEquipBackpack( item._itemID, player._inventory );
        const BattleRoyaleArmorSlot& slot = pArmor->_slot == BattleRoyaleCatalog::getHelmetSlotName() ? player._loadout.getHelmet() : player._loadout.getVest();
        return slot.isEmpty() && player._loadout.tryEquipArmor( item._itemID );
    }

    void BattleRoyaleGameModeComponent::pickUpItems( int32 person )
    {
        const float2 position     = _match.findPlayer( person )->_position;
        const size_t before       = _listGroundItem.size();
        const int32  weaponBefore = _listPerson[static_cast<size_t>( person )]._weaponIndex;
        (void)VectorUtil::removeIf( _listGroundItem, [&]( const BattleRoyaleGroundItem& item )
        {
            return float2::getDistanceSquared( item._position, position ) <= _pickupRadius * _pickupRadius && tryTakeItem( person, item );
        } );
        if ( _listGroundItem.size() == before )
            return;
        ++_groundItemRevision;
        if ( person == kPlayer )
            getSoundQueue().queueEvent( weaponBefore != _listPerson[static_cast<size_t>( person )]._weaponIndex ? BattleRoyaleGameModeComponentInternal::kEventWeaponTaken : BattleRoyaleGameModeComponentInternal::kEventItemTaken );
    }

    void BattleRoyaleGameModeComponent::stepPlayer( float32 deltaTime )
    {
        const bool bReviving = _match.findPlayer( kPlayer )->_revivingTarget >= 0;
        const bool bMoving   = _commandMove.getLength() >= BattleRoyaleGameModeComponentInternal::kMinDirection;
        if ( bReviving && ( bMoving || _bUseHeld == SW_FALSE ) )
            _match.endRevive( kPlayer );
        else if ( bReviving == false && _bUseHeld == SW_TRUE && bMoving == false )
        {
            const optional<int32> mate = findDownedMate( kPlayer, _reviveRange );
            if ( mate.has_value() )
                (void)_match.beginRevive( kPlayer, *mate ); // 자리가 찼으면 다음 걸음에 다시 본다
        }
        movePerson( kPlayer, _commandMove, _playerSpeed, deltaTime );
        if ( _bFireHeld == SW_TRUE )
            fire( kPlayer, _bFireQueued == SW_TRUE, _playerHitChance );
    }

    void BattleRoyaleGameModeComponent::stepBot( int32 person, float32 deltaTime )
    {
        using Internal                         = BattleRoyaleGameModeComponentInternal;
        const BattleRoyalePlayer& self         = *_match.findPlayer( person );
        const BattleRoyaleZone&   zone         = _match.getZone();
        const bool                bOutside     = zone.isInside( self._position ) == false;
        const bool                bMustMoveOn  = zone.getStage() == BattleRoyaleZoneStage::Shrinking && float2::getDistance( self._position, zone.getNextCenter() ) > zone.getNextRadius();
        const optional<int32>     enemyInRange = canShoot( person ) ? findVisibleEnemy( person, _listPerson[static_cast<size_t>( person )]._weapon.getShot()._range * Internal::kAttackRangeRatio ) : nullopt;
        const bool                bReviving    = self._revivingTarget >= 0;
        const optional<int32>     downedMate   = findDownedMate( person, _botRescueRange );
        const bool                bRescue      = downedMate.has_value() && enemyInRange.has_value() == false && bOutside == false;
        if ( bReviving )
        {
            if ( enemyInRange.has_value() == false )
                return; // 살리는 동안 서 있는다
            _match.endRevive( person );
        }
        if ( bOutside || bMustMoveOn )
        {
            walkRoute( person, zone.getNextCenter(), deltaTime );
        }
        else if ( bRescue )
        {
            const float2 matePosition = _match.findPlayer( *downedMate )->_position;
            if ( float2::getDistance( self._position, matePosition ) <= _reviveRange )
                (void)_match.beginRevive( person, *downedMate ); // 자리가 찼으면 다음 걸음에 다시 본다
            else
                walkRoute( person, matePosition, deltaTime );
        }
        else if ( enemyInRange.has_value() == false )
        {
            const optional<float2> wantedItem = findWantedItem( person, _botLootRange );
            const optional<int32>  enemy      = findNearestEnemy( person );
            if ( wantedItem.has_value() )
                walkRoute( person, *wantedItem, deltaTime );
            else if ( enemy.has_value() )
                walkRoute( person, _match.findPlayer( *enemy )->_position, deltaTime );
        }
        if ( canShoot( person ) )
            fire( person, true, _botHitChance );
    }

    void BattleRoyaleGameModeComponent::stepBattle( float32 deltaTime )
    {
        const bool bAutoPlay = isAutoPlayOn();
        for ( int32 person = 0; person < getPersonCount() && _match.getMatchState().getPhase() == MatchPhase::InProgress; ++person )
        {
            if ( _match.findPlayer( person )->isAlive() == false )
                continue;
            updateWeapon( person, deltaTime );
            if ( person == kPlayer && bAutoPlay == false )
                stepPlayer( deltaTime );
            else
                stepBot( person, deltaTime );
            pickUpItems( person );
            queueWeaponSound( person );
        }
        _bUseQueued  = SW_FALSE;
        _bFireQueued = SW_FALSE;
        _match.update( deltaTime );
    }

    void BattleRoyaleGameModeComponent::endGame()
    {
        if ( _phase == BattleRoyalePhase::Ended )
            return;
        _phase = BattleRoyalePhase::Ended;
        _resultHold.start( _resultHoldSeconds );
        getSoundQueue().queueEvent( hasWon() ? BattleRoyaleGameModeComponentInternal::kEventWon : BattleRoyaleGameModeComponentInternal::kEventLost );
        SW_LOG_INFO_ID( "BattleRoyale.Ended", "[BattleRoyale] the match ends for squad %# - winning team %# (-1 = still fought or draw), %# people left", kPlayerSquad,
                        _match.getMatchState().getWinningTeam(), _match.countRemainingPlayers() );
    }

    void BattleRoyaleGameModeComponent::queuePersonSound( const utf8* pEventName, int32 person )
    {
        if ( const GameObjectManager* const pManager = getObjectManager() )
            getSoundQueue().queueEventAt( pEventName, pManager->getWorldOrigin().toLocal( getPersonPosition( person ) ) );
    }

    void BattleRoyaleGameModeComponent::queueWeaponSound( int32 person )
    {
        if ( const GameObjectManager* const pManager = getObjectManager() )
            _listPerson[static_cast<size_t>( person )]._weapon.queueSound( getSoundQueue(), pManager->getWorldOrigin().toLocal( getPersonPosition( person ) ) );
    }

    void BattleRoyaleGameModeComponent::settle()
    {
        _listEvent.clear();
        _match.pollEvents( _listEvent );
        bool bEnded = false;
        for ( const BattleRoyaleEvent& event : _listEvent )
        {
            const utf8* const pSound = BattleRoyaleGameModeComponentInternal::findSound( event._kind );
            if ( pSound != nullptr && event._player.has_value() )
                queuePersonSound( pSound, *event._player );
            if ( event._kind == BattleRoyaleEvent::Kind::PlayerDowned )
                SW_LOG_INFO_ID( "BattleRoyale.Downed", "[BattleRoyale] person %# is down", event._player.value() );
            else if ( event._kind == BattleRoyaleEvent::Kind::PlayerRevived )
                SW_LOG_INFO_ID( "BattleRoyale.Revived", "[BattleRoyale] person %# is up again", event._player.value() );
            else if ( event._kind == BattleRoyaleEvent::Kind::TeamEliminated )
                SW_LOG_INFO_ID( "BattleRoyale.SquadEliminated", "[BattleRoyale] squad %# takes place %#", event._team.value(), event._value );
            else if ( event._kind == BattleRoyaleEvent::Kind::SupplyDropLanded )
            {
                const vector<BattleRoyaleGroundItem>& listDropped = _match.getSupplyDrops()[static_cast<size_t>( event._value )]._listItem;
                _listGroundItem.insert( _listGroundItem.end(), listDropped.begin(), listDropped.end() );
                ++_groundItemRevision;
                if ( const GameObjectManager* const pManager = getObjectManager() )
                    getSoundQueue().queueEventAt( BattleRoyaleGameModeComponentInternal::kEventSupplyDrop, pManager->getWorldOrigin().toLocal( toAbsolute( event._position ) ) );
                SW_LOG_INFO_ID( "BattleRoyale.SupplyDrop", "[BattleRoyale] a supply drop landed with %# item piles", listDropped.size() );
            }
            const bool bSquadLost = event._kind == BattleRoyaleEvent::Kind::TeamEliminated && event._team == kPlayerSquad;
            bEnded                = bEnded || bSquadLost || event._kind == BattleRoyaleEvent::Kind::MatchEnded;
        }
        _listZoneEvent.clear();
        _match.getZoneMutable().pollEvents( _listZoneEvent );
        for ( const BattleRoyaleZoneEvent& event : _listZoneEvent )
        {
            switch ( event._kind )
            {
                case BattleRoyaleZoneEvent::Kind::NextZoneRevealed:
                {
                    getSoundQueue().queueEvent( BattleRoyaleGameModeComponentInternal::kEventZoneRevealed );
                    SW_LOG_INFO_ID( "BattleRoyale.ZoneRevealed", "[BattleRoyale] the next zone is known (phase %#, radius %#)", event._phase, event._radius );
                    break;
                }
                case BattleRoyaleZoneEvent::Kind::ShrinkStarted:
                {
                    getSoundQueue().queueEvent( BattleRoyaleGameModeComponentInternal::kEventZoneShrink );
                    SW_LOG_INFO_ID( "BattleRoyale.ZoneShrinks", "[BattleRoyale] the zone starts to shrink (phase %#)", event._phase );
                    break;
                }
                case BattleRoyaleZoneEvent::Kind::FinalZone:
                {
                    getSoundQueue().queueEvent( BattleRoyaleGameModeComponentInternal::kEventFinalZone );
                    SW_LOG_INFO_ID( "BattleRoyale.FinalZone", "[BattleRoyale] the final zone stands at radius %#", event._radius );
                    break;
                }
            }
        }
        if ( bEnded )
            endGame();
    }

    void BattleRoyaleGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        if ( _phase == BattleRoyalePhase::Ended )
        {
            if ( _resultHold.update( deltaTime ) )
                (void)finishMinigame( hashed_string( hasWon() ? BattleRoyaleGameModeComponentInternal::kWinResult : BattleRoyaleGameModeComponentInternal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
            return;
        }
        const int32 stepCount = _timer.consume( deltaTime );
        for ( int32 step = 0; step < stepCount && _phase != BattleRoyalePhase::Ended; ++step )
        {
            if ( _phase == BattleRoyalePhase::Drop )
                stepDrop( _timer.getStep() );
            else
                stepBattle( _timer.getStep() );
            settle();
        }
    }

    void BattleRoyaleGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`BattleRoyaleViewComponent`)이 읽어 보인다.
        (void)manager;
        (void)bRespawnViews;
    }

    void BattleRoyaleGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        if ( hasWon() )
            addMoneyResult( outListValue, _victoryMoney );
    }

    void BattleRoyaleGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( BattleRoyaleGameModeComponentInternal::kStateTag, BattleRoyaleGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 판은 같은 카탈로그 · 분대로 연 뒤에 읽는다(키트 계약). 비행 경로는 씨앗으로 `startGame` 이 이미 정했고, 입력 명령은 싣지 않는다.
        BattleRoyaleMatch              restoredMatch;
        FixedStepTimer                 timer          = _timer;
        GameRandom                     random         = _random;
        Countdown                      resultHold     = _resultHold;
        vector<Person>                 listPerson     = _listPerson;
        vector<BattleRoyaleGroundItem> listGroundItem = _listGroundItem;
        if ( io.isReading() && io.check( _pCatalog != nullptr && openMatch( restoredMatch ) ) == false )
            return;
        io.value( io.isWriting() ? _match : restoredMatch );
        io.value( timer );
        io.value( random );
        io.value( resultHold );
        io.list( listPerson, BattleRoyaleGameModeComponentInternal::kMinPersonBytes, [&]( Person& person )
        {
            float3                position = person._skydiver.getPosition();
            BattleRoyaleFallStage stage    = person._skydiver.getStage();
            io.value( person._jumpTime );
            io.flag( person._bJumped );
            io.value( position );
            io.enumValue( stage, BattleRoyaleFallStage::Landed );
            io.value( person._weaponIndex );
            // 총의 정의 · 탄은 표의 것이다 — 같은 총을 먼저 든 뒤 상태를 읽는다(무기 상태의 계약).
            if ( io.isReading() && io.check( equipWeapon( person, person._weaponIndex ) ) == false )
                return;
            io.value( person._weapon );
            if ( io.isWriting() || io.isOk() == false )
                return;
            // 낙하산은 저장한 단계까지 다시 편다. 땅에 닿은 사람은 바닥 높이에서 시작하면 닿은 상태다.
            person._skydiver.initialize( _pCatalog->getFallSettings(), position, _groundHeight );
            if ( stage == BattleRoyaleFallStage::Parachute )
                (void)person._skydiver.tryOpenParachute(); // 낮은 높이면 이미 펴졌다
            io.check( person._bJumped == SW_FALSE || person._skydiver.getStage() == stage );
        } );
        io.list( listGroundItem, BattleRoyaleGameModeComponentInternal::kMinGroundItemBytes, [&]( BattleRoyaleGroundItem& item )
        {
            io.value( item._position );
            io.value( item._itemID );
            io.value( item._count );
            io.value( item._spotIndex );
            io.check( item._count > 0 );
        } );
        io.check( io.isWriting() || static_cast<int32>( listPerson.size() ) == restoredMatch.getPlayerCount() );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _match          = std::move( restoredMatch );
        _timer          = timer;
        _random         = random;
        _resultHold     = resultHold;
        _listPerson     = std::move( listPerson );
        _listGroundItem = std::move( listGroundItem );
        ++_groundItemRevision;
    }
} // namespace sw
