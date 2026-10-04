#include "pch.h"

#include "GameFramework/Kits/Simulation/CreatureLife/CreatureTown.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Inventory/Inventory.h"
#include "GameFramework/Utility/GameRandom.h"
#include "GameFramework/World/WeatherSystem.h"

namespace sw
{
    SW_LOG_CALLER( "CreatureTown" );

    namespace
    {
        struct CreatureTownInternal
        {
            static constexpr int32 kRotationCount = 4;

            /** @brief 회전한 패턴의 가로 · 세로입니다(홀수 회전은 뒤바뀐다). */
            static int2 computeRotatedSize( const HabitatDef& habitat, int32 rotation )
            {
                return ( rotation & 1 ) != 0 ? int2{ habitat._height, habitat._width } : int2{ habitat._width, habitat._height };
            }

            /** @brief 회전한 패턴의 (@p rotatedX, @p rotatedY) 칸이 원래 패턴의 어느 칸인지입니다. 90° 를 @p rotation 번 돌린 것입니다. */
            static const HabitatCell& sampleRotated( const HabitatDef& habitat, int32 rotation, int32 rotatedX, int32 rotatedY )
            {
                const int32 width  = habitat._width;
                const int32 height = habitat._height;
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
    const utf8* toString( CreatureInteractResult result )
    {
        switch ( result )
        {
            case CreatureInteractResult::Ok:
                return "Ok";
            case CreatureInteractResult::UnknownCreature:
                return "UnknownCreature";
            case CreatureInteractResult::AlreadyToday:
                return "AlreadyToday";
            case CreatureInteractResult::MissingItem:
                return "MissingItem";
        }
        return "Unknown";
    }

    const utf8* toString( CreatureRequestResult result )
    {
        switch ( result )
        {
            case CreatureRequestResult::Ok:
                return "Ok";
            case CreatureRequestResult::UnknownCreature:
                return "UnknownCreature";
            case CreatureRequestResult::NotOffered:
                return "NotOffered";
            case CreatureRequestResult::AlreadyActive:
                return "AlreadyActive";
            case CreatureRequestResult::Unavailable:
                return "Unavailable";
        }
        return "Unknown";
    }

    const utf8* toString( CreatureAbilityResult result )
    {
        switch ( result )
        {
            case CreatureAbilityResult::Ok:
                return "Ok";
            case CreatureAbilityResult::UnknownCreature:
                return "UnknownCreature";
            case CreatureAbilityResult::NotKnown:
                return "NotKnown";
            case CreatureAbilityResult::NoUsesLeft:
                return "NoUsesLeft";
            case CreatureAbilityResult::OutOfBounds:
                return "OutOfBounds";
            case CreatureAbilityResult::NoRule:
                return "NoRule";
        }
        return "Unknown";
    }

    const utf8* toString( CreatureHouseResult result )
    {
        switch ( result )
        {
            case CreatureHouseResult::Ok:
                return "Ok";
            case CreatureHouseResult::UnknownCreature:
                return "UnknownCreature";
            case CreatureHouseResult::UnknownHouse:
                return "UnknownHouse";
            case CreatureHouseResult::HouseFull:
                return "HouseFull";
        }
        return "Unknown";
    }

    CreatureTown::CreatureTown()
        : _listObject{}
        , _listHabitat{}
        , _listCreature{}
        , _listHouse{}
        , _eventBuffer{}
        , _listReputationScratch{}
        , _listQuestScratch{}
        , _friendship{}
        , _questLog{}
        , _settings{}
        , _pCatalog{ nullptr }
        , _width{ 0 }
        , _height{ 0 }
        , _day{ 0 }
        , _nextHabitatId{ 1 }
        , _lastAttractKey{ -1 }
        , _appealTier{ -1 }
    {
    }

    void CreatureTown::initialize( const CreatureLifeCatalog* pCatalog, const ReputationCatalog* pReputationCatalog, const QuestCatalog* pQuestCatalog,
                                   int32 width, int32 height, const CreatureTownSettings& settings )
    {
        _pCatalog = pCatalog;
        _settings = settings;
        _width    = MathUtil::max( 1, width );
        _height   = MathUtil::max( 1, height );
        _listObject.assign( static_cast<size_t>( _width * _height ), hashed_string{} );
        _listHabitat.clear();
        _listCreature.clear();
        _listHouse.clear();
        _eventBuffer.clear();
        _friendship.initialize( pReputationCatalog );
        _questLog.initialize( pQuestCatalog );
        _day            = 0;
        _nextHabitatId  = 1;
        _lastAttractKey = -1;
        _appealTier     = -1;
        updateAppealTier();
        _eventBuffer.clear(); // 처음 단계는 알리지 않는다
    }

    bool CreatureTown::setObject( int32 x, int32 y, const hashed_string& object )
    {
        if ( x < 0 || y < 0 || x >= _width || y >= _height )
            return false;
        hashed_string& tileObject = _listObject[static_cast<size_t>( y * _width + x )];
        if ( tileObject == object )
            return true;
        tileObject = object;
        refreshHabitats();
        return true;
    }

    int32 CreatureTown::attractVisitors( const WorldClock& clock, const WeatherSystem& weather )
    {
        return attractVisitorsAt( clock.getDay(), clock.getHourInt(), clock.getDayPhase(), weather.getCurrent() );
    }

    int32 CreatureTown::attractVisitorsAt( int32 day, int32 hour, DayPhase phase, const hashed_string& weatherId )
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
                const CreatureSpeciesDef& species = listSpecies[static_cast<size_t>( speciesIndex )];
                if ( findCreatureIndex( species._id ) >= 0 || species.likesHabitat( instance._habitatId ) == false || species.comesIn( phase, weatherId ) == false )
                    continue;
                // 서식지 번호가 아니라 자리로 섞는다 — 같은 배치면 지어 온 순서와 상관없이 같은 답이다.
                const uint32  placeKey = static_cast<uint32>( instance._origin._y * _width + instance._origin._x ) * 4u + static_cast<uint32>( instance._rotation );
                const uint32  salt     = static_cast<uint32>( speciesIndex ) * 977u + placeKey * 31u;
                const float32 roll     = GameHash::toUnitFloat( GameHash::hashCoord( attractKey, static_cast<int32>( salt ), _settings._randomSeed ) );
                if ( roll >= species._chance && species._chance < 1.0f )
                    continue;
                TownCreature creature;
                creature._speciesId  = species._id;
                creature._habitat    = instance._id;
                creature._arrivalDay = _day;
                creature._listAbilityUse.assign( species._listAbility.size(), 0 );
                _listCreature.push_back( creature );
                _eventBuffer.push( CreatureTownEvent{ species._id, {}, instance._id, CreatureTownEvent::Kind::CreatureArrived } );
                (void)_friendship.changeValue( species._id, 0 ); // 세력 자리를 만들어 둔다(시작값)
                ++arrivedCount;
            }
        }
        if ( arrivedCount > 0 )
            updateAppealTier();
        return arrivedCount;
    }

