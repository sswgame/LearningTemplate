#include "pch.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternOutlaw.h"

#include "Core/Log/LogOnce.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/RPG/OpenWorldWestern/WesternCatalog.h"
#include "GameFramework/Runtime/Foundation/Condition/GameFlags.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemStackList.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"

namespace sw
{
    namespace
    {
        struct WesternOutlawInternal
        {
            static constexpr int32   kCentsPerDollar = 100;
            static constexpr float64 kHoursPerDay    = 24.0;

            static float64 readClockHours( const WorldClock& clock ) { return static_cast<float64>( clock.getDay() ) * kHoursPerDay + static_cast<float64>( clock.getHour() ); }
        };
    } // namespace

    WesternOutlaw::WesternOutlaw()
        : _honor{}
        , _law{}
        , _survival{}
        , _sight{ AIPerceptionSettings{}, nullptr }
        , _random{}
        , _listCarcass{}
        , _listPelt{}
        , _refs{}
        , _regionID{}
        , _pCatalog{ nullptr }
        , _pLoot{ nullptr }
        , _lastClockHours{ 0.0 }
        , _regionBaseTemperature{ 0.0f }
        , _centsCarry{ 0 }
        , _bClockSeen{ SW_FALSE }
    {
    }

    bool WesternOutlaw::initialize( const WesternCatalog* pCatalog, const LootCatalog* pLoot, const SessionStateRefs& refs, const hashed_string& regionID, float32 regionBaseTemperature,
                                    const AIPerceptionSettings& sight )
    {
        const bool bMissingState = pCatalog == nullptr || refs._pWallet == nullptr || refs._pReputation == nullptr || refs._pClock == nullptr;
        if ( bMissingState )
        {
            SW_LOG_ERROR_ID( "WesternOutlaw.MissingCatalogOrSessionState", "[Western] the outlaw needs a western catalog and the session wallet, reputation and clock" );
            return false;
        }
        if ( pCatalog->findRegion( regionID ) == nullptr )
        {
            SW_LOG_ERROR_ID( "WesternOutlaw.RegionNotInCatalog", "[Western] region '%#' is not in the western catalog", regionID.c_str() );
            return false;
        }
        if ( _survival.initialize( pCatalog ) == false )
            return false; // 까닭은 생존이 남겼다
        _honor.initialize( pCatalog, refs );
        _law.initialize( pCatalog );
        _sight                 = WesternPerceptionSight{ sight, nullptr };
        _refs                  = refs;
        _regionID              = regionID;
        _pCatalog              = pCatalog;
        _pLoot                 = pLoot;
        _regionBaseTemperature = regionBaseTemperature;
        _bClockSeen            = SW_FALSE;
        _centsCarry            = 0;
        _listCarcass.clear();
        _listPelt.clear();
        return true;
    }

    float32 WesternOutlaw::computeAirTemperature() const
    {
        return _refs._pWeather != nullptr ? WesternSurvival::computeAirTemperature( *_refs._pWeather, _regionBaseTemperature ) : _regionBaseTemperature;
    }

    int64 WesternOutlaw::getMoney() const
    {
        return _refs._pWallet != nullptr && _pCatalog != nullptr ? _refs._pWallet->getBalance( _pCatalog->getCurrency() ) : 0;
    }

    void WesternOutlaw::update( float32 deltaTime )
    {
        using Internal = WesternOutlawInternal;
        if ( _pCatalog == nullptr )
            return;
        const float64 clockHours = Internal::readClockHours( *_refs._pClock );
        const float64 lastHours  = _bClockSeen == SW_TRUE ? _lastClockHours : clockHours;
        const float32 gameHours  = clockHours > lastHours ? static_cast<float32>( clockHours - lastHours ) : 0.0f; // 불러온 시계가 뒤로 가도 시간을 되돌리지 않는다
        const int32   dayCount   = static_cast<int32>( clockHours / Internal::kHoursPerDay ) - static_cast<int32>( lastHours / Internal::kHoursPerDay );
        _lastClockHours          = clockHours;
        _bClockSeen              = SW_TRUE;

        _survival.update( deltaTime, gameHours, computeAirTemperature() );
        for ( WesternCarcass& carcass : _listCarcass )
        {
            WesternCarcass::ageCarcass( carcass, gameHours );
        }
        _law.update( deltaTime );
        for ( int32 dayIndex = 0; dayIndex < dayCount; ++dayIndex )
        {
            _law.advanceDay();
        }
        logLawEvents();
        if ( _refs._pFlags != nullptr )
            _honor.applyDialogueFlags( *_refs._pFlags );
    }

