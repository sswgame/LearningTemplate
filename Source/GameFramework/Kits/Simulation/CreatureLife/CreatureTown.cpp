#include "pch.h"

#include "GameFramework/Kits/Simulation/CreatureLife/CreatureTown.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Framework/GameStateRefs.h"
#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Quest/QuestLog.h"
#include "GameFramework/Base/Utility/GameRandom.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/World/WeatherSystem.h"

namespace sw
{
    SW_LOG_CALLER( "CreatureTown" );

    namespace
    {
        struct CreatureTownInternal
        {
            /** @brief 종의 호감도 세력 id 입니다 — 공유 평판에서 키트 접두로 다른 키트의 세력과 갈린다(문자열 붙이기는 여기 한 곳). */
            static hashed_string makeFactionId( const hashed_string& speciesId ) { return hashed_string( string( "creature." ) + speciesId.c_str() ); }

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
            case CreatureAbilityResult::LandTaken:
                return "LandTaken";
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
        , _listOpenRequest{}
        , _eventBuffer{}
        , _settings{}
        , _pCatalog{ nullptr }
        , _pReputation{ nullptr }
        , _pQuestLog{ nullptr }
        , _pClock{ nullptr }
        , _topology{}
        , _land{}
        , _nextHabitatId{ 1 }
        , _lastAttractKey{ -1 }
        , _appealTier{ -1 }
    {
    }

    void CreatureTown::initialize( const CreatureLifeCatalog* pCatalog, const GameStateRefs& refs, int32 width, int32 height, const CreatureTownSettings& settings )
    {
        _pCatalog    = pCatalog;
        _pReputation = refs._pReputation;
        _pQuestLog   = refs._pQuestLog;
        _pClock      = refs._pClock;
        _settings    = settings;
        _topology    = GridTopology{ MathUtil::max( 1, width ), MathUtil::max( 1, height ) };
        _listObject.assign( static_cast<size_t>( _topology.getCellCount() ), hashed_string{} );
        _listHabitat.clear();
        _listCreature.clear();
        _listHouse.clear();
        _listOpenRequest.clear();
        _eventBuffer.clear();
        _nextHabitatId  = 1;
        _lastAttractKey = -1;
        _appealTier     = -1;
        updateAppealTier();
        _eventBuffer.clear(); // 처음 단계는 알리지 않는다
    }

    bool CreatureTown::bindLand( LandRegistry* pLand, const int2& origin )
    {
        LandBinding land;
        land.bind( pLand, origin, hashed_string( "CreatureLife" ) );
        // 이미 놓인 칸을 먼저 모두 볼 수 있어야 얻는다(반쯤 얻고 실패하지 않게).
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
                const uint32  placeKey = static_cast<uint32>( _topology.toIndex( instance._origin ) ) * 4u + static_cast<uint32>( instance._rotation );
                const uint32  salt     = static_cast<uint32>( speciesIndex ) * 977u + placeKey * 31u;
                const float32 roll     = GameHash::toUnitFloat( GameHash::hashCoord( attractKey, static_cast<int32>( salt ), _settings._randomSeed ) );
                if ( roll >= species._chance && species._chance < 1.0f )
                    continue;
                TownCreature creature;
                creature._speciesId  = species._id;
                creature._habitat    = instance._id;
                creature._arrivalDay = getDay();
                creature._listAbilityUse.assign( species._listAbility.size(), 0 );
                _listCreature.push_back( creature );
                _eventBuffer.push( CreatureTownEvent{ species._id, {}, instance._id, CreatureTownEvent::Kind::CreatureArrived } );
                if ( _pReputation != nullptr )
                    (void)_pReputation->changeValue( CreatureTownInternal::makeFactionId( species._id ), 0 ); // 세력 자리를 만들어 둔다(시작값)
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
        if ( creature._lastTalkDay == getDay() )
            return CreatureInteractResult::AlreadyToday;
        creature._lastTalkDay = getDay();
        changeFriendship( speciesId, _settings._talkPoints );
        return CreatureInteractResult::Ok;
    }