    CreatureInteractResult CreatureTown::talkTo( const hashed_string& speciesId )
    {
        const int32 creatureIndex = findCreatureIndex( speciesId );
        if ( creatureIndex < 0 )
            return CreatureInteractResult::UnknownCreature;
        TownCreature& creature = _listCreature[static_cast<size_t>( creatureIndex )];
        if ( creature._lastTalkDay == _day )
            return CreatureInteractResult::AlreadyToday;
        creature._lastTalkDay = _day;
        (void)_friendship.changeValue( speciesId, _settings._talkPoints );
        flushReputationEvents();
        return CreatureInteractResult::Ok;
    }

    CreatureInteractResult CreatureTown::giveGift( const hashed_string& speciesId, const hashed_string& itemId, Inventory& inventory )
    {
        const int32 creatureIndex = findCreatureIndex( speciesId );
        if ( creatureIndex < 0 )
            return CreatureInteractResult::UnknownCreature;
        TownCreature& creature = _listCreature[static_cast<size_t>( creatureIndex )];
        if ( creature._lastGiftDay == _day )
            return CreatureInteractResult::AlreadyToday;
        if ( inventory.removeItem( itemId, 1 ) == false )
            return CreatureInteractResult::MissingItem;
        creature._lastGiftDay              = _day;
        const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( speciesId );
        int32                     points   = _settings._giftPoints;
        if ( pSpecies != nullptr && pSpecies->likesGift( itemId ) )
            points = _settings._likedGiftPoints;
        else if ( pSpecies != nullptr && pSpecies->likesFood( itemId ) )
            points = _settings._foodPoints;
        (void)_friendship.changeValue( speciesId, points );
        flushReputationEvents();
        return CreatureInteractResult::Ok;
    }

