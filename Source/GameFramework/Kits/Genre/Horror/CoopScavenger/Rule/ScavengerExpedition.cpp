#include "pch.h"

#include "GameFramework/Kits/Genre/Horror/CoopScavenger/Rule/ScavengerExpedition.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Foundation/Framework/GameStateRefs.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/World/Environment/WeatherSystem.h"
#include "GameFramework/Base/World/Environment/WorldClock.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "ScavengerExpedition" );

    namespace
    {
        struct ScavengerExpeditionInternal
        {

            static uint32 makeDaySeed( uint32 seed, int32 dayIndex, uint32 salt ) { return GameHash::hashCoord( dayIndex, static_cast<int32>( salt ), seed ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( ScavengerActionResult result )
    {
        switch ( result )
        {
            case ScavengerActionResult::Ok:
                return "Ok";
            case ScavengerActionResult::WrongPhase:
                return "WrongPhase";
            case ScavengerActionResult::UnknownMoon:
                return "UnknownMoon";
            case ScavengerActionResult::NotEnoughCredits:
                return "NotEnoughCredits";
            case ScavengerActionResult::InvalidPlayer:
                return "InvalidPlayer";
            case ScavengerActionResult::Blocked:
                return "Blocked";
            case ScavengerActionResult::NotCompany:
                return "NotCompany";
            case ScavengerActionResult::NotOnShip:
                return "NotOnShip";
        }
        return "Unknown";
    }

    ScavengerExpedition::ScavengerExpedition()
        : _listCrew{}
        , _listShipScrap{}
        , _eventBuffer{}
        , _listSpawnScratch{}
        , _data{}
        , _quota{}
        , _facility{}
        , _indoorDirector{}
        , _outdoorDirector{}
        , _shop{}
        , _random{}
        , _pShipStorage{ nullptr }
        , _pWallet{ nullptr }
        , _pClock{ nullptr }
        , _pWeather{ nullptr }
        , _pMoon{ nullptr }
        , _seed{ 1u }
        , _hoursOnMoon{ 0.0f }
        , _phase{ ScavengerPhase::InOrbit }
        , _bDuskAnnounced{ SW_FALSE }
    {
    }

    void ScavengerExpedition::initialize( const ScavengerExpeditionData& data, const GameStateRefs& refs, Inventory& shipStorage, uint32 seed, int32 crewCount )
    {
        _data = data;
        _seed = seed;
        _random.setSeed( seed );
        _listShipScrap.clear();
        _eventBuffer.clear();
        _facility.clear();
        _pMoon          = nullptr;
        _hoursOnMoon    = 0.0f;
        _phase          = ScavengerPhase::InOrbit;
        _bDuskAnnounced = SW_FALSE;
        _pWallet        = refs._pWallet;
        _pClock         = refs._pClock;
        _pWeather       = refs._pWeather;
        _facility.setFlags( refs._pFlags );
        _facility.bindCatalog( data._pCatalog );
        _shop.initialize( data._pShopCatalog, data._pItemCatalog );
        _pShipStorage = &shipStorage;
        _listCrew.clear();
        _listCrew.resize( static_cast<size_t>( MathUtil::max( 1, crewCount ) ) );
        if ( data._pCatalog == nullptr )
            return;
        _quota.initialize( data._pCatalog->getQuotaSettings() );
        if ( _pWallet != nullptr )
            _pWallet->setBalance( data._pCatalog->getCurrency(), data._pCatalog->getQuotaSettings()._startCredits );
        if ( data._pCatalog->getMoons().empty() == false )
            _pMoon = &data._pCatalog->getMoons().front();
        resetCrew();
    }

    ScavengerActionResult ScavengerExpedition::routeTo( const hashed_string& moonId )
    {
        if ( _phase != ScavengerPhase::InOrbit || _data._pCatalog == nullptr )
            return ScavengerActionResult::WrongPhase;
        const ScavengerMoonDef* pMoon = _data._pCatalog->findMoon( moonId );
        if ( pMoon == nullptr )
            return ScavengerActionResult::UnknownMoon;
        if ( pMoon == _pMoon )
            return ScavengerActionResult::Ok;
        if ( _pWallet == nullptr || _pWallet->trySpend( _data._pCatalog->getCurrency(), pMoon->_routeCost ) == false )
            return ScavengerActionResult::NotEnoughCredits;
        _pMoon = pMoon;
        pushEvent( ScavengerEvent::Kind::Routed, -1, pMoon->_routeCost, pMoon->_id );
        return ScavengerActionResult::Ok;
    }

    ScavengerActionResult ScavengerExpedition::land()
    {
        if ( _phase != ScavengerPhase::InOrbit || _pMoon == nullptr )
            return ScavengerActionResult::WrongPhase;
        const ScavengerCatalog& catalog = *_data._pCatalog;
        _phase                          = ScavengerPhase::Landed;
        _hoursOnMoon                    = 0.0f;
        _bDuskAnnounced                 = SW_FALSE;
        const float32 arrivalHour       = catalog.getDaySettings()._arrivalHour;
        if ( _pClock != nullptr && MathUtil::abs( _pClock->getHour() - arrivalHour ) > 0.01f )
            _pClock->advanceToHour( arrivalHour ); // 아침 — 지났으면 다음 날
        const int32 dayIndex = getDayIndex();
        // 위성 = 날씨 표의 계절. 그날의 씨앗으로 굴린 위성 날씨를 공유 날씨에 바로 건다.
        if ( _pWeather != nullptr && _data._pWeatherCatalog != nullptr )
        {
            WeatherSystem moonWeather;
            moonWeather.initialize( _data._pWeatherCatalog, ScavengerExpeditionInternal::makeDaySeed( _seed, dayIndex, 0x57u ), _pMoon->_id );
            _pWeather->forceWeather( moonWeather.getCurrent(), moonWeather.getRemaining(), true );
        }

        const float32 valueScale = computeWeatherValue( "scrapValue" );
        const uint32  layoutSeed = ScavengerExpeditionInternal::makeDaySeed( _seed ^ _pMoon->_id.getHash(), dayIndex, 0x46u );
        if ( _facility.createLayout( catalog, *_pMoon, layoutSeed, valueScale ) == false )
            SW_LOG_WARNING( "facility layout for '%#' failed to load", _pMoon->_id.c_str() );

        _indoorDirector.initialize( nullptr, 1u );
        _outdoorDirector.initialize( nullptr, 1u );
        if ( _data._pThreatTable != nullptr && _pMoon->_bCompany == SW_FALSE )
        {
            _indoorDirector.initialize( _data._pThreatTable, ScavengerExpeditionInternal::makeDaySeed( _seed, dayIndex, 0x49u ) );
            _indoorDirector.setAllowedTags( vector<hashed_string>{ hashed_string( "Indoor" ) } );
            _outdoorDirector.initialize( _data._pThreatTable, ScavengerExpeditionInternal::makeDaySeed( _seed, dayIndex, 0x4fu ) );
            _outdoorDirector.setAllowedTags( vector<hashed_string>{ hashed_string( "Outdoor" ) } );
        }
        resetCrew();
        pushEvent( ScavengerEvent::Kind::Landed, -1, _facility.computeGroundValue(), _pMoon->_id );
        return ScavengerActionResult::Ok;
    }

    ScavengerActionResult ScavengerExpedition::takeOff()
    {
        if ( _phase != ScavengerPhase::Landed )
            return ScavengerActionResult::WrongPhase;
        departShip( false );
        return ScavengerActionResult::Ok;
    }

    void ScavengerExpedition::update( float32 deltaTime )
    {
        if ( _phase != ScavengerPhase::Landed || deltaTime <= 0.0f || _data._pCatalog == nullptr )
            return;
        const ScavengerDaySettings& day = _data._pCatalog->getDaySettings();
        _hoursOnMoon += deltaTime * 24.0f / day._secondsPerDay;
        const float32 hour = day._arrivalHour + _hoursOnMoon;

        for ( ScavengerCrewMember& member : _listCrew )
        {
            if ( member.isDead() == false )
                member._vitality.update( deltaTime );
        }
        if ( _pMoon->_bCompany == SW_FALSE )
        {
            const float32 threatTime = deltaTime * computeThreatScale();
            (void)_indoorDirector.update( threatTime );
            collectThreatEvents( _indoorDirector, true );
            (void)_outdoorDirector.update( threatTime );
            collectThreatEvents( _outdoorDirector, false );
        }
        if ( _bDuskAnnounced == SW_FALSE && hour >= day._duskHour )
        {
            _bDuskAnnounced = SW_TRUE;
            pushEvent( ScavengerEvent::Kind::Dusk, -1, 0 );
        }
        if ( hour >= day._departHour )
            departShip( true );
    }

    ScavengerActionResult ScavengerExpedition::movePlayer( int32 player, const hashed_string& areaId )
    {
        if ( _phase != ScavengerPhase::Landed )
            return ScavengerActionResult::WrongPhase;
        if ( isValidPlayer( player ) == false || _listCrew[static_cast<size_t>( player )].isDead() )
            return ScavengerActionResult::InvalidPlayer;
        ScavengerCrewMember& member = _listCrew[static_cast<size_t>( player )];
        if ( _facility.canTraverse( member._areaId, areaId ) == false )
            return ScavengerActionResult::Blocked;
        member._areaId = areaId;
        return ScavengerActionResult::Ok;
    }

    ScavengerPickupResult ScavengerExpedition::pickUp( int32 player, int32 uid )
    {
        if ( _phase != ScavengerPhase::Landed || isValidPlayer( player ) == false || _listCrew[static_cast<size_t>( player )].isDead() )
            return ScavengerPickupResult::Unavailable;
        ScavengerCrewMember&        member = _listCrew[static_cast<size_t>( player )];
        const ScavengerPickupResult result = member._carry.evaluatePickup();
        if ( result != ScavengerPickupResult::Ok )
            return result;
        ScavengerScrap scrap;
        if ( _facility.tryTakeScrap( uid, member._areaId, scrap ) == false )
            return ScavengerPickupResult::NotFound;
        return member._carry.add( scrap );
    }

    ScavengerActionResult ScavengerExpedition::dropItem( int32 player, int32 uid )
    {
        if ( _phase != ScavengerPhase::Landed )
            return ScavengerActionResult::WrongPhase;
        if ( isValidPlayer( player ) == false || _listCrew[static_cast<size_t>( player )].isDead() )
            return ScavengerActionResult::InvalidPlayer;
        ScavengerCrewMember& member = _listCrew[static_cast<size_t>( player )];
        ScavengerScrap       scrap;
        if ( member._carry.tryRemove( uid, scrap ) == false )
            return ScavengerActionResult::InvalidPlayer;
        if ( member._areaId == hashed_string( kShipAreaId ) )
        {
            if ( scrap.isBody() )
            {
                _listCrew[static_cast<size_t>( scrap._bodyOf )]._bBodyRecovered = SW_TRUE;
                pushEvent( ScavengerEvent::Kind::BodyRecovered, scrap._bodyOf, 0 );
                return ScavengerActionResult::Ok;
            }
            scrap._day = getDayIndex();
            _listShipScrap.push_back( scrap );
            return ScavengerActionResult::Ok;
        }
        (void)_facility.placeScrap( scrap, member._areaId );
        return ScavengerActionResult::Ok;
    }

    int32 ScavengerExpedition::depositToShip( int32 player )
    {
        if ( isValidPlayer( player ) == false || _listCrew[static_cast<size_t>( player )].isDead() )
            return 0;
        ScavengerCrewMember& member = _listCrew[static_cast<size_t>( player )];
        if ( member._areaId != hashed_string( kShipAreaId ) )
            return 0;
        int32 count = 0;
        while ( member._carry.getCount() > 0 )
        {
            const int32 uid = member._carry.getScraps().front()._uid;
            if ( dropItem( player, uid ) != ScavengerActionResult::Ok )
                break;
            ++count;
        }
        return count;
    }

    float32 ScavengerExpedition::applyDamage( int32 player, float32 amount )
    {
        if ( _phase != ScavengerPhase::Landed || isValidPlayer( player ) == false )
            return 0.0f;
        ScavengerCrewMember& member = _listCrew[static_cast<size_t>( player )];
        if ( member.isDead() )
            return 0.0f;
        const VitalityDamageResult result = member._vitality.applyDamage( amount );
        if ( result._bDied == SW_TRUE )
            handleDeath( player, false );
        return result._healthDamage;
    }

    void ScavengerExpedition::killPlayer( int32 player )
    {
        if ( _phase != ScavengerPhase::Landed || isValidPlayer( player ) == false || _listCrew[static_cast<size_t>( player )].isDead() )
            return;
        _listCrew[static_cast<size_t>( player )]._vitality.kill();
        handleDeath( player, false );
    }

    void ScavengerExpedition::notifyThreatDespawned( bool bIndoor, uint32 spawnId )
    {
        (void)( bIndoor ? _indoorDirector : _outdoorDirector ).notifyDespawned( spawnId );
        _listSpawnScratch.clear();
        ( bIndoor ? _indoorDirector : _outdoorDirector ).drainEvents( _listSpawnScratch );
    }

    int32 ScavengerExpedition::sellAllShipScrap( ScavengerActionResult& outResult )
    {
        outResult = ScavengerActionResult::Ok;
        if ( _phase != ScavengerPhase::Landed )
        {
            outResult = ScavengerActionResult::WrongPhase;
            return 0;
        }
        if ( _pMoon == nullptr || _pMoon->_bCompany == SW_FALSE )
        {
            outResult = ScavengerActionResult::NotCompany;
            return 0;
        }
        const int32 credits = computeSellValue();
        _listShipScrap.clear();
        if ( credits > 0 )
        {
            if ( _pWallet != nullptr )
                _pWallet->add( _data._pCatalog->getCurrency(), credits );
            _quota.addFulfilled( credits );
        }
        pushEvent( ScavengerEvent::Kind::ScrapSold, -1, credits );
        return credits;
    }

    int32 ScavengerExpedition::computeSellValue() const
    {
        if ( _data._pCatalog == nullptr )
            return 0;
        const float32 rate = _data._pCatalog->computeBuyRate( _quota.getDaysLeft() );
        return static_cast<int32>( static_cast<float32>( computeShipValue() ) * rate );
    }

    ShopResult ScavengerExpedition::buyFromTerminal( const hashed_string& itemId, int32 count )
    {
        if ( _data._pCatalog == nullptr || _phase == ScavengerPhase::GameOver )
            return ShopResult::UnknownShop;
        if ( _pWallet == nullptr )
            return ShopResult::NotEnoughMoney;
        return _shop.buy( _data._pCatalog->getTerminalShopId(), itemId, count, *_pWallet, *_pShipStorage );
    }

    void ScavengerExpedition::drainEvents( vector<ScavengerEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    int64 ScavengerExpedition::getCredits() const { return _data._pCatalog != nullptr && _pWallet != nullptr ? _pWallet->getBalance( _data._pCatalog->getCurrency() ) : 0; }

    int32 ScavengerExpedition::computeShipValue() const
    {
        int32 total = 0;
        for ( const ScavengerScrap& scrap : _listShipScrap )
        {
            total += scrap._value;
        }
        return total;
    }

    const ScavengerCrewMember* ScavengerExpedition::findCrewMember( int32 player ) const
    {
        return isValidPlayer( player ) ? &_listCrew[static_cast<size_t>( player )] : nullptr;
    }

    int32 ScavengerExpedition::countAlive() const
    {
        int32 count = 0;
        for ( const ScavengerCrewMember& member : _listCrew )
        {
            count += member.isDead() ? 0 : 1;
        }
        return count;
    }

    float32 ScavengerExpedition::computeThreatScale() const
    {
        if ( _pMoon == nullptr )
            return 0.0f;
        return _pMoon->_risk * computeWeatherValue( "threat" );
    }

    float32 ScavengerExpedition::computeWeatherValue( const utf8* pName ) const
    {
        if ( _pWeather == nullptr )
            return 1.0f;
        const float32 value = _pWeather->computeValue( hashed_string( pName ) );
        return value > 0.0f ? value : 1.0f;
    }

    void ScavengerExpedition::handleDeath( int32 player, bool bLeftBehind )
    {
        ScavengerCrewMember& member = _listCrew[static_cast<size_t>( player )];
        // 든 것은 그 자리에 떨어지고 시신이 남는다. 우주선 안에서 죽으면 시신은 이미 실려 있다.
        vector<ScavengerScrap> listDropped;
        member._carry.takeAll( listDropped );
        const bool bOnShip = member._areaId == hashed_string( kShipAreaId );
        for ( ScavengerScrap& scrap : listDropped )
        {
            if ( bOnShip && scrap.isBody() == false )
            {
                scrap._day = getDayIndex();
                _listShipScrap.push_back( scrap );
            }
            else if ( bOnShip )
            {
                _listCrew[static_cast<size_t>( scrap._bodyOf )]._bBodyRecovered = SW_TRUE;
            }
            else
            {
                (void)_facility.placeScrap( scrap, member._areaId );
            }
        }
        if ( bOnShip )
        {
            member._bBodyRecovered = SW_TRUE;
        }
        else if ( bLeftBehind == false && _data._pCatalog != nullptr )
        {
            ScavengerScrap body;
            body._scrapId    = hashed_string( "body" );
            body._weight     = _data._pCatalog->getCarrySettings()._bodyWeight;
            body._bodyOf     = player;
            body._bTwoHanded = SW_TRUE;
            (void)_facility.placeScrap( body, member._areaId );
        }
        pushEvent( ScavengerEvent::Kind::PlayerDied, player, bLeftBehind ? 1 : 0 );
        if ( bLeftBehind == false && countAlive() == 0 && _phase == ScavengerPhase::Landed )
            departShip( true ); // 모두 죽었다 — 자동 이륙
    }

    void ScavengerExpedition::departShip( bool bAuto )
    {
        _phase = ScavengerPhase::InOrbit; // 남겨진 사람의 죽음이 다시 이륙을 부르지 않게 먼저 바꾼다
        // 우주선 안의 사람이 든 것은 실리고, 밖에 남은 사람은 죽는다.
        for ( size_t index = 0; index < _listCrew.size(); ++index )
        {
            ScavengerCrewMember& member = _listCrew[index];
            if ( member.isDead() )
                continue;
            if ( member._areaId == hashed_string( kShipAreaId ) )
            {
                vector<ScavengerScrap> listCarried;
                member._carry.takeAll( listCarried );
                for ( ScavengerScrap& scrap : listCarried )
                {
                    if ( scrap.isBody() )
                    {
                        _listCrew[static_cast<size_t>( scrap._bodyOf )]._bBodyRecovered = SW_TRUE;
                        pushEvent( ScavengerEvent::Kind::BodyRecovered, scrap._bodyOf, 0 );
                        continue;
                    }
                    scrap._day = getDayIndex();
                    _listShipScrap.push_back( scrap );
                }
                continue;
            }
            member._vitality.kill();
            handleDeath( static_cast<int32>( index ), true );
        }
        pushEvent( ScavengerEvent::Kind::ShipDeparted, -1, bAuto ? 1 : 0 );

        const ScavengerPenaltySettings& penalty   = _data._pCatalog->getPenaltySettings();
        float32                         fineRatio = 0.0f;
        for ( const ScavengerCrewMember& member : _listCrew )
        {
            if ( member.isDead() )
                fineRatio += member._bBodyRecovered == SW_TRUE ? penalty._recoveredFine : penalty._deathFine;
        }
        if ( countAlive() == 0 )
            loseScrapOnWipe();
        const int64 fine = static_cast<int64>( static_cast<float32>( getCredits() ) * MathUtil::saturate( fineRatio ) );
        if ( fine > 0 && _pWallet != nullptr && _pWallet->trySpend( _data._pCatalog->getCurrency(), fine ) )
            pushEvent( ScavengerEvent::Kind::FinePaid, -1, static_cast<int32>( fine ) );

        int32                         overtimeBonus = 0;
        const ScavengerDeadlineResult deadline      = _quota.endDay( _random, overtimeBonus );
        if ( deadline == ScavengerDeadlineResult::QuotaMet )
        {
            if ( overtimeBonus > 0 )
                if ( _pWallet != nullptr )
                    _pWallet->add( _data._pCatalog->getCurrency(), overtimeBonus );
            pushEvent( ScavengerEvent::Kind::QuotaMet, -1, _quota.getQuota() );
        }
        else if ( deadline == ScavengerDeadlineResult::GameOver )
        {
            _phase = ScavengerPhase::GameOver;
            pushEvent( ScavengerEvent::Kind::GameOver, -1, _quota.getFulfilled() );
        }
        if ( _pClock != nullptr && _data._pCatalog != nullptr )
            _pClock->advanceToHour( _data._pCatalog->getDaySettings()._arrivalHour ); // 다음 날 아침
        _shop.advanceDay();
        _facility.clear();
        _indoorDirector.initialize( nullptr, 1u );
        _outdoorDirector.initialize( nullptr, 1u );
        if ( _phase != ScavengerPhase::GameOver )
            resetCrew();
    }

    void ScavengerExpedition::loseScrapOnWipe()
    {
        const ScavengerPenaltySettings& penalty   = _data._pCatalog->getPenaltySettings();
        const int32                     total     = static_cast<int32>( _listShipScrap.size() );
        int32                           loseCount = static_cast<int32>( MathUtil::ceil( static_cast<float32>( total ) * penalty._allDeadLossRatio - 1.0e-4f ) );
        if ( penalty._allDeadMaxKept > 0 )
            loseCount = MathUtil::min( loseCount, MathUtil::max( 0, total - penalty._allDeadMaxKept ) );
        loseCount = MathUtil::clamp( loseCount, 0, total );
        // 잃을 것을 씨앗으로 고른다(섞은 앞쪽) — 남는 것은 실은 순서를 지킨다.
        vector<int32> listOrder;
        for ( int32 index = 0; index < total; ++index )
        {
            listOrder.push_back( index );
        }
        for ( int32 index = total - 1; index > 0; --index )
        {
            std::swap( listOrder[static_cast<size_t>( index )], listOrder[static_cast<size_t>( _random.nextInt( 0, index ) )] );
        }
        vector<uint8> listLost( static_cast<size_t>( total ), SW_FALSE );
        for ( int32 index = 0; index < loseCount; ++index )
        {
            listLost[static_cast<size_t>( listOrder[static_cast<size_t>( index )] )] = SW_TRUE;
        }
        vector<ScavengerScrap> listKept;
        for ( int32 index = 0; index < total; ++index )
        {
            if ( listLost[static_cast<size_t>( index )] == SW_FALSE )
                listKept.push_back( _listShipScrap[static_cast<size_t>( index )] );
        }
        _listShipScrap.swap( listKept );
        pushEvent( ScavengerEvent::Kind::CrewWiped, -1, loseCount );
    }

    void ScavengerExpedition::resetCrew()
    {
        if ( _data._pCatalog == nullptr )
            return;
        VitalitySettings settings;
        settings._maxHealth = _data._pCatalog->getCrewHealth();
        for ( ScavengerCrewMember& member : _listCrew )
        {
            member._vitality.initialize( settings );
            member._carry.initialize( _data._pCatalog->getCarrySettings() );
            member._areaId         = hashed_string( kShipAreaId );
            member._bBodyRecovered = SW_FALSE;
        }
    }

    void ScavengerExpedition::collectThreatEvents( SpawnDirector& director, bool bIndoor )
    {
        _listSpawnScratch.clear();
        director.drainEvents( _listSpawnScratch );
        for ( const SpawnEvent& spawn : _listSpawnScratch )
        {
            if ( spawn._kind != SpawnEvent::Kind::Spawned )
                continue;
            ScavengerEvent event;
            event._kind    = ScavengerEvent::Kind::ThreatSpawned;
            event._id      = spawn._entryId;
            event._value   = static_cast<int32>( spawn._spawnId );
            event._bIndoor = bIndoor ? SW_TRUE : SW_FALSE;
            _eventBuffer.push( event );
        }
    }

    void ScavengerExpedition::pushEvent( ScavengerEvent::Kind kind, int32 player, int32 value, const hashed_string& id )
    {
        ScavengerEvent event;
        event._kind   = kind;
        event._player = player;
        event._value  = value;
        event._id     = id;
        _eventBuffer.push( event );
    }

    int32 ScavengerExpedition::getDayIndex() const { return _pClock != nullptr ? _pClock->getDay() : 0; }

    void ScavengerExpedition::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listCrew.size() );
        for ( const ScavengerCrewMember& member : _listCrew )
        {
            member._vitality.writeState( outArchive );
            member._carry.writeState( outArchive );
            StateArchiveUtil::writeName( outArchive, member._areaId );
            outArchive << member._bBodyRecovered;
        }
        outArchive << static_cast<uint32>( _listShipScrap.size() );
        for ( const ScavengerScrap& scrap : _listShipScrap )
        {
            ScavengerFacility::writeScrap( outArchive, scrap );
        }
        StateArchiveUtil::writeName( outArchive, _pMoon != nullptr ? _pMoon->_id : hashed_string{} );
        outArchive << static_cast<uint8>( _phase );
        outArchive << _bDuskAnnounced;
        outArchive << _seed;
        outArchive << _hoursOnMoon;
        StateArchiveUtil::writeRandom( outArchive, _random );
        _quota.writeState( outArchive );
        _facility.writeState( outArchive );
        _indoorDirector.writeState( outArchive );
        _outdoorDirector.writeState( outArchive );
        _shop.writeState( outArchive );
    }

    bool ScavengerExpedition::readState( Archive& archive )
    {
        if ( _data._pCatalog == nullptr )
            return false;
        uint32 crewCount = 0;
        archive >> crewCount;
        if ( archive.isError() || crewCount != _listCrew.size() )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 데이터 · 빌린 포인터 · 사람마다의 설정은 사본이 그대로 든다.
        ScavengerExpedition restored = *this;
        for ( ScavengerCrewMember& member : restored._listCrew )
        {
            const bool bMemberRead = member._vitality.readState( archive ) && member._carry.readState( archive ) && StateArchiveUtil::readName( archive, member._areaId );
            if ( bMemberRead == false )
                return false;
            archive >> member._bBodyRecovered;
            if ( archive.isError() || member._bBodyRecovered > SW_TRUE )
                return false;
        }

        uint32 scrapCount = 0;
        if ( StateArchiveUtil::readCount( archive, ScavengerFacility::kMinScrapBytes, scrapCount ) == false )
            return false;
        restored._listShipScrap.assign( scrapCount, ScavengerScrap{} );
        for ( ScavengerScrap& scrap : restored._listShipScrap )
        {
            if ( ScavengerFacility::readScrap( archive, scrap ) == false )
                return false;
        }

        hashed_string moonId;
        uint8         phase = 0;
        if ( StateArchiveUtil::readName( archive, moonId ) == false )
            return false;
        archive >> phase;
        archive >> restored._bDuskAnnounced;
        archive >> restored._seed;
        archive >> restored._hoursOnMoon;
        const bool bHeadValid = StateArchiveUtil::readRandom( archive, restored._random ) && phase <= static_cast<uint8>( ScavengerPhase::GameOver ) &&
                                restored._bDuskAnnounced <= SW_TRUE;
        if ( bHeadValid == false )
            return false;
        restored._phase = static_cast<ScavengerPhase>( phase );
        restored._pMoon = moonId.empty() ? nullptr : _data._pCatalog->findMoon( moonId );
        if ( moonId.empty() == false && restored._pMoon == nullptr )
            return false;
        if ( restored._phase == ScavengerPhase::Landed && restored._pMoon == nullptr )
            return false;

        // 위협 감독은 내려 있는 동안만 표를 든다(궤도 · 회사에서는 비어 있다) — 같은 표로 열고 나머지를 읽는다.
        const bool bThreatActive = restored._phase == ScavengerPhase::Landed && restored._pMoon->_bCompany == SW_FALSE && _data._pThreatTable != nullptr;
        restored._indoorDirector.initialize( bThreatActive ? _data._pThreatTable : nullptr, 1u );
        restored._outdoorDirector.initialize( bThreatActive ? _data._pThreatTable : nullptr, 1u );
        const bool bPartRead = restored._quota.readState( archive ) && restored._facility.readState( archive ) &&
                               restored._indoorDirector.readState( archive ) && restored._outdoorDirector.readState( archive ) && restored._shop.readState( archive );
        if ( bPartRead == false )
            return false;
        restored._eventBuffer.clear();
        restored._listSpawnScratch.clear();
        *this = std::move( restored );
        return true;
    }
} // namespace sw