    CreatureInteractResult CreatureTown::giveGift( const hashed_string& speciesId, const hashed_string& itemId, Inventory& inventory )
    {
        const int32 creatureIndex = findCreatureIndex( speciesId );
        if ( creatureIndex < 0 )
            return CreatureInteractResult::UnknownCreature;
        TownCreature& creature = _listCreature[static_cast<size_t>( creatureIndex )];
        if ( creature._lastGiftDay == getDay() )
            return CreatureInteractResult::AlreadyToday;
        if ( inventory.removeItem( itemId, 1 ) == false )
            return CreatureInteractResult::MissingItem;
        creature._lastGiftDay              = getDay();
        const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( speciesId );
        int32                     points   = _settings._giftPoints;
        if ( pSpecies != nullptr && pSpecies->likesGift( itemId ) )
            points = _settings._likedGiftPoints;
        else if ( pSpecies != nullptr && pSpecies->likesFood( itemId ) )
            points = _settings._foodPoints;
        changeFriendship( speciesId, points );
        return CreatureInteractResult::Ok;
    }

    CreatureRequestResult CreatureTown::startRequest( const hashed_string& speciesId, const hashed_string& questId )
    {
        if ( findCreatureIndex( speciesId ) < 0 )
            return CreatureRequestResult::UnknownCreature;
        const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( speciesId );
        if ( pSpecies == nullptr || pSpecies->offersRequest( questId ) == false )
            return CreatureRequestResult::NotOffered;
        if ( _pQuestLog == nullptr )
            return CreatureRequestResult::Unavailable;
        const QuestStartResult startResult = _pQuestLog->start( questId, MathUtil::max( 0, _pReputation != nullptr ? _pReputation->getTierIndex( CreatureTownInternal::makeFactionId( speciesId ) ) : 0 ) );
        if ( startResult == QuestStartResult::AlreadyActive )
            return CreatureRequestResult::AlreadyActive;
        if ( startResult != QuestStartResult::Ok )
            return CreatureRequestResult::Unavailable;
        _listOpenRequest.push_back( questId );
        notifyHabitatObjectives(); // 이미 있는 서식지도 센다
        collectCompletedRequests();
        return CreatureRequestResult::Ok;
    }

    int32 CreatureTown::deliverItem( const hashed_string& itemId, int32 count, Inventory& inventory )
    {
        const bool bCanDeliver = 0 < count && _pQuestLog != nullptr && inventory.hasItem( itemId, count );
        if ( bCanDeliver == false )
            return 0;
        const int32 progressedCount = _pQuestLog->notify( CreatureTownInternal::makeDeliverKind(), itemId, count );
        if ( progressedCount > 0 )
        {
            if ( inventory.removeItem( itemId, count ) == false )
                SW_LOG_WARNING( "inventory lost '%#' between hasItem and removeItem", itemId.c_str() );
        }
        collectCompletedRequests();
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
        _eventBuffer.push( CreatureTownEvent{ speciesId, pRule->_yieldItem, tileIndex, CreatureTownEvent::Kind::AbilityUsed } );
        (void)setObject( x, y, pRule->_to );
        return CreatureAbilityResult::Ok;
    }

    int32 CreatureTown::placeHouse( int32 x, int32 y, int32 capacity )
    {
        if ( _topology.isInside( x, y ) == false || _listObject[static_cast<size_t>( _topology.toIndex( x, y ) )].empty() == false || _land.isUsable( x, y ) == false )
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
        for ( TownCreature& creature : _listCreature )
        {
            std::fill( creature._listAbilityUse.begin(), creature._listAbilityUse.end(), 0 );
        }
        updateAppealTier(); // 공유 평판은 주인이 날 넘김에 식혔다 — 매력도만 다시 본다
    }