    CreatureRequestResult CreatureTown::startRequest( const hashed_string& speciesId, const hashed_string& questId )
    {
        if ( findCreatureIndex( speciesId ) < 0 )
            return CreatureRequestResult::UnknownCreature;
        const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( speciesId );
        if ( pSpecies == nullptr || pSpecies->offersRequest( questId ) == false )
            return CreatureRequestResult::NotOffered;
        const QuestStartResult startResult = _questLog.start( questId, MathUtil::max( 0, _friendship.getTierIndex( speciesId ) ) );
        if ( startResult == QuestStartResult::AlreadyActive )
            return CreatureRequestResult::AlreadyActive;
        if ( startResult != QuestStartResult::Ok )
            return CreatureRequestResult::Unavailable;
        notifyHabitatObjectives(); // 이미 있는 서식지도 센다
        flushQuestEvents();
        return CreatureRequestResult::Ok;
    }

    int32 CreatureTown::deliverItem( const hashed_string& itemId, int32 count, Inventory& inventory )
    {
        if ( count <= 0 || inventory.hasItem( itemId, count ) == false )
            return 0;
        const int32 progressedCount = _questLog.notify( CreatureTownInternal::makeDeliverKind(), itemId, count );
        if ( progressedCount > 0 )
        {
            if ( inventory.removeItem( itemId, count ) == false )
                SW_LOG_WARNING( "inventory lost '%#' between hasItem and removeItem", itemId.c_str() );
        }
        flushQuestEvents();
        return progressedCount;
    }