    bool WesternOutlaw::perform( const WesternDeed& deed, const vector<WesternWitness>& listCandidate )
    {
        if ( _pCatalog == nullptr )
            return false;
        switch ( deed._kind )
        {
            case WesternDeedKind::Crime:
            {
                return commitCrime( deed, listCandidate );
            }
            case WesternDeedKind::HonorAction:
            {
                if ( _pCatalog->findHonorAction( deed._id ) == nullptr )
                    return false;
                (void)_honor.applyAction( deed._id );
                return true;
            }
            case WesternDeedKind::Hunt:
            {
                return hunt( deed );
            }
            case WesternDeedKind::Skin:
            {
                return skinCarcasses();
            }
            case WesternDeedKind::SellPelts:
            {
                return sellPelts();
            }
            case WesternDeedKind::PayBounty:
            {
                return _law.payBounty( _regionID, *_refs._pWallet ) == WesternPayResult::Ok;
            }
            case WesternDeedKind::Eat:
            {
                return _survival.eat( deed._id );
            }
            case WesternDeedKind::ChangeClothes:
            {
                if ( _survival.setClothing( vector<hashed_string>{ deed._id } ) == false )
                    return false;
                _law.setDisguised( true );
                return true;
            }
        }
        return false;
    }

    void WesternOutlaw::updateSeenByLaw( const float3& position, const vector<WesternWitness>& listCandidate )
    {
        bool bSeen = false;
        for ( const WesternWitness& witness : listCandidate )
        {
            if ( witness._bLawman != SW_FALSE && _sight.canWitnessSee( witness, position ) )
                bSeen = true;
        }
        _law.setSeenByLaw( _regionID, bSeen );
    }

    bool WesternOutlaw::commitCrime( const WesternDeed& deed, const vector<WesternWitness>& listCandidate )
    {
        if ( _law.commitCrime( deed._id, _regionID, deed._position, listCandidate, _sight, false ) == 0 )
            return false;
        (void)_honor.applyCrime( deed._id ); // 명예는 목격과 상관없이 바뀐다(양심의 문제다)
        return true;
    }

    bool WesternOutlaw::hunt( const WesternDeed& deed )
    {
        if ( _pCatalog->findAnimal( deed._id ) == nullptr )
            return false;
        WesternKill kill;
        kill._animalID = deed._id;
        kill._weaponID = deed._weapon;
        kill._zoneID   = deed._zone;
        kill._hitCount = deed._hitCount;
        _listCarcass.push_back( WesternCarcass::makeCarcass( *_pCatalog, kill ) );
        return true;
    }

    bool WesternOutlaw::skinCarcasses()
    {
        bool          bSkinned = false;
        ItemStackList listItem;
        // 사체의 전리품 테이블(고기 · 깃털)을 굴려 빌린 가방에 넣는다(RDR2 의 손질 → 가방과 같다).
        for ( size_t carcassIndex = _listCarcass.size(); carcassIndex > 0; --carcassIndex )
        {
            const WesternAnimalDef* const pAnimal = _pCatalog->findAnimal( _listCarcass[carcassIndex - 1]._animalID );
            if ( pAnimal != nullptr && pAnimal->_lootTable.empty() == false && ( _pLoot == nullptr || _refs._pInventory == nullptr ) )
            {
                SW_LOG_ERROR_ONCE_ID( "WesternOutlaw.LootWithoutCatalogOrBag", "[Western] animal '%#' names loot table '%#' but the outlaw has no loot catalog or bag - the loot is not rolled",
                                      pAnimal->_id.c_str(), pAnimal->_lootTable.c_str() );
            }
            WesternPelt pelt;
            if ( WesternCarcass::skin( *_pCatalog, _listCarcass[carcassIndex - 1], _refs._pInventory != nullptr ? _pLoot : nullptr, _random, pelt, listItem ) == false )
                continue;
            _listPelt.push_back( pelt );
            _listCarcass.erase( _listCarcass.begin() + static_cast<ptrdiff_t>( carcassIndex - 1 ) );
            bSkinned = true;
        }
        for ( const ItemStack& stack : listItem.getItems() )
        {
            const int32 added = _refs._pInventory->addItem( stack._itemID, stack._count );
            if ( added < stack._count )
                SW_LOG_WARNING_ID( "WesternOutlaw.BagFull", "[Western] the bag is full - %# of '%#' from skinning left behind", stack._count - added, stack._itemID.c_str() );
        }
        return bSkinned;
    }

