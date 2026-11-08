#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureTown.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/CreatureLife/CreatureLifeCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Foundation/Random/GameRandom.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/Gameplay/Quest/QuestLog.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

namespace sw
{
    namespace
    {
        struct CreatureTownInternal
        {
            /** @brief 종의 호감도 세력 id 를 반환합니다. 공유 평판에서 키트 접두어로 다른 키트의 세력과 구분하며, 문자열 이어 붙이기는 여기 한 곳에서만 합니다. */
            static hashed_string makeFactionID( const hashed_string& speciesID ) { return hashed_string( string( "creature." ) + speciesID.c_str() ); }

            static constexpr int32 kRotationCount = 4;

            /** @brief 회전한 패턴의 가로와 세로를 반환합니다(홀수 회전이면 서로 바뀝니다). */
            static int2 computeRotatedSize( const HabitatDef& habitat, int32 rotation )
            {
                const GridTopology& pattern = habitat._topology;
                return ( rotation & 1 ) != 0 ? int2{ pattern._height, pattern._width } : int2{ pattern._width, pattern._height };
            }

            /** @brief 회전한 패턴의 (@p rotatedX, @p rotatedY) 셀이 원래 패턴의 어느 셀인지 반환합니다. 90° 를 @p rotation 번 돌린 것입니다. */
            static const HabitatCell& sampleRotated( const HabitatDef& habitat, int32 rotation, int32 rotatedX, int32 rotatedY )
            {
                const int32 width  = habitat._topology._width;
                const int32 height = habitat._topology._height;
                switch ( rotation )
                {
                    case 1:
                        return habitat.getCell( rotatedY, height - 1 - rotatedX );
                    case 2:
                        return habitat.getCell( width - 1 - rotatedX, height - 1 - rotatedY );
                    case 3:
                        return habitat.getCell( width - 1 - rotatedY, rotatedX );
                    default:
                        return habitat.getCell( rotatedX, rotatedY );
                }
            }

            static hashed_string makeDeliverKind() { return hashed_string( "Deliver" ); }