    CreatureAbilityResult CreatureTown::useAbility( const hashed_string& speciesId, const hashed_string& abilityId, int32 x, int32 y, Inventory* pYieldInventory )
    {
        const int32 creatureIndex = findCreatureIndex( speciesId );
        if ( creatureIndex < 0 )
            return CreatureAbilityResult::UnknownCreature;
        const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( speciesId );
        const CreatureAbilityDef* pAbility = _pCatalog->findAbility( abilityId );
        if ( pSpecies == nullptr || pAbility == nullptr )
            return CreatureAbilityResult::NotKnown;
        int32 abilitySlot = -1;
        for ( int32 slot = 0; slot < static_cast<int32>( pSpecies->_listAbility.size() ); ++slot )
        {
            if ( pSpecies->_listAbility[static_cast<size_t>( slot )] == abilityId )
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
        if ( x < 0 || y < 0 || x >= _width || y >= _height )
            return CreatureAbilityResult::OutOfBounds;
        const int32             tileIndex = y * _width + x;
        const CreatureTileRule* pRule     = pAbility->findRule( _listObject[static_cast<size_t>( tileIndex )] );
        if ( pRule == nullptr )
            return CreatureAbilityResult::NoRule;
        ++usedCount;
        if ( pRule->_yieldItem.empty() == false && pYieldInventory != nullptr )
            (void)pYieldInventory->addItem( pRule->_yieldItem, pRule->_yieldCount );
        _eventBuffer.push( CreatureTownEvent{ speciesId, pRule->_yieldItem, tileIndex, CreatureTownEvent::Kind::AbilityUsed } );
        (void)setObject( x, y, pRule->_to );
        return CreatureAbilityResult::Ok;
    }

    int32 CreatureTown::placeHouse( int32 x, int32 y, int32 capacity )
    {
        if ( x < 0 || y < 0 || x >= _width || y >= _height || _listObject[static_cast<size_t>( y * _width + x )].empty() == false )
            return -1;
        CreatureHouse house;
        house._tile     = int2{ x, y };
        house._capacity = MathUtil::max( 1, capacity );
        _listHouse.push_back( house );
        (void)setObject( x, y, _settings._houseObject );
        return static_cast<int32>( _listHouse.size() ) - 1;
    }

    CreatureHouseResult CreatureTown::assignHouse( const hashed_string& speciesId, int32 houseIndex )
    {
        const int32 creatureIndex = findCreatureIndex( speciesId );
        if ( creatureIndex < 0 )
            return CreatureHouseResult::UnknownCreature;
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
            listOldResident.erase( std::remove( listOldResident.begin(), listOldResident.end(), speciesId ), listOldResident.end() );
        }
        house._listResident.push_back( speciesId );
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
            int32      bestHouse    = -1;
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
            if ( bestHouse >= 0 && assignHouse( creature._speciesId, bestHouse ) == CreatureHouseResult::Ok )
                ++assignedCount;
        }
        return assignedCount;
    }

    void CreatureTown::advanceDay()
    {
        ++_day;
        for ( TownCreature& creature : _listCreature )
            std::fill( creature._listAbilityUse.begin(), creature._listAbilityUse.end(), 0 );
        _friendship.advanceDay();
        flushReputationEvents();
    }

    void CreatureTown::drainEvents( vector<CreatureTownEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    const hashed_string* CreatureTown::findObject( int32 x, int32 y ) const
    {
        if ( x < 0 || y < 0 || x >= _width || y >= _height )
            return nullptr;
        return &_listObject[static_cast<size_t>( y * _width + x )];
    }

    const TownCreature* CreatureTown::findCreature( const hashed_string& speciesId ) const
    {
        const int32 creatureIndex = findCreatureIndex( speciesId );
        return creatureIndex >= 0 ? &_listCreature[static_cast<size_t>( creatureIndex )] : nullptr;
    }

    const HabitatInstance* CreatureTown::findHabitat( int32 habitatInstanceId ) const
    {
        for ( const HabitatInstance& instance : _listHabitat )
        {
            if ( instance._id == habitatInstanceId )
                return &instance;
        }
        return nullptr;
    }

    int32 CreatureTown::countHabitats( const hashed_string& habitatId ) const
    {
        int32 count = 0;
        for ( const HabitatInstance& instance : _listHabitat )
        {
            if ( instance._habitatId == habitatId )
                ++count;
        }
        return count;
    }

    int32 CreatureTown::countResidents( int32 habitatInstanceId ) const
    {
        int32 count = 0;
        for ( const TownCreature& creature : _listCreature )
        {
            if ( creature._habitat == habitatInstanceId )
                ++count;
        }
        return count;
    }

    int32 CreatureTown::countAbilityUsesLeft( const hashed_string& speciesId, const hashed_string& abilityId ) const
    {
        const TownCreature*       pCreature = findCreature( speciesId );
        const CreatureSpeciesDef* pSpecies  = _pCatalog != nullptr ? _pCatalog->findSpecies( speciesId ) : nullptr;
        const CreatureAbilityDef* pAbility  = _pCatalog != nullptr ? _pCatalog->findAbility( abilityId ) : nullptr;
        if ( pCreature == nullptr || pSpecies == nullptr || pAbility == nullptr )
            return 0;
        for ( size_t slot = 0; slot < pSpecies->_listAbility.size(); ++slot )
        {
            if ( pSpecies->_listAbility[slot] == abilityId )
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
            total += _friendship.getValue( creature._speciesId );
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
            for ( int32 originY = 0; originY < _height; ++originY )
            {
                for ( int32 originX = 0; originX < _width; ++originX )
                {
                    for ( int32 rotation = 0; rotation < CreatureTownInternal::kRotationCount; ++rotation )
                    {
                        if ( matchesAt( habitat, rotation, originX, originY, listClaimed, listTile ) == false )
                            continue;
                        for ( const int32 tileIndex : listTile )
                            listClaimed[static_cast<size_t>( tileIndex )] = SW_TRUE;
                        HabitatInstance instance;
                        instance._habitatId = habitat._id;
                        instance._listTile  = listTile;
                        instance._origin    = int2{ originX, originY };
                        instance._rotation  = rotation;
                        instance._id        = -1;
                        listMatch.push_back( instance );
                        break; // 이 자리의 칸은 이제 차지됐다
                    }
                }
            }
        }

        // 전과 같은 서식지는 번호를 잇는다.
        vector<uint8> listKept( _listHabitat.size(), SW_FALSE );
        for ( HabitatInstance& match : listMatch )
        {
            for ( size_t oldIndex = 0; oldIndex < _listHabitat.size(); ++oldIndex )
            {
                const HabitatInstance& old = _listHabitat[oldIndex];
                if ( listKept[oldIndex] == SW_FALSE && old._habitatId == match._habitatId && old._rotation == match._rotation && old._origin._x == match._origin._x &&
                     old._origin._y == match._origin._y )
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
            _eventBuffer.push( CreatureTownEvent{ old._habitatId, {}, old._id, CreatureTownEvent::Kind::HabitatLost } );
            for ( TownCreature& creature : _listCreature )
            {
                if ( creature._habitat == old._id )
                    creature._habitat = -1;
            }
        }
        for ( HabitatInstance& match : listMatch )
        {
            if ( match._id >= 0 )
                continue;
            match._id = _nextHabitatId++;
            _eventBuffer.push( CreatureTownEvent{ match._habitatId, {}, match._id, CreatureTownEvent::Kind::HabitatFormed } );
        }
        _listHabitat = std::move( listMatch );

        // 서식지를 잃은 생물은 좋아하는 서식지의 빈자리로 옮긴다.
        for ( TownCreature& creature : _listCreature )
        {
            if ( creature._habitat >= 0 )
                continue;
            const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( creature._speciesId );
            for ( const HabitatInstance& instance : _listHabitat )
            {
                if ( pSpecies != nullptr && pSpecies->likesHabitat( instance._habitatId ) && hasRoom( instance ) )
                {
                    creature._habitat = instance._id;
                    break;
                }
            }
        }
        notifyHabitatObjectives();
        flushQuestEvents();
        updateAppealTier();
    }

    bool CreatureTown::matchesAt( const HabitatDef& habitat, int32 rotation, int32 originX, int32 originY, const vector<uint8>& listClaimed,
                                  vector<int32>& outListTile ) const
    {
        outListTile.clear();
        const int2 size = CreatureTownInternal::computeRotatedSize( habitat, rotation );
        if ( originX + size._x > _width || originY + size._y > _height )
            return false;
        for ( int32 rotatedY = 0; rotatedY < size._y; ++rotatedY )
        {
            for ( int32 rotatedX = 0; rotatedX < size._x; ++rotatedX )
            {
                const HabitatCell& cell = CreatureTownInternal::sampleRotated( habitat, rotation, rotatedX, rotatedY );
                if ( cell._bAny != SW_FALSE )
                    continue;
                const int32 tileIndex = ( originY + rotatedY ) * _width + originX + rotatedX;
                if ( listClaimed[static_cast<size_t>( tileIndex )] != SW_FALSE || ( _listObject[static_cast<size_t>( tileIndex )] == cell._object ) == false )
                    return false;
                outListTile.push_back( tileIndex );
            }
        }
        return outListTile.empty() == false;
    }

    int32 CreatureTown::findCreatureIndex( const hashed_string& speciesId ) const
    {
        for ( int32 creatureIndex = 0; creatureIndex < static_cast<int32>( _listCreature.size() ); ++creatureIndex )
        {
            if ( _listCreature[static_cast<size_t>( creatureIndex )]._speciesId == speciesId )
                return creatureIndex;
        }
        return -1;
    }

    bool CreatureTown::hasRoom( const HabitatInstance& instance ) const
    {
        const HabitatDef* pHabitat = _pCatalog->findHabitat( instance._habitatId );
        return pHabitat != nullptr && countResidents( instance._id ) < pHabitat->_capacity;
    }

    void CreatureTown::notifyHabitatObjectives()
    {
        const hashed_string habitatKind = CreatureTownInternal::makeHabitatKind();
        for ( const HabitatDef& habitat : _pCatalog->getHabitats() )
            (void)_questLog.notifyCount( habitatKind, habitat._id, countHabitats( habitat._id ) );
    }

    void CreatureTown::flushReputationEvents()
    {
        _listReputationScratch.clear();
        _friendship.drainEvents( _listReputationScratch );
        for ( const ReputationEvent& reputationEvent : _listReputationScratch )
        {
            _eventBuffer.push(
                CreatureTownEvent{ reputationEvent._factionId, reputationEvent._newTier, reputationEvent._value, CreatureTownEvent::Kind::FriendshipTierChanged } );
        }
        updateAppealTier();
    }

    void CreatureTown::flushQuestEvents()
    {
        _listQuestScratch.clear();
        _questLog.drainEvents( _listQuestScratch );
        bool bFriendshipChanged = false;
        for ( const QuestEvent& questEvent : _listQuestScratch )
        {
            if ( questEvent._kind != QuestEvent::Kind::Completed )
                continue;
            // 그 부탁을 하는 생물(마을에 사는) 에게 호감도를 준다.
            for ( const TownCreature& creature : _listCreature )
            {
                const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( creature._speciesId );
                if ( pSpecies == nullptr || pSpecies->offersRequest( questEvent._questId ) == false )
                    continue;
                _eventBuffer.push( CreatureTownEvent{ creature._speciesId, questEvent._questId, 0, CreatureTownEvent::Kind::RequestCompleted } );
                (void)_friendship.changeValue( creature._speciesId, _settings._requestPoints );
                bFriendshipChanged = true;
                break;
            }
        }
        if ( bFriendshipChanged )
            flushReputationEvents();
    }

    void CreatureTown::updateAppealTier()
    {
        if ( _pCatalog == nullptr )
            return;
        const vector<TownAppealTier>& listTier = _pCatalog->getAppeal()._listTier;
        const float32                 score    = computeAppealScore();
        int32                         tier     = -1;
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
        const int32 tileIndex = pHabitat->_listTile.front();
        return int2{ tileIndex % _width, tileIndex / _width };
    }
} // namespace sw