    void CreatureTown::drainEvents( vector<CreatureTownEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    const hashed_string* CreatureTown::findObject( int32 x, int32 y ) const
    {
        if ( _topology.isInside( x, y ) == false )
            return nullptr;
        return &_listObject[static_cast<size_t>( _topology.toIndex( x, y ) )];
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
        {
            total += getFriendship( creature._speciesId );
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
        if ( _pQuestLog == nullptr )
            return;
        const hashed_string habitatKind = CreatureTownInternal::makeHabitatKind();
        for ( const HabitatDef& habitat : _pCatalog->getHabitats() )
        {
            (void)_pQuestLog->notifyCount( habitatKind, habitat._id, countHabitats( habitat._id ) );
        }
    }

    void CreatureTown::changeFriendship( const hashed_string& speciesId, int32 delta )
    {
        if ( _pReputation == nullptr )
            return;
        const hashed_string factionId  = CreatureTownInternal::makeFactionId( speciesId );
        const int32         tierBefore = _pReputation->getTierIndex( factionId );
        (void)_pReputation->changeValue( factionId, delta );
        if ( _pReputation->getTierIndex( factionId ) != tierBefore )
            _eventBuffer.push( CreatureTownEvent{ speciesId, _pReputation->getTierName( factionId ), _pReputation->getValue( factionId ), CreatureTownEvent::Kind::FriendshipTierChanged } );
        updateAppealTier();
    }

    void CreatureTown::collectCompletedRequests()
    {
        if ( _pQuestLog == nullptr || _listOpenRequest.empty() )
            return;
        size_t keptCount = 0;
        for ( size_t requestIndex = 0; requestIndex < _listOpenRequest.size(); ++requestIndex )
        {
            const hashed_string questId = _listOpenRequest[requestIndex];
            const QuestStatus   status  = _pQuestLog->getStatus( questId );
            if ( status == QuestStatus::Active )
            {
                _listOpenRequest[keptCount] = questId;
                ++keptCount;
                continue;
            }
            // 끝났다 — 완료면 그 부탁을 하는 생물에게 호감도를 준다. 실패 · 포기(NotStarted)는 보상 없이 뺀다.
            if ( status == QuestStatus::Completed )
                (void)rewardRequest( questId );
        }
        _listOpenRequest.resize( keptCount );
    }

    bool CreatureTown::rewardRequest( const hashed_string& questId )
    {
        for ( const TownCreature& creature : _listCreature )
        {
            const CreatureSpeciesDef* pSpecies = _pCatalog->findSpecies( creature._speciesId );
            if ( pSpecies == nullptr || pSpecies->offersRequest( questId ) == false )
                continue;
            _eventBuffer.push( CreatureTownEvent{ creature._speciesId, questId, 0, CreatureTownEvent::Kind::RequestCompleted } );
            changeFriendship( creature._speciesId, _settings._requestPoints );
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
        return _topology.toCell( pHabitat->_listTile.front() );
    }

    void CreatureTown::writeState( Archive& outArchive ) const
    {
        outArchive << _topology._width;
        outArchive << _topology._height;
        for ( const hashed_string& object : _listObject )
        {
            StateArchiveUtil::writeName( outArchive, object );
        }
        outArchive << static_cast<uint32>( _listHabitat.size() );
        for ( const HabitatInstance& habitat : _listHabitat )
        {
            StateArchiveUtil::writeName( outArchive, habitat._habitatId );
            StateArchiveUtil::writeInt2( outArchive, habitat._origin );
            outArchive << habitat._id;
            outArchive << habitat._rotation;
            outArchive << static_cast<uint32>( habitat._listTile.size() );
            for ( const int32 tileIndex : habitat._listTile )
            {
                outArchive << tileIndex;
            }
        }
        outArchive << static_cast<uint32>( _listCreature.size() );
        for ( const TownCreature& creature : _listCreature )
        {
            StateArchiveUtil::writeName( outArchive, creature._speciesId );
            outArchive << static_cast<uint32>( creature._listAbilityUse.size() );
            for ( const int32 useCount : creature._listAbilityUse )
            {
                outArchive << useCount;
            }
            outArchive << creature._habitat;
            outArchive << creature._house;
            outArchive << creature._lastTalkDay;
            outArchive << creature._lastGiftDay;
            outArchive << creature._arrivalDay;
        }
        outArchive << static_cast<uint32>( _listHouse.size() );
        for ( const CreatureHouse& house : _listHouse )
        {
            outArchive << static_cast<uint32>( house._listResident.size() );
            for ( const hashed_string& resident : house._listResident )
            {
                StateArchiveUtil::writeName( outArchive, resident );
            }
            StateArchiveUtil::writeInt2( outArchive, house._tile );
            outArchive << house._capacity;
        }
        outArchive << static_cast<uint32>( _listOpenRequest.size() );
        for ( const hashed_string& questId : _listOpenRequest )
        {
            StateArchiveUtil::writeName( outArchive, questId );
        }
        outArchive << _nextHabitatId;
        outArchive << _lastAttractKey;
    }

    bool CreatureTown::readState( Archive& archive )
    {
        int32 width  = 0;
        int32 height = 0;
        archive >> width;
        archive >> height;
        if ( archive.isError() || width != _topology._width || height != _topology._height )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 카탈로그 · 설정 · 빌린 일지 · 시계 포인터는 사본이 그대로 든다.
        CreatureTown town      = *this;
        const int32  tileCount = width * height;
        for ( hashed_string& object : town._listObject )
        {
            if ( StateArchiveUtil::readName( archive, object ) == false )
                return false;
        }

        uint32 habitatCount = 0;
        // 서식지마다 이름(4) + 자리(8) + 번호(4) + 회전(4) + 칸 수(4) 이상
        if ( StateArchiveUtil::readCount( archive, 24, habitatCount ) == false )
            return false;
        town._listHabitat.assign( habitatCount, HabitatInstance{} );
        for ( HabitatInstance& habitat : town._listHabitat )
        {
            uint32 habitatTileCount = 0;
            if ( StateArchiveUtil::readName( archive, habitat._habitatId ) == false )
                return false;
            StateArchiveUtil::readInt2( archive, habitat._origin );
            archive >> habitat._id;
            archive >> habitat._rotation;
            if ( StateArchiveUtil::readCount( archive, 4, habitatTileCount ) == false )
                return false;
            habitat._listTile.resize( habitatTileCount, 0 );
            for ( int32& tileIndex : habitat._listTile )
            {
                archive >> tileIndex;
                const bool bInside = 0 <= tileIndex && tileIndex < tileCount;
                if ( bInside == false )
                    return false;
            }
        }

        uint32 creatureCount = 0;
        // 생물마다 이름(4) + 능력 수(4) + 서식지 · 집 · 대화 날 · 선물 날 · 온 날(20) 이상
        if ( StateArchiveUtil::readCount( archive, 28, creatureCount ) == false )
            return false;
        town._listCreature.assign( creatureCount, TownCreature{} );
        for ( TownCreature& creature : town._listCreature )
        {
            uint32     abilityCount = 0;
            const bool bHeadRead    = StateArchiveUtil::readName( archive, creature._speciesId ) && StateArchiveUtil::readCount( archive, 4, abilityCount );
            if ( bHeadRead == false )
                return false;
            creature._listAbilityUse.resize( abilityCount, 0 );
            for ( int32& useCount : creature._listAbilityUse )
            {
                archive >> useCount;
            }
            archive >> creature._habitat;
            archive >> creature._house;
            archive >> creature._lastTalkDay;
            archive >> creature._lastGiftDay;
            archive >> creature._arrivalDay;
        }

        uint32 houseCount = 0;
        // 집마다 사는 수(4) + 칸(8) + 정원(4) 이상
        if ( StateArchiveUtil::readCount( archive, 16, houseCount ) == false )
            return false;
        town._listHouse.assign( houseCount, CreatureHouse{} );
        for ( CreatureHouse& house : town._listHouse )
        {
            uint32 residentCount = 0;
            if ( StateArchiveUtil::readCount( archive, 4, residentCount ) == false )
                return false;
            house._listResident.assign( residentCount, hashed_string{} );
            for ( hashed_string& resident : house._listResident )
            {
                if ( StateArchiveUtil::readName( archive, resident ) == false )
                    return false;
            }
            StateArchiveUtil::readInt2( archive, house._tile );
            archive >> house._capacity;
        }

        uint32 requestCount = 0;
        if ( StateArchiveUtil::readCount( archive, 4, requestCount ) == false )
            return false;
        town._listOpenRequest.assign( requestCount, hashed_string{} );
        for ( hashed_string& questId : town._listOpenRequest )
        {
            if ( StateArchiveUtil::readName( archive, questId ) == false )
                return false;
        }
        archive >> town._nextHabitatId;
        archive >> town._lastAttractKey;
        if ( archive.isError() )
            return false;

        // 생물이 가리키는 서식지 번호 · 집 자리가 실제로 있어야 한다.
        const int32 townHouseCount = static_cast<int32>( town._listHouse.size() );
        for ( const TownCreature& creature : town._listCreature )
        {
            const bool bHabitatValid = creature._habitat < 0 || town.findHabitat( creature._habitat ) != nullptr;
            const bool bHouseValid   = -1 <= creature._house && creature._house < townHouseCount;
            if ( bHabitatValid == false || bHouseValid == false )
                return false;
        }
        town._eventBuffer.clear();
        town.updateAppealTier();
        town._eventBuffer.clear(); // 되살린 단계는 알리지 않는다
        *this = std::move( town );
        return true;
    }

    int32 CreatureTown::getDay() const { return _pClock != nullptr ? _pClock->getDay() : 0; }

    int32 CreatureTown::getFriendship( const hashed_string& speciesId ) const
    {
        return _pReputation != nullptr ? _pReputation->getValue( CreatureTownInternal::makeFactionId( speciesId ) ) : 0;
    }

    hashed_string CreatureTown::getFriendshipTier( const hashed_string& speciesId ) const
    {
        return _pReputation != nullptr ? _pReputation->getTierName( CreatureTownInternal::makeFactionId( speciesId ) ) : hashed_string{};
    }
} // namespace sw