            static hashed_string makeHabitatKind() { return hashed_string( "Habitat" ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    CreatureTown::CreatureTown()
        : _listObject{}
        , _listHabitat{}
        , _listCreature{}
        , _listHouse{}
        , _listOpenRequest{}
        , _eventBuffer{}
        , _settings{}
        , _pCatalog{ nullptr }
        , _pReputation{ nullptr }
        , _pQuestLog{ nullptr }
        , _pClock{ nullptr }
        , _topology{}
        , _land{}
        , _nextHabitatID{ 1 }
        , _lastAttractKey{ constant::invalid_index::kInt32 }
        , _appealTier{ constant::invalid_index::kInt32 }
    {
    }

    bool CreatureTown::initialize( const CreatureLifeCatalog* pCatalog, const SessionStateRefs& refs, int32 width, int32 height, const CreatureTownSettings& settings )
    {
        if ( width < 1 || height < 1 )
        {
            SW_LOG_ERROR_ID( "CreatureTown.CreatureTownRefusedTownSize", "CreatureTown: refused town size %#x%# (needs >= 1 in both)", width, height );
            return false;
        }
        _pCatalog    = pCatalog;
        _pReputation = refs._pReputation;
        _pQuestLog   = refs._pQuestLog;
        _pClock      = refs._pClock;
        _settings    = settings;
        _topology    = GridTopology{ width, height };
        _listObject.assign( static_cast<size_t>( _topology.getCellCount() ), hashed_string{} );
        _listHabitat.clear();
        _listCreature.clear();
        _listHouse.clear();
        _listOpenRequest.clear();
        _eventBuffer.clear();
        _nextHabitatID  = 1;
        _lastAttractKey = constant::invalid_index::kInt32;
        _appealTier     = constant::invalid_index::kInt32;
        updateAppealTier();
        _eventBuffer.clear(); // 처음 단계는 알리지 않는다
        return true;
    }

    bool CreatureTown::bindLand( LandRegistry* pLand, const int2& origin )
    {
        LandBinding land;
        land.bind( pLand, origin, hashed_string( "CreatureLife" ) );
        // 이미 놓인 셀을 먼저 모두 볼 수 있어야 얻는다(반쯤 얻고 실패하지 않게 하려는 것이다).
        for ( int32 index = 0; index < _topology.getCellCount(); ++index )
        {
            const int2 cell = _topology.toCell( index );
            if ( _listObject[static_cast<size_t>( index )].empty() == false && land.isUsable( cell._x, cell._y ) == false )
                return false;
        }
        for ( int32 index = 0; index < _topology.getCellCount(); ++index )
        {
            const hashed_string& object = _listObject[static_cast<size_t>( index )];
            const int2           cell   = _topology.toCell( index );
            if ( object.empty() == false )
                (void)land.claimRect( cell._x, cell._y, cell._x, cell._y, isBlockingObject( object ) );
        }
        _land = land;
        return true;
    }

    bool CreatureTown::setObject( int32 x, int32 y, const hashed_string& object )
    {
        if ( _topology.isInside( x, y ) == false )
            return false;
        hashed_string& tileObject = _listObject[static_cast<size_t>( _topology.toIndex( x, y ) )];
        if ( tileObject == object )
            return true;
        if ( object.empty() )
            _land.releaseRect( x, y, x, y );
        else if ( _land.claimRect( x, y, x, y, isBlockingObject( object ) ) == false )
            return false;
        tileObject = object;
        refreshHabitats();
        return true;
    }

    bool CreatureTown::isBlockingObject( const hashed_string& object ) const
    {
        for ( const hashed_string& blockingObject : _settings._listBlockingObject )
        {
            if ( blockingObject == object )
                return true;
        }
        return false;
    }

    int32 CreatureTown::attractVisitors( const WorldClock& clock, const WeatherSystem& weather )
    {
        return attractVisitorsAt( clock.getDay(), clock.getHourInt(), clock.getDayPhase(), weather.getCurrent() );
    }

    int32 CreatureTown::attractVisitorsAt( int32 day, int32 hour, DayPhase phase, const hashed_string& weatherID )
    {
        if ( _pCatalog == nullptr )
            return 0;
        const int32 attractKey = day * 24 + hour;
        if ( attractKey == _lastAttractKey )
            return 0; // 같은 시를 두 번 굴리지 않는다
        _lastAttractKey = attractKey;

        int32                             arrivedCount = 0;
        const vector<CreatureSpeciesDef>& listSpecies  = _pCatalog->getSpecies();
        for ( const HabitatInstance& instance : _listHabitat )
        {
            for ( int32 speciesIndex = 0; speciesIndex < static_cast<int32>( listSpecies.size() ) && hasRoom( instance ); ++speciesIndex )
            {
                const CreatureSpeciesDef& species      = listSpecies[static_cast<size_t>( speciesIndex )];
                const bool                bSkipSpecies = findCreatureIndex( species._id ).has_value() || species.likesHabitat( instance._habitatID ) == false || species.comesIn( phase, weatherID ) == false;
                if ( bSkipSpecies )
                    continue;
                // 서식지 번호가 아니라 위치로 섞는다. 같은 배치면 지어 온 순서와 상관없이 같은 답이 나온다.
                const uint32  placeKey = static_cast<uint32>( _topology.toIndex( instance._origin ) ) * 4u + static_cast<uint32>( instance._rotation );
                const uint32  salt     = static_cast<uint32>( speciesIndex ) * 977u + placeKey * 31u;
                const float32 roll     = GameHash::toUnitFloat( GameHash::hashCoord( attractKey, static_cast<int32>( salt ), _settings._randomSeed ) );
                if ( roll >= species._chance && species._chance < 1.0f )
                    continue;
                TownCreature creature;
                creature._speciesID  = species._id;
                creature._habitat    = instance._id;
                creature._arrivalDay = getDay();
                creature._listAbilityUse.assign( species._listAbility.size(), 0 );
                _listCreature.push_back( creature );
                _eventBuffer.push( CreatureTownEvent{ species._id, {}, instance._id, CreatureTownEvent::Kind::CreatureArrived } );
                if ( _pReputation != nullptr )
                    (void)_pReputation->changeValue( CreatureTownInternal::makeFactionID( species._id ), 0 ); // 세력 항목을 만들어 둔다(시작값)
                ++arrivedCount;
            }
        }
        if ( arrivedCount > 0 )
            updateAppealTier();
        return arrivedCount;
    }

    CreatureInteractResult CreatureTown::talkTo( const hashed_string& speciesID )
    {
        const optional<int32> foundCreature = findCreatureIndex( speciesID );
        if ( foundCreature.has_value() == false )
            return CreatureInteractResult::UnknownCreature;
        const int32   creatureIndex = *foundCreature;
        TownCreature& creature      = _listCreature[static_cast<size_t>( creatureIndex )];
        if ( creature._lastTalkDay == getDay() )
            return CreatureInteractResult::AlreadyToday;
        creature._lastTalkDay = getDay();
        changeFriendship( speciesID, _settings._talkPoints );
        return CreatureInteractResult::Ok;
    }

    CreatureInteractResult CreatureTown::giveGift( const hashed_string& speciesID, const hashed_string& itemID, Inventory& inventory )
    {
        const optional<int32> foundCreature = findCreatureIndex( speciesID );
        if ( foundCreature.has_value() == false )
            return CreatureInteractResult::UnknownCreature;
        const int32   creatureIndex = *foundCreature;
        TownCreature& creature      = _listCreature[static_cast<size_t>( creatureIndex )];
        if ( creature._lastGiftDay == getDay() )
            return CreatureInteractResult::AlreadyToday;
        if ( inventory.removeItem( itemID, 1 ) == false )
            return CreatureInteractResult::MissingItem;
        creature._lastGiftDay              = getDay();
        const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( speciesID );
        int32                     points   = _settings._giftPoints;
        if ( pSpecies != nullptr && pSpecies->likesGift( itemID ) )
            points = _settings._likedGiftPoints;
        else if ( pSpecies != nullptr && pSpecies->likesFood( itemID ) )
            points = _settings._foodPoints;
        changeFriendship( speciesID, points );
        return CreatureInteractResult::Ok;
    }

    CreatureRequestResult CreatureTown::startRequest( const hashed_string& speciesID, const hashed_string& questID )
    {
        if ( findCreatureIndex( speciesID ).has_value() == false )
            return CreatureRequestResult::UnknownCreature;
        const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( speciesID );
        if ( pSpecies == nullptr || pSpecies->offersRequest( questID ) == false )
            return CreatureRequestResult::NotOffered;
        if ( _pQuestLog == nullptr )
            return CreatureRequestResult::Unavailable;
        const QuestStartResult startResult = _pQuestLog->start( questID, MathUtil::max( 0, _pReputation != nullptr ? _pReputation->getTierIndex( CreatureTownInternal::makeFactionID( speciesID ) ) : 0 ) );
        if ( startResult == QuestStartResult::AlreadyActive )
            return CreatureRequestResult::AlreadyActive;
        if ( startResult != QuestStartResult::Ok )
            return CreatureRequestResult::Unavailable;
        _listOpenRequest.push_back( questID );
        notifyHabitatObjectives(); // 이미 있는 서식지도 센다
        collectCompletedRequests();
        return CreatureRequestResult::Ok;
    }

    int32 CreatureTown::deliverItem( const hashed_string& itemID, int32 count, Inventory& inventory )
    {
        const bool bCanDeliver = 0 < count && _pQuestLog != nullptr && inventory.hasItem( itemID, count );
        if ( bCanDeliver == false )
            return 0;
        const int32 progressedCount = _pQuestLog->notify( CreatureTownInternal::makeDeliverKind(), itemID, count );
        if ( progressedCount > 0 )
        {
            if ( inventory.removeItem( itemID, count ) == false )
                SW_LOG_WARNING_ID( "CreatureTown.InventoryLostBetweenHasItem", "inventory lost '%#' between hasItem and removeItem", itemID.c_str() );
        }
        collectCompletedRequests();
        return progressedCount;
    }

    CreatureAbilityResult CreatureTown::useAbility( const hashed_string& speciesID, const hashed_string& abilityID, int32 x, int32 y, Inventory* pYieldInventory )
    {
        const optional<int32> foundCreature = findCreatureIndex( speciesID );
        if ( foundCreature.has_value() == false )
            return CreatureAbilityResult::UnknownCreature;
        const int32               creatureIndex = *foundCreature;
        const CreatureSpeciesDef* pSpecies      = _pCatalog->findSpecies( speciesID );
        const CreatureAbilityDef* pAbility      = _pCatalog->findAbility( abilityID );
        if ( pSpecies == nullptr || pAbility == nullptr )
            return CreatureAbilityResult::NotKnown;
        int32 abilitySlot = constant::invalid_index::kInt32;
        for ( int32 slot = 0; slot < static_cast<int32>( pSpecies->_listAbility.size() ); ++slot )
        {
            if ( pSpecies->_listAbility[static_cast<size_t>( slot )] == abilityID )
            {
                abilitySlot = slot;
                break;
            }
        }
        if ( abilitySlot < 0 )
            return CreatureAbilityResult::NotKnown;
        TownCreature& creature = _listCreature[static_cast<size_t>( creatureIndex )];
        if ( creature._listAbilityUse.size() < pSpecies->_listAbility.size() )
            creature._listAbilityUse.resize( pSpecies->_listAbility.size(), 0 );
        int32& usedCount = creature._listAbilityUse[static_cast<size_t>( abilitySlot )];
        if ( usedCount >= pAbility->_usesPerDay )
            return CreatureAbilityResult::NoUsesLeft;
        if ( _topology.isInside( x, y ) == false )
            return CreatureAbilityResult::OutOfBounds;
        const int32             tileIndex = _topology.toIndex( x, y );
        const CreatureTileRule* pRule     = pAbility->findRule( _listObject[static_cast<size_t>( tileIndex )] );
        if ( pRule == nullptr )
            return CreatureAbilityResult::NoRule;
        if ( _land.isUsable( x, y ) == false )
            return CreatureAbilityResult::LandTaken;
        ++usedCount;
        if ( pRule->_yieldItem.empty() == false && pYieldInventory != nullptr )
            (void)pYieldInventory->addItem( pRule->_yieldItem, pRule->_yieldCount );
        _eventBuffer.push( CreatureTownEvent{ speciesID, pRule->_yieldItem, tileIndex, CreatureTownEvent::Kind::AbilityUsed } );
        (void)setObject( x, y, pRule->_to );
        return CreatureAbilityResult::Ok;
    }

    int32 CreatureTown::placeHouse( int32 x, int32 y, int32 capacity )
    {
        const bool bCannotPlaceHouse = _topology.isInside( x, y ) == false || _listObject[static_cast<size_t>( _topology.toIndex( x, y ) )].empty() == false || _land.isUsable( x, y ) == false;
        if ( bCannotPlaceHouse )
            return constant::invalid_index::kInt32;
        CreatureHouse house;
        house._tile     = int2{ x, y };
        house._capacity = MathUtil::max( 1, capacity );
        _listHouse.push_back( house );
        (void)setObject( x, y, _settings._houseObject );
        return static_cast<int32>( _listHouse.size() ) - 1;
    }

    CreatureHouseResult CreatureTown::assignHouse( const hashed_string& speciesID, int32 houseIndex )
    {
        const optional<int32> foundCreature = findCreatureIndex( speciesID );
        if ( foundCreature.has_value() == false )
            return CreatureHouseResult::UnknownCreature;
        const int32 creatureIndex = *foundCreature;
        if ( houseIndex < 0 || houseIndex >= static_cast<int32>( _listHouse.size() ) )
            return CreatureHouseResult::UnknownHouse;
        TownCreature&  creature = _listCreature[static_cast<size_t>( creatureIndex )];
        CreatureHouse& house    = _listHouse[static_cast<size_t>( houseIndex )];
        if ( creature._house == houseIndex )
            return CreatureHouseResult::Ok;
        if ( static_cast<int32>( house._listResident.size() ) >= house._capacity )
            return CreatureHouseResult::HouseFull;
        if ( creature._house >= 0 )
        {
            vector<hashed_string>& listOldResident = _listHouse[static_cast<size_t>( creature._house )]._listResident;
            VectorUtil::removeValue( listOldResident, speciesID );
        }
        house._listResident.push_back( speciesID );
        creature._house = houseIndex;
        return CreatureHouseResult::Ok;
    }

    int32 CreatureTown::assignHomeless()
    {
        int32 assignedCount = 0;
        for ( TownCreature& creature : _listCreature )
        {
            if ( creature._house >= 0 )
                continue;
            const int2 anchor       = computeCreatureAnchor( creature );
            int32      bestHouse    = constant::invalid_index::kInt32;
            int32      bestDistance = 0;
            for ( int32 houseIndex = 0; houseIndex < static_cast<int32>( _listHouse.size() ); ++houseIndex )
            {
                const CreatureHouse& house = _listHouse[static_cast<size_t>( houseIndex )];
                if ( static_cast<int32>( house._listResident.size() ) >= house._capacity )
                    continue;
                const int32 distance = MathUtil::abs( house._tile._x - anchor._x ) + MathUtil::abs( house._tile._y - anchor._y );
                if ( bestHouse < 0 || distance < bestDistance )
                {
                    bestHouse    = houseIndex;
                    bestDistance = distance;
                }
            }
            if ( bestHouse >= 0 && assignHouse( creature._speciesID, bestHouse ) == CreatureHouseResult::Ok )
                ++assignedCount;
        }
        return assignedCount;
    }

    void CreatureTown::advanceDay()
    {
        for ( TownCreature& creature : _listCreature )
        {
            std::fill( creature._listAbilityUse.begin(), creature._listAbilityUse.end(), 0 );
        }
        updateAppealTier(); // 공유 평판의 감쇠는 주인이 날이 넘어갈 때 이미 처리했으므로, 매력도만 다시 본다
    }

    void CreatureTown::pollEvents( vector<CreatureTownEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    const hashed_string* CreatureTown::findObject( int32 x, int32 y ) const
    {
        if ( _topology.isInside( x, y ) == false )
            return nullptr;
        return &_listObject[static_cast<size_t>( _topology.toIndex( x, y ) )];
    }

    const TownCreature* CreatureTown::findCreature( const hashed_string& speciesID ) const
    {
        const optional<int32> creatureIndex = findCreatureIndex( speciesID );
        return creatureIndex.has_value() ? &_listCreature[static_cast<size_t>( *creatureIndex )] : nullptr;
    }

    const HabitatInstance* CreatureTown::findHabitat( int32 habitatInstanceID ) const
    {
        return VectorUtil::findBy<&HabitatInstance::_id>( _listHabitat, habitatInstanceID );
    }

    int32 CreatureTown::countHabitats( const hashed_string& habitatID ) const
    {
        int32 count = 0;
        for ( const HabitatInstance& instance : _listHabitat )
        {
            if ( instance._habitatID == habitatID )
                ++count;
        }
        return count;
    }

    int32 CreatureTown::countResidents( int32 habitatInstanceID ) const
    {
        int32 count = 0;
        for ( const TownCreature& creature : _listCreature )
        {
            if ( creature._habitat == habitatInstanceID )
                ++count;
        }
        return count;
    }

    int32 CreatureTown::countAbilityUsesLeft( const hashed_string& speciesID, const hashed_string& abilityID ) const
    {
        const TownCreature*       pCreature           = findCreature( speciesID );
        const CreatureSpeciesDef* pSpecies            = _pCatalog != nullptr ? _pCatalog->findSpecies( speciesID ) : nullptr;
        const CreatureAbilityDef* pAbility            = _pCatalog != nullptr ? _pCatalog->findAbility( abilityID ) : nullptr;
        const bool                bAbilityDataMissing = pCreature == nullptr || pSpecies == nullptr || pAbility == nullptr;
        if ( bAbilityDataMissing )
            return 0;
        for ( size_t slot = 0; slot < pSpecies->_listAbility.size(); ++slot )
        {
            if ( pSpecies->_listAbility[slot] == abilityID )
            {
                const int32 usedCount = slot < pCreature->_listAbilityUse.size() ? pCreature->_listAbilityUse[slot] : 0;
                return MathUtil::max( 0, pAbility->_usesPerDay - usedCount );
            }
        }
        return 0;
    }

    float32 CreatureTown::computeAverageFriendship() const
    {
        if ( _listCreature.empty() )
            return 0.0f;
        int64 total = 0;
        for ( const TownCreature& creature : _listCreature )
        {
            total += getFriendship( creature._speciesID );
        }
        return static_cast<float32>( total ) / static_cast<float32>( _listCreature.size() );
    }

    float32 CreatureTown::computeAppealScore() const
    {
        if ( _pCatalog == nullptr )
            return 0.0f;
        int32 diversity = 0;
        for ( const HabitatDef& habitat : _pCatalog->getHabitats() )
        {
            if ( countHabitats( habitat._id ) > 0 )
                ++diversity;
        }
        const TownAppealDef& appeal = _pCatalog->getAppeal();
        return static_cast<float32>( diversity ) * appeal._diversityWeight + static_cast<float32>( _listCreature.size() ) * appeal._creatureWeight +
               computeAverageFriendship() * appeal._friendshipWeight;
    }

    hashed_string CreatureTown::getAppealTierName() const
    {
        if ( _pCatalog == nullptr || _appealTier < 0 )
            return {};
        return _pCatalog->getAppeal()._listTier[static_cast<size_t>( _appealTier )]._name;
    }

    void CreatureTown::refreshHabitats()
    {
        if ( _pCatalog == nullptr )
            return;
        vector<uint8>           listClaimed( _listObject.size(), SW_FALSE );
        vector<HabitatInstance> listMatch;
        vector<int32>           listTile;
        for ( const int32 habitatIndex : _pCatalog->getHabitatMatchOrder() )
        {
            const HabitatDef& habitat = _pCatalog->getHabitats()[static_cast<size_t>( habitatIndex )];
            for ( int32 originY = 0; originY < _topology._height; ++originY )
            {
                for ( int32 originX = 0; originX < _topology._width; ++originX )
                {
                    for ( int32 rotation = 0; rotation < CreatureTownInternal::kRotationCount; ++rotation )
                    {
                        if ( matchesAt( habitat, rotation, originX, originY, listClaimed, listTile ) == false )
                            continue;
                        for ( const int32 tileIndex : listTile )
                        {
                            listClaimed[static_cast<size_t>( tileIndex )] = SW_TRUE;
                        }
                        HabitatInstance instance;
                        instance._habitatID = habitat._id;
                        instance._listTile  = listTile;
                        instance._origin    = int2{ originX, originY };
                        instance._rotation  = rotation;
                        instance._id        = constant::invalid_index::kInt32;
                        listMatch.push_back( instance );
                        break; // 이 서식지의 셀은 이제 차지됐다
                    }
                }
            }
        }

        // 전과 같은 서식지는 번호를 이어받는다.
        vector<uint8> listKept( _listHabitat.size(), SW_FALSE );
        for ( HabitatInstance& match : listMatch )
        {
            for ( size_t oldIndex = 0; oldIndex < _listHabitat.size(); ++oldIndex )
            {
                const HabitatInstance& old                = _listHabitat[oldIndex];
                const bool             bMatchesOldHabitat = listKept[oldIndex] == SW_FALSE && old._habitatID == match._habitatID && old._rotation == match._rotation && old._origin._x == match._origin._x &&
                                                old._origin._y == match._origin._y;
                if ( bMatchesOldHabitat )
                {
                    match._id          = old._id;
                    listKept[oldIndex] = SW_TRUE;
                    break;
                }
            }
        }
        for ( size_t oldIndex = 0; oldIndex < _listHabitat.size(); ++oldIndex )
        {
            if ( listKept[oldIndex] != SW_FALSE )
                continue;
            const HabitatInstance& old = _listHabitat[oldIndex];
            _eventBuffer.push( CreatureTownEvent{ old._habitatID, {}, old._id, CreatureTownEvent::Kind::HabitatLost } );
            for ( TownCreature& creature : _listCreature )
            {
                if ( creature._habitat == old._id )
                    creature._habitat = constant::invalid_index::kInt32;
            }
        }
        for ( HabitatInstance& match : listMatch )
        {
            if ( match._id >= 0 )
                continue;
            match._id = _nextHabitatID++;
            _eventBuffer.push( CreatureTownEvent{ match._habitatID, {}, match._id, CreatureTownEvent::Kind::HabitatFormed } );
        }
        _listHabitat = std::move( listMatch );

        // 서식지를 잃은 생물은 좋아하는 서식지의 빈자리로 옮긴다.
        for ( TownCreature& creature : _listCreature )
        {
            if ( creature._habitat >= 0 )
                continue;
            const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( creature._speciesID );
            for ( const HabitatInstance& instance : _listHabitat )
            {
                const bool bHabitatHasRoom = pSpecies != nullptr && pSpecies->likesHabitat( instance._habitatID ) && hasRoom( instance );
                if ( bHabitatHasRoom )
                {
                    creature._habitat = instance._id;
                    break;
                }
            }
        }
        notifyHabitatObjectives();
        collectCompletedRequests();
        updateAppealTier();
    }

    bool CreatureTown::matchesAt( const HabitatDef& habitat, int32 rotation, int32 originX, int32 originY, const vector<uint8>& listClaimed,
                                  vector<int32>& outListTile ) const
    {
        outListTile.clear();
        const int2 size = CreatureTownInternal::computeRotatedSize( habitat, rotation );
        if ( _topology.isRectInside( int2{ originX, originY }, size ) == false )
            return false;
        for ( int32 rotatedY = 0; rotatedY < size._y; ++rotatedY )
        {
            for ( int32 rotatedX = 0; rotatedX < size._x; ++rotatedX )
            {
                const HabitatCell& cell = CreatureTownInternal::sampleRotated( habitat, rotation, rotatedX, rotatedY );
                if ( cell._bAny != SW_FALSE )
                    continue;
                const int32 tileIndex = _topology.toIndex( originX + rotatedX, originY + rotatedY );
                if ( listClaimed[static_cast<size_t>( tileIndex )] != SW_FALSE || ( _listObject[static_cast<size_t>( tileIndex )] == cell._object ) == false )
                    return false;
                outListTile.push_back( tileIndex );
            }
        }
        return outListTile.empty() == false;
    }

    optional<int32> CreatureTown::findCreatureIndex( const hashed_string& speciesID ) const
    {
        for ( int32 creatureIndex = 0; creatureIndex < static_cast<int32>( _listCreature.size() ); ++creatureIndex )
        {
            if ( _listCreature[static_cast<size_t>( creatureIndex )]._speciesID == speciesID )
                return creatureIndex;
        }
        return nullopt;
    }

    bool CreatureTown::hasRoom( const HabitatInstance& instance ) const
    {
        const HabitatDef* pHabitat = _pCatalog->findHabitat( instance._habitatID );
        return pHabitat != nullptr && countResidents( instance._id ) < pHabitat->_capacity;
    }

    void CreatureTown::notifyHabitatObjectives()
    {
        if ( _pQuestLog == nullptr )
            return;
        const hashed_string habitatKind = CreatureTownInternal::makeHabitatKind();
        for ( const HabitatDef& habitat : _pCatalog->getHabitats() )
        {
            (void)_pQuestLog->notifyCount( habitatKind, habitat._id, countHabitats( habitat._id ) );
        }
    }

    void CreatureTown::changeFriendship( const hashed_string& speciesID, int32 delta )
    {
        if ( _pReputation == nullptr )
            return;
        const hashed_string factionID  = CreatureTownInternal::makeFactionID( speciesID );
        const int32         tierBefore = _pReputation->getTierIndex( factionID );
        (void)_pReputation->changeValue( factionID, delta );
        if ( _pReputation->getTierIndex( factionID ) != tierBefore )
            _eventBuffer.push( CreatureTownEvent{ speciesID, _pReputation->getTierName( factionID ), _pReputation->getValue( factionID ), CreatureTownEvent::Kind::FriendshipTierChanged } );
        updateAppealTier();
    }

    void CreatureTown::collectCompletedRequests()
    {
        if ( _pQuestLog == nullptr || _listOpenRequest.empty() )
            return;
        size_t keptCount = 0;
        for ( size_t requestIndex = 0; requestIndex < _listOpenRequest.size(); ++requestIndex )
        {
            const hashed_string questID = _listOpenRequest[requestIndex];
            const QuestStatus   status  = _pQuestLog->getStatus( questID );
            if ( status == QuestStatus::Active )
            {
                _listOpenRequest[keptCount] = questID;
                ++keptCount;
                continue;
            }
            // 끝났다. 완료이면 그 부탁을 하는 생물에게 호감도를 주고, 실패나 포기(NotStarted)는 보상 없이 뺀다.
            if ( status == QuestStatus::Completed )
                (void)rewardRequest( questID );
        }
        _listOpenRequest.resize( keptCount );
    }

    bool CreatureTown::rewardRequest( const hashed_string& questID )
    {
        for ( const TownCreature& creature : _listCreature )
        {
            const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( creature._speciesID );
            if ( pSpecies == nullptr || pSpecies->offersRequest( questID ) == false )
                continue;
            _eventBuffer.push( CreatureTownEvent{ creature._speciesID, questID, 0, CreatureTownEvent::Kind::RequestCompleted } );
            changeFriendship( creature._speciesID, _settings._requestPoints );
            return true;
        }
        return false;
    }

    void CreatureTown::updateAppealTier()
    {
        if ( _pCatalog == nullptr )
            return;
        const vector<TownAppealTier>& listTier = _pCatalog->getAppeal()._listTier;
        const float32                 score    = computeAppealScore();
        int32                         tier     = constant::invalid_index::kInt32;
        for ( int32 tierIndex = 0; tierIndex < static_cast<int32>( listTier.size() ); ++tierIndex )
        {
            if ( score >= listTier[static_cast<size_t>( tierIndex )]._minScore )
                tier = tierIndex;
        }
        if ( tier == _appealTier )
            return;
        _appealTier = tier;
        _eventBuffer.push( CreatureTownEvent{ {}, getAppealTierName(), tier, CreatureTownEvent::Kind::AppealTierChanged } );
    }

    int2 CreatureTown::computeCreatureAnchor( const TownCreature& creature ) const
    {
        const HabitatInstance* pHabitat = findHabitat( creature._habitat );
        if ( pHabitat == nullptr || pHabitat->_listTile.empty() )
            return int2{ 0, 0 };
        return _topology.toCell( pHabitat->_listTile.front() );
    }

    void CreatureTown::serializeState( StateIO& io )
    {
        io.expect( _topology._width );
        io.expect( _topology._height );
        if ( io.isOk() == false )
            return;
        const int32 tileCount = _topology._width * _topology._height;
        for ( hashed_string& object : _listObject )
        {
            io.value( object );
        }
        // 서식지마다 이름(4) + 위치(8) + 번호(4) + 회전(4) + 셀 수(4) 이상
        io.list( _listHabitat, 24, [&]( HabitatInstance& habitat )
        {
            io.value( habitat._habitatID );
            io.value( habitat._origin );
            io.value( habitat._id );
            io.value( habitat._rotation );
            io.list( habitat._listTile, 4, [&]( int32& tileIndex )
            {
                io.value( tileIndex );
                io.check( 0 <= tileIndex && tileIndex < tileCount );
            } );
            // 서식지는 적어도 한 셀을 차지하고 번호는 1 부터이다(`matchesAt`, `_nextHabitatID`). 회전은 해시와 맞춤에 그대로 쓴다.
            io.check( habitat._listTile.empty() == false && 0 < habitat._id && 0 <= habitat._rotation && habitat._rotation < CreatureTownInternal::kRotationCount );
        } );
        // 생물마다 이름(4) + 능력 수(4) + 서식지, 집, 대화 날, 선물 날, 온 날(20) 이상
        io.list( _listCreature, 28, [&]( TownCreature& creature )
        {
            io.value( creature._speciesID );
            io.list( creature._listAbilityUse );
            io.value( creature._habitat );
            io.value( creature._house );
            io.value( creature._lastTalkDay );
            io.value( creature._lastGiftDay );
            io.value( creature._arrivalDay );
            // 음수 사용 횟수는 하루 횟수를 늘린다. 없는 날은 -1 이다.
            for ( const int32 useCount : creature._listAbilityUse )
            {
                io.check( 0 <= useCount );
            }
            io.check( constant::invalid_index::kInt32 <= creature._habitat && constant::invalid_index::kInt32 <= creature._lastTalkDay && constant::invalid_index::kInt32 <= creature._lastGiftDay && 0 <= creature._arrivalDay );
        } );
        // 집마다 사는 수(4) + 셀(8) + 정원(4) 이상
        io.list( _listHouse, 16, [&]( CreatureHouse& house )
        {
            io.list( house._listResident );
            io.value( house._tile );
            io.value( house._capacity );
            io.check( 1 <= house._capacity && _topology.isInside( house._tile._x, house._tile._y ) );
        } );
        io.list( _listOpenRequest );
        io.value( _nextHabitatID );
        io.value( _lastAttractKey );
        io.check( 0 < _nextHabitatID && -1 <= _lastAttractKey );
        if ( io.isWriting() || io.isOk() == false )
            return;
        io.check( areLinksValid() );
        _eventBuffer.clear();
        updateAppealTier();
        _eventBuffer.clear(); // 복원한 단계는 알리지 않는다
    }

    bool CreatureTown::areLinksValid() const
    {
        if ( _pCatalog == nullptr && ( _listHabitat.empty() == false || _listCreature.empty() == false ) )
            return false; // 카탈로그 없이는 서식지도 생물도 생기지 않는다(`refreshHabitats`, `attractVisitorsAt`)

        // 서식지: 카탈로그에 있고, 번호가 겹치지 않고, 셀은 한 서식지에만 속한다.
        vector<uint8> listClaimed( _listObject.size(), SW_FALSE );
        for ( const HabitatInstance& habitat : _listHabitat )
        {
            const bool bKnownPlace = _pCatalog->findHabitat( habitat._habitatID ) != nullptr && _topology.isInside( habitat._origin._x, habitat._origin._y );
            if ( bKnownPlace == false || habitat._id >= _nextHabitatID )
                return false;
            for ( const int32 tileIndex : habitat._listTile )
            {
                uint8& bClaimed = listClaimed[static_cast<size_t>( tileIndex )];
                if ( bClaimed != SW_FALSE )
                    return false;
                bClaimed = SW_TRUE;
            }
        }
        if ( VectorUtil::findDuplicateBy<&HabitatInstance::_id>( _listHabitat ) != nullptr )
            return false;

        // 생물: 종마다 하나이고(찾기는 앞의 것만 돌려준다), 서식지와 집은 실제로 있으며, 집은 그 생물을 입주자로 적었다.
        for ( const TownCreature& creature : _listCreature )
        {
            if ( _pCatalog->findSpecies( creature._speciesID ) == nullptr )
                return false;
        }
        if ( VectorUtil::findDuplicateBy<&TownCreature::_speciesID>( _listCreature ) != nullptr )
            return false;
        const int32 houseCount = static_cast<int32>( _listHouse.size() );
        for ( const TownCreature& creature : _listCreature )
        {
            const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( creature._speciesID );
            if ( creature._listAbilityUse.size() > pSpecies->_listAbility.size() )
                return false;
            for ( size_t slot = 0; slot < creature._listAbilityUse.size(); ++slot )
            {
                const CreatureAbilityDef* pAbility = _pCatalog->findAbility( pSpecies->_listAbility[slot] );
                if ( pAbility != nullptr && creature._listAbilityUse[slot] > pAbility->_usesPerDay )
                    return false;
            }
            const bool bHabitatValid = creature._habitat < 0 || findHabitat( creature._habitat ) != nullptr;
            const bool bHouseValid   = constant::invalid_index::kInt32 <= creature._house && creature._house < houseCount;
            if ( bHabitatValid == false || bHouseValid == false )
                return false;
            if ( creature._house >= 0 && VectorUtil::contains( _listHouse[static_cast<size_t>( creature._house )]._listResident, creature._speciesID ) == false )
                return false;
        }
        for ( const HabitatInstance& habitat : _listHabitat )
        {
            if ( countResidents( habitat._id ) > _pCatalog->findHabitat( habitat._habitatID )->_capacity )
                return false;
        }

        // 집: 정원 안이고, 입주자는 겹치지 않으며 이 집을 자기 집으로 적은 생물이다.
        for ( int32 houseIndex = 0; houseIndex < houseCount; ++houseIndex )
        {
            const CreatureHouse& house = _listHouse[static_cast<size_t>( houseIndex )];
            if ( static_cast<int32>( house._listResident.size() ) > house._capacity || VectorUtil::findDuplicate( house._listResident ) != nullptr )
                return false;
            for ( const hashed_string& resident : house._listResident )
            {
                const TownCreature* pCreature = findCreature( resident );
                if ( pCreature == nullptr || pCreature->_house != houseIndex )
                    return false;
            }
        }

        for ( const hashed_string& questID : _listOpenRequest )
        {
            if ( questID.empty() )
                return false;
        }
        return VectorUtil::findDuplicate( _listOpenRequest ) == nullptr;
    }

    int32 CreatureTown::getDay() const
    {
        return _pClock != nullptr ? _pClock->getDay() : 0;
    }

    int32 CreatureTown::getFriendship( const hashed_string& speciesID ) const
    {
        return _pReputation != nullptr ? _pReputation->getValue( CreatureTownInternal::makeFactionID( speciesID ) ) : 0;
    }

    hashed_string CreatureTown::getFriendshipTier( const hashed_string& speciesID ) const
    {
        return _pReputation != nullptr ? _pReputation->getTierName( CreatureTownInternal::makeFactionID( speciesID ) ) : hashed_string{};
    }
} // namespace sw