    bool WesternOutlaw::sellPelts()
    {
        using Internal = WesternOutlawInternal;
        if ( _listPelt.empty() )
            return false;
        int32 cents = _centsCarry;
        for ( const WesternPelt& pelt : _listPelt )
        {
            cents += WesternCarcass::computePeltPrice( *_pCatalog, pelt );
        }
        // 할인 비율만큼 값을 더 쳐준다(음수 할인이면 깎는다). 값이 뒤집히지 않게 0 아래로는 내리지 않는다.
        const float32 honorBonus = MathUtil::max( 0.0f, 2.0f - _honor.computePriceScale() );
        const int32   paid       = static_cast<int32>( MathUtil::round( static_cast<float32>( cents ) * honorBonus ) );
        const int32   dollars    = paid / Internal::kCentsPerDollar;
        _centsCarry              = paid - dollars * Internal::kCentsPerDollar;
        if ( dollars > 0 )
            _refs._pWallet->add( _pCatalog->getCurrency(), dollars );
        _listPelt.clear();
        return true;
    }

    void WesternOutlaw::logLawEvents()
    {
        vector<WesternLawEvent> listEvent;
        _law.pollEvents( listEvent );
        for ( const WesternLawEvent& lawEvent : listEvent )
        {
            switch ( lawEvent._kind )
            {
                case WesternLawEvent::Kind::Reported:
                {
                    SW_LOG_INFO_ID( "WesternOutlaw.CrimeReported", "[Western] %# was reported in %# - bounty %#", lawEvent._crimeID.c_str(), lawEvent._regionID.c_str(), lawEvent._value );
                    break;
                }
                case WesternLawEvent::Kind::WantedRaised:
                {
                    SW_LOG_INFO_ID( "WesternOutlaw.WantedRaised", "[Western] wanted level %# in %#", lawEvent._value, lawEvent._regionID.c_str() );
                    break;
                }
                case WesternLawEvent::Kind::WantedLowered:
                {
                    SW_LOG_INFO_ID( "WesternOutlaw.WantedLowered", "[Western] the law lost the trail - wanted level %# in %#", lawEvent._value, lawEvent._regionID.c_str() );
                    break;
                }
                case WesternLawEvent::Kind::BountyPaid:
                {
                    SW_LOG_INFO_ID( "WesternOutlaw.BountyPaid", "[Western] paid a bounty of %# in %#", lawEvent._value, lawEvent._regionID.c_str() );
                    break;
                }
                case WesternLawEvent::Kind::Witnessed:
                case WesternLawEvent::Kind::Unwitnessed:
                case WesternLawEvent::Kind::ReportPrevented:
                case WesternLawEvent::Kind::PursuitChanged:
                {
                    break;
                }
            }
        }
    }

    void WesternOutlaw::serializeState( StateIO& io )
    {
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        io.header( kStateTag, kStateVersion );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            _law = WesternLawState{};
            _law.initialize( _pCatalog );
            _survival = WesternSurvival{};
            if ( io.check( _survival.initialize( _pCatalog ) ) == false )
                return;
            _bClockSeen = SW_FALSE;
        }
        io.value( _law ); // 법 · 생존은 제 본문(리플렉션 칸 + 손 값)으로 싣는다
        io.value( _survival );
        io.reflected( *this );
        if ( io.isWriting() || io.isOk() == false )
            return;
        for ( const WesternCarcass& carcass : _listCarcass )
        {
            io.check( _pCatalog->findAnimal( carcass._animalID ) != nullptr ); // 카탈로그 검사만 손으로
        }
        for ( const WesternPelt& pelt : _listPelt )
        {
            io.check( _pCatalog->findAnimal( pelt._animalID ) != nullptr );
        }
    }

} // namespace sw
