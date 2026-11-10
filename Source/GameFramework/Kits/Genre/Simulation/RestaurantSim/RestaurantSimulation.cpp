#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantSimulation.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantCatalog.h"
#include "GameFramework/Runtime/Foundation/Framework/SessionStateRefs.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"

namespace sw
{
    namespace
    {
        struct RestaurantSimulationInternal
        {
            static constexpr float32 kMinDemand      = 0.05f;
            static constexpr float32 kMinutesPerHour = 60.0f;

            static bool isSeated( const RestaurantCustomer& customer )
            {
                return customer._state == CustomerState::WaitingFood || customer._state == CustomerState::Eating || customer._state == CustomerState::WaitingCheckout;
            }

            /** @brief 주문 후보 하나입니다. 요리와, 그 손님이 고를 가중치(양수만 담습니다)를 가집니다. */
            struct DishChoice
            {
                const DishDef* _pDish{ nullptr };
                float32        _weight{ 0.0f };
            };

            static float32 getChoiceWeight( const DishChoice& choice )
            {
                return choice._weight;
            }

            /** @brief 읽은 결산의 돈과 사람 수가 음수가 아닌지 봅니다(이익만 음수가 될 수 있습니다). */
            static bool isDaySummaryValid( const RestaurantDaySummary& today )
            {
                const bool bMoneyValid  = 0 <= today._revenue && 0 <= today._tips && 0 <= today._ingredientCost && 0 <= today._wages && 0 <= today._spoilageCost;
                const bool bPeopleValid = 0 <= today._arrivals && 0 <= today._served && 0 <= today._walkouts && 0 <= today._noChoice && 0 <= today._unservedAtClose;
                return bMoneyValid && bPeopleValid && 0.0f <= today._rating && today._rating <= 5.0f;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    RestaurantSimulation::RestaurantSimulation()
        : _listStaff{}
        , _listCustomer{}
        , _listOrder{}
        , _listMenu{}
        , _listStation{}
        , _listSatisfaction{}
        , _eventBuffer{}
        , _listSpoilageScratch{}
        , _stock{}
        , _crafter{}
        , _market{}
        , _settings{}
        , _today{}
        , _stepTimer{ kStepMinutes, 24.0f * 60.0f }
        , _random{}
        , _weatherID{}
        , _pCatalog{ nullptr }
        , _pPantry{ nullptr }
        , _pWallet{ nullptr }
        , _pReputation{ nullptr }
        , _pClock{ nullptr }
        , _pRecipeCatalog{ nullptr }
        , _pShopCatalog{ nullptr }
        , _pStaffCurve{ nullptr }
        , _minutes{ 0.0f }
        , _arrival{}
        , _pendingSpoilageCost{ 0 }
        , _nextCustomerID{ 1 }
        , _bOpen{ SW_FALSE }
    {
    }

    void RestaurantSimulation::initialize( const RestaurantCatalog* pCatalog, const RecipeCatalog* pRecipeCatalog, const ItemCatalog* pItemCatalog,
                                           const ShopCatalog* pShopCatalog, const ExperienceCurve* pStaffCurve,
                                           const SessionStateRefs& refs, Inventory& pantry, const RestaurantSettings& settings )
    {
        _pCatalog       = pCatalog;
        _pRecipeCatalog = pRecipeCatalog;
        _pShopCatalog   = pShopCatalog;
        _pStaffCurve    = pStaffCurve;
        _settings       = settings;
        _pPantry        = &pantry;
        _pWallet        = refs._pWallet;
        _pReputation    = refs._pReputation;
        _pClock         = refs._pClock;
        _stock.initialize( _pPantry );
        _crafter.initialize( pRecipeCatalog );
        _market.initialize( pShopCatalog, pItemCatalog );
        _random.setSeed( settings._randomSeed );
        _stepTimer = FixedStepTimer( kStepMinutes, 24.0f * 60.0f );
        _listStaff.clear();
        _listCustomer.clear();
        _listOrder.clear();
        _listStation.clear();
        _listSatisfaction.clear();
        _eventBuffer.clear();
        _listMenu.clear();
        if ( pCatalog != nullptr )
        {
            for ( const DishDef& dish : pCatalog->getDishes() )
            {
                _listMenu.push_back( MenuEntry{ dish._id, dish._basePrice, SW_TRUE } );
            }
        }
        _today   = RestaurantDaySummary{};
        _minutes = 0.0f;
        _arrival.reset();
        _pendingSpoilageCost = 0;
        _nextCustomerID      = 1;
        _bOpen               = SW_FALSE;
        rollMarketPrices();
    }

    void RestaurantSimulation::setStationCount( const hashed_string& station, int32 count )
    {
        for ( StationSlot& slot : _listStation )
        {
            if ( slot._station == station )
            {
                slot._count = MathUtil::max( 0, count );
                return;
            }
        }
        _listStation.push_back( StationSlot{ station, MathUtil::max( 0, count ) } );
    }

    int32 RestaurantSimulation::hireStaff( const hashed_string& name, StaffRole role, int64 dailyWage, int32 level )
    {
        StaffMember staff;
        staff._name      = name;
        staff._role      = role;
        staff._dailyWage = MathUtil::max<int64>( 0, dailyWage );
        if ( _pStaffCurve != nullptr )
            staff._level.setLevel( *_pStaffCurve, MathUtil::max( 1, level ) );
        _listStaff.push_back( staff );
        return static_cast<int32>( _listStaff.size() ) - 1;
    }

    bool RestaurantSimulation::fireStaff( int32 staffIndex )
    {
        const bool bInvalidStaffIndex = staffIndex < 0 || staffIndex >= static_cast<int32>( _listStaff.size() ) || _listStaff[static_cast<size_t>( staffIndex )]._bHired == SW_FALSE;
        if ( bInvalidStaffIndex )
            return false;
        StaffMember& staff = _listStaff[static_cast<size_t>( staffIndex )];
        for ( KitchenOrder& order : _listOrder )
        {
            if ( order._cook == staffIndex )
                order._cook = constant::invalid_index::kInt32; // 남은 시간은 다음 요리사가 이어받는다
        }
        const optional<int32> customerIndex = findCustomerIndex( staff._taskCustomer );
        if ( customerIndex.has_value() && staff._role != StaffRole::Cook )
        {
            RestaurantCustomer& customer = _listCustomer[static_cast<size_t>( *customerIndex )];
            customer._bServing           = SW_FALSE;
            customer._bCheckingOut       = SW_FALSE;
        }
        staff._bHired       = SW_FALSE;
        staff._taskCustomer = constant::invalid_index::kInt32;
        staff._busyMinutes  = 0.0f;
        return true;
    }

    bool RestaurantSimulation::setMenuPrice( const hashed_string& dishID, int64 price )
    {
        for ( MenuEntry& entry : _listMenu )
        {
            if ( entry._dishID == dishID )
            {
                entry._price = MathUtil::max<int64>( 1, price );
                return true;
            }
        }
        return false;
    }

    bool RestaurantSimulation::setDishOnMenu( const hashed_string& dishID, bool bOnMenu )
    {
        for ( MenuEntry& entry : _listMenu )
        {
            if ( entry._dishID == dishID )
            {
                entry._bOnMenu = bOnMenu ? SW_TRUE : SW_FALSE;
                return true;
            }
        }
        return false;
    }

    ShopResult RestaurantSimulation::buyIngredient( const hashed_string& shopID, const hashed_string& itemID, int32 count )
    {
        if ( _pWallet == nullptr )
            return ShopResult::NotEnoughMoney;
        int64            spent  = 0;
        const ShopResult result = _market.buy( shopID, itemID, count, *_pWallet, *_pPantry, &spent );
        if ( result != ShopResult::Ok )
            return result;
        const int32 shelfLife = _pCatalog != nullptr ? _pCatalog->findShelfLife( itemID ) : 0;
        _stock.recordBatch( itemID, count, shelfLife, spent / count );
        return ShopResult::Ok;
    }

    int32 RestaurantSimulation::addIngredient( const hashed_string& itemID, int32 count, int64 unitCost )
    {
        const int32 shelfLife = _pCatalog != nullptr ? _pCatalog->findShelfLife( itemID ) : 0;
        return _stock.addFresh( itemID, count, shelfLife, unitCost );
    }

    void RestaurantSimulation::openDay( const hashed_string& weatherID )
    {
        _bOpen     = SW_TRUE;
        _weatherID = weatherID;
        _minutes   = 0.0f;
        _arrival.reset();
        _today               = RestaurantDaySummary{};
        _today._spoilageCost = _pendingSpoilageCost;
        _pendingSpoilageCost = 0;
        _stepTimer.reset();
        _listCustomer.clear();
        _listOrder.clear();
        for ( StaffMember& staff : _listStaff )
        {
            staff._busyMinutes  = 0.0f;
            staff._taskCustomer = constant::invalid_index::kInt32;
        }
    }

    void RestaurantSimulation::update( float32 deltaMinutes )
    {
        if ( _pCatalog == nullptr )
            return;
        const int32 stepCount = _stepTimer.consume( deltaMinutes );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
        {
            stepFixed();
        }
    }

    int32 RestaurantSimulation::admitCustomer( const hashed_string& typeID )
    {
        const CustomerTypeDef* pType = _pCatalog != nullptr ? _pCatalog->findCustomerType( typeID ) : nullptr;
        if ( pType == nullptr )
            return constant::invalid_index::kInt32;
        RestaurantCustomer customer;
        customer._typeID   = typeID;
        customer._id       = _nextCustomerID++;
        customer._patience = pType->_patience;
        _listCustomer.push_back( customer );
        ++_today._arrivals;
        _eventBuffer.push( RestaurantEvent{ typeID, 0, customer._id, RestaurantEvent::Kind::CustomerArrived } );
        return customer._id;
    }

    void RestaurantSimulation::closeDay( RestaurantDaySummary& outSummary )
    {
        for ( RestaurantCustomer& customer : _listCustomer )
        {
            if ( customer._state == CustomerState::Eating || customer._state == CustomerState::WaitingCheckout )
            {
                settlePayment( customer ); // 다 먹은 것으로 치고 계산한다
            }
            else if ( customer._state == CustomerState::Queued || customer._state == CustomerState::WaitingFood )
            {
                customer._state = CustomerState::Left;
                customer._seat  = constant::invalid_index::kInt32;
                ++_today._unservedAtClose;
            }
        }
        _listOrder.clear();
        const hashed_string currency = getCurrency();
        for ( StaffMember& staff : _listStaff )
        {
            staff._busyMinutes  = 0.0f;
            staff._taskCustomer = constant::invalid_index::kInt32;
            if ( staff._bHired == SW_FALSE )
                continue;
            _today._wages += staff._dailyWage;
            if ( _pWallet == nullptr || _pWallet->trySpend( currency, staff._dailyWage ) == false )
            {
                const int64 balance = _pWallet != nullptr ? _pWallet->getBalance( currency ) : 0;
                if ( balance > 0 && _pWallet->trySpend( currency, balance ) == false )
                    SW_LOG_WARNING_ID( "RestaurantSimulation.CouldNotPayRemaining", "could not pay the remaining balance %# to '%#'", balance, staff._name.c_str() );
                SW_LOG_WARNING_ID( "RestaurantSimulation.WageShort", "wage for '%#' is short by %#", staff._name.c_str(), staff._dailyWage - balance );
            }
        }
        _today._profit = _today._revenue + _today._tips - _today._ingredientCost - _today._wages - _today._spoilageCost;
        _today._rating = computeRating();
        _bOpen         = SW_FALSE;
        outSummary     = _today;
    }

    void RestaurantSimulation::advanceDay()
    {
        _listSpoilageScratch.clear();
        _stock.advanceDay( _listSpoilageScratch );
        for ( const IngredientSpoilage& spoilage : _listSpoilageScratch )
        {
            _pendingSpoilageCost += spoilage._cost;
            _eventBuffer.push( RestaurantEvent{ spoilage._itemID, spoilage._count, nullopt, RestaurantEvent::Kind::IngredientSpoiled } );
        }
        _market.advanceDay();
        rollMarketPrices();
    }

    void RestaurantSimulation::pollEvents( vector<RestaurantEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    void RestaurantSimulation::serializeState( StateIO& io )
    {
        // 직원마다 이름(4) + 레벨(경험치와 누적 16 + 레벨 4) + 바쁨(4) + 일당(8) + 맡은 손님(4) + 역할과 고용(2)
        io.list( _listStaff, 42, [&]( StaffMember& staff )
        {
            io.value( staff._name );
            io.value( staff._level );
            io.value( staff._busyMinutes );
            io.value( staff._dailyWage );
            io.value( staff._taskCustomer );
            io.enumValue( staff._role, StaffRole::Cashier );
            io.flag( staff._bHired );
            io.check( 0.0f <= staff._busyMinutes && 0 <= staff._dailyWage && -1 <= staff._taskCustomer );
        } );
        // 손님마다 이름 둘(8) + 기다림, 인내, 먹기(12) + 가격(8) + 번호, 자리, 품질(12) + 상태, 서빙, 계산(3)
        io.list( _listCustomer, 43, [&]( RestaurantCustomer& customer )
        {
            io.value( customer._typeID );
            io.value( customer._dishID );
            io.value( customer._waited );
            io.value( customer._patience );
            io.value( customer._eatRemaining );
            io.value( customer._price );
            io.value( customer._id );
            io.value( customer._seat );
            io.value( customer._quality );
            io.enumValue( customer._state, CustomerState::Left );
            io.flag( customer._bServing );
            io.flag( customer._bCheckingOut );
            io.check( 0.0f <= customer._waited && 0.0f <= customer._patience && 0 <= customer._price && 0 < customer._id && 0 <= customer._quality && -1 <= customer._seat && customer._seat < _settings._seatCount );
            // 앉은 손님만 자리를 가진다(`leaveCustomer`, `settlePayment` 가 비운다).
            io.check( RestaurantSimulationInternal::isSeated( customer ) == ( customer._seat >= 0 ) );
        } );
        uint32 orderCount = static_cast<uint32>( _listOrder.size() );
        // 주문마다 이름 둘(8) + 남은 시간, 손님, 요리사, 품질(16) + 준비(1)
        io.count( 25, orderCount );
        if ( io.isOk() == false )
            return;
        if ( io.isReading() )
        {
            _listOrder.clear();
        }
        const int32 staffCount = static_cast<int32>( _listStaff.size() );
        for ( uint32 index = 0; index < orderCount; ++index )
        {
            KitchenOrder order = io.isWriting() ? _listOrder[index] : KitchenOrder{};
            io.value( order._dishID );
            io.value( order._station );
            io.value( order._remaining );
            io.value( order._customerID );
            io.value( order._cook );
            io.value( order._quality );
            io.flag( order._bReady );
            // 기다리는 주문과 조리 중인 주문의 남은 시간은 음수가 아니다. 다 된 주문만 걸음만큼 넘긴 값이 남는다. 다 되면 요리사를 풀어 준다.
            const bool bProgressValid = order._bReady == SW_FALSE ? 0.0f <= order._remaining : order._cook < 0;
            if ( io.check( constant::invalid_index::kInt32 <= order._cook && order._cook < staffCount && 0 <= order._quality && bProgressValid ) == false )
                return;
            if ( io.isReading() )
            {
                _listOrder.push_back( order );
            }
        }
        // 메뉴는 카탈로그의 요리마다 하나이다. 이름(4) + 가격(8) + 켜짐(1)
        io.expect( static_cast<uint32>( _listMenu.size() ) );
        if ( io.isOk() == false )
            return;
        for ( MenuEntry& entry : _listMenu )
        {
            io.value( entry._dishID );
            io.value( entry._price );
            io.flag( entry._bOnMenu );
            // `setMenuPrice` 처럼 가격은 1 이상이다. 음수 가격은 손님이 낼 때 지갑에서 돈을 뺀다.
            io.check( 1 <= entry._price );
        }
        // 스테이션마다 이름(4) + 수(4)
        io.list( _listStation, 8, [&]( StationSlot& slot )
        {
            io.value( slot._station );
            io.value( slot._count );
            io.check( slot._station.empty() == false && 0 <= slot._count );
        } );
        io.list( _listSatisfaction, 4, [&]( float32& satisfaction )
        {
            io.value( satisfaction );
            io.check( 0.0f <= satisfaction && satisfaction <= 1.0f );
        } );
        io.value( _stock );
        io.value( _crafter );
        io.value( _market );
        io.value( _today._revenue );
        io.value( _today._tips );
        io.value( _today._ingredientCost );
        io.value( _today._wages );
        io.value( _today._spoilageCost );
        io.value( _today._profit );
        io.value( _today._rating );
        io.value( _today._arrivals );
        io.value( _today._served );
        io.value( _today._walkouts );
        io.value( _today._noChoice );
        io.value( _today._unservedAtClose );
        io.value( _stepTimer );
        io.value( _random );
        io.value( _weatherID );
        io.value( _minutes );
        io.value( _arrival );
        io.value( _pendingSpoilageCost );
        io.value( _nextCustomerID );
        io.flag( _bOpen );
        if ( io.isWriting() || io.isOk() == false )
            return;
        io.check( 0.0f <= _minutes && 0 <= _pendingSpoilageCost && 0 < _nextCustomerID && RestaurantSimulationInternal::isDaySummaryValid( _today ) && areLinksValid() );
        _eventBuffer.clear();
        _listSpoilageScratch.clear();
    }

    bool RestaurantSimulation::areLinksValid() const
    {
        if ( _pCatalog == nullptr && ( _listCustomer.empty() == false || _listOrder.empty() == false ) )
            return false; // 카탈로그 없이는 손님을 받지 못한다(`admitCustomer`)
        // 메뉴와 스테이션은 요리와 이름마다 한 줄이다. 찾기는 앞의 것만 돌려주어 뒤의 줄은 쓰이지 않는다.
        for ( const MenuEntry& entry : _listMenu )
        {
            if ( _pCatalog == nullptr || _pCatalog->findDish( entry._dishID ) == nullptr )
                return false;
        }
        const bool bRepeatedLine = VectorUtil::findDuplicateBy<&MenuEntry::_dishID>( _listMenu ) != nullptr || VectorUtil::findDuplicateBy<&StationSlot::_station>( _listStation ) != nullptr;
        // 별점 창보다 많은 만족도는 `pushSatisfaction` 이 남기지 않는다.
        const size_t ratingWindow = static_cast<size_t>( _pCatalog != nullptr ? _pCatalog->getRatingWindow() : 10 );
        if ( bRepeatedLine || _listSatisfaction.size() > ratingWindow || VectorUtil::findDuplicateBy<&RestaurantCustomer::_id>( _listCustomer ) != nullptr )
            return false;

        vector<uint8> listSeatTaken( static_cast<size_t>( MathUtil::max( 0, _settings._seatCount ) ), SW_FALSE );
        for ( const RestaurantCustomer& customer : _listCustomer )
        {
            const DishDef* pDish      = customer._dishID.empty() ? nullptr : _pCatalog->findDish( customer._dishID );
            const bool     bDishKnown = customer._dishID.empty() || pDish != nullptr;
            const bool     bTypeKnown = _pCatalog->findCustomerType( customer._typeID ) != nullptr;
            // 품질은 그 요리의 최대를 넘지 못하고, 앉은 손님은 고른 요리가 있다(`placeOrder`).
            const bool bQualityValid = pDish != nullptr ? customer._quality <= pDish->getMaxQuality() : customer._quality == 0;
            const bool bSeated       = RestaurantSimulationInternal::isSeated( customer );
            if ( customer._id >= _nextCustomerID || bDishKnown == false || bTypeKnown == false || bQualityValid == false || ( bSeated && pDish == nullptr ) )
                return false;
            if ( bSeated == false )
                continue;
            uint8& bTaken = listSeatTaken[static_cast<size_t>( customer._seat )];
            if ( bTaken != SW_FALSE )
                return false; // 앉은 손님 둘이 한 자리에 있으면 그 자리가 비어도 빈 줄 모른다
            bTaken = SW_TRUE;
        }
        for ( const StaffMember& staff : _listStaff )
        {
            // 맡은 일이 있는 직원은 고용 중이고 그 손님이 있다(`fireStaff` 가 풀어 준다). 레벨은 곡선 안이다.
            const bool bTaskValid  = staff._taskCustomer < 0 || ( staff._bHired != SW_FALSE && findCustomerIndex( staff._taskCustomer ).has_value() );
            const bool bLevelValid = _pStaffCurve == nullptr || staff._level.getLevel() <= _pStaffCurve->getMaxLevel();
            if ( bTaskValid == false || bLevelValid == false )
                return false;
        }
        for ( const KitchenOrder& order : _listOrder )
        {
            // 주문은 요리를 기다리는 손님의 것이다. 계산이 끝난 손님의 주문은 나르면 먹기로 되돌아가 또 계산한다.
            const optional<int32> customerIndex = findCustomerIndex( order._customerID );
            if ( customerIndex.has_value() == false || _listCustomer[static_cast<size_t>( *customerIndex )]._state != CustomerState::WaitingFood )
                return false;
            const DishDef* pDish = _pCatalog->findDish( order._dishID );
            if ( pDish == nullptr || order._quality > pDish->getMaxQuality() )
                return false;
            if ( order._cook < 0 )
                continue;
            const StaffMember& cook = _listStaff[static_cast<size_t>( order._cook )];
            if ( cook._bHired == SW_FALSE || cook._role != StaffRole::Cook )
                return false;
        }
        return true;
    }

    bool RestaurantSimulation::canServe( const hashed_string& dishID ) const
    {
        const MenuEntry* pEntry           = findMenuEntry( dishID );
        const DishDef*   pDish            = _pCatalog != nullptr ? _pCatalog->findDish( dishID ) : nullptr;
        const bool       bDishUnavailable = pEntry == nullptr || pEntry->_bOnMenu == SW_FALSE || pDish == nullptr;
        if ( bDishUnavailable )
            return false;
        const RecipeDef* pRecipe = findRecipe( *pDish );
        if ( pRecipe == nullptr )
            return false;
        if ( pRecipe->_station.empty() == false && countStations( pRecipe->_station ) <= 0 )
            return false;
        return _crafter.evaluate( pRecipe->_id, *_pPantry, pRecipe->_station, findBestCookLevel() ) == CraftResult::Ok;
    }

    int64 RestaurantSimulation::getMenuPrice( const hashed_string& dishID ) const
    {
        const MenuEntry* pEntry = findMenuEntry( dishID );
        return pEntry != nullptr ? pEntry->_price : 0;
    }

    float32 RestaurantSimulation::computePriceDemandScale() const
    {
        float32 ratioSum   = 0.0f;
        int32   entryCount = 0;
        for ( const MenuEntry& entry : _listMenu )
        {
            const DishDef* pDish = _pCatalog != nullptr ? _pCatalog->findDish( entry._dishID ) : nullptr;
            if ( entry._bOnMenu == SW_FALSE || pDish == nullptr )
                continue;
            ratioSum += static_cast<float32>( entry._price ) / static_cast<float32>( pDish->_basePrice );
            ++entryCount;
        }
        if ( entryCount == 0 )
            return 0.0f;
        const float32 averageRatio = ratioSum / static_cast<float32>( entryCount );
        return MathUtil::clamp( 1.0f - _settings._priceElasticity * ( averageRatio - 1.0f ), 0.1f, 2.0f );
    }

    float32 RestaurantSimulation::computeArrivalRate() const
    {
        if ( _pCatalog == nullptr )
            return 0.0f;
        const int32   hour            = _pCatalog->getOpenHour() + static_cast<int32>( _minutes / RestaurantSimulationInternal::kMinutesPerHour );
        const float32 reputationScale = MathUtil::max( 0.0f, 1.0f + static_cast<float32>( getReputation() ) * _settings._reputationArrivalScale );
        return _pCatalog->getArrivalRate( hour ) * reputationScale * _pCatalog->findWeatherScale( _weatherID ) * computePriceDemandScale();
    }

    float32 RestaurantSimulation::computeRating() const
    {
        if ( _listSatisfaction.empty() )
            return 0.0f;
        float32 total = 0.0f;
        for ( const float32 satisfaction : _listSatisfaction )
        {
            total += satisfaction;
        }
        return 1.0f + 4.0f * total / static_cast<float32>( _listSatisfaction.size() );
    }

    int32 RestaurantSimulation::countCooking() const
    {
        int32 count = 0;
        for ( const KitchenOrder& order : _listOrder )
        {
            if ( order._cook >= 0 && order._bReady == SW_FALSE )
                ++count;
        }
        return count;
    }

    int32 RestaurantSimulation::countOccupiedSeats() const
    {
        int32 count = 0;
        for ( const RestaurantCustomer& customer : _listCustomer )
        {
            if ( RestaurantSimulationInternal::isSeated( customer ) )
                ++count;
        }
        return count;
    }

    void RestaurantSimulation::stepFixed()
    {
        finishStaffWork();
        updatePatience();
        if ( _bOpen != SW_FALSE && _minutes < static_cast<float32>( _pCatalog->getCloseHour() - _pCatalog->getOpenHour() ) * RestaurantSimulationInternal::kMinutesPerHour )
            spawnArrivals();
        seatCustomers();
        startCooking();
        assignServers();
        assignCheckouts();
        _minutes += kStepMinutes;
    }

    void RestaurantSimulation::finishStaffWork()
    {
        // 조리: 주문의 남은 시간이 요리사의 시계이다.
        for ( KitchenOrder& order : _listOrder )
        {
            if ( order._cook < 0 || order._bReady != SW_FALSE )
                continue;
            StaffMember& cook = _listStaff[static_cast<size_t>( order._cook )];
            order._remaining -= kStepMinutes;
            cook._busyMinutes = MathUtil::max( 0.0f, order._remaining );
            if ( order._remaining > 0.0f )
                continue;
            order._bReady      = SW_TRUE;
            cook._taskCustomer = constant::invalid_index::kInt32;
            _eventBuffer.push( RestaurantEvent{ order._dishID, order._quality, order._customerID, RestaurantEvent::Kind::DishCooked } );
            grantXP( cook, _settings._cookXP );
            order._cook = constant::invalid_index::kInt32;
        }

        // 손님이 먹는다.
        for ( RestaurantCustomer& customer : _listCustomer )
        {
            if ( customer._state != CustomerState::Eating )
                continue;
            customer._eatRemaining -= kStepMinutes;
            if ( customer._eatRemaining <= 0.0f )
                customer._state = CustomerState::WaitingCheckout;
        }

        // 서빙과 계산.
        for ( StaffMember& staff : _listStaff )
        {
            const bool bNoPendingStaffTask = staff._bHired == SW_FALSE || staff._role == StaffRole::Cook || staff._taskCustomer < 0;
            if ( bNoPendingStaffTask )
                continue;
            staff._busyMinutes -= kStepMinutes;
            if ( staff._busyMinutes > 0.0f )
                continue;
            const optional<int32> customerIndex = findCustomerIndex( staff._taskCustomer );
            staff._taskCustomer                 = constant::invalid_index::kInt32;
            staff._busyMinutes                  = 0.0f;
            if ( customerIndex.has_value() == false )
                continue;
            RestaurantCustomer& customer = _listCustomer[static_cast<size_t>( *customerIndex )];
            if ( customer._bServing != SW_FALSE )
            {
                customer._bServing = SW_FALSE;
                for ( auto orderIter = _listOrder.begin(); orderIter != _listOrder.end(); ++orderIter )
                {
                    if ( orderIter->_customerID == customer._id && orderIter->_bReady != SW_FALSE )
                    {
                        customer._quality = orderIter->_quality;
                        _listOrder.erase( orderIter );
                        break;
                    }
                }
                const CustomerTypeDef* pType = _pCatalog->findCustomerType( customer._typeID );
                customer._eatRemaining       = pType != nullptr ? pType->_eatMinutes : 0.0f;
                customer._state              = customer._eatRemaining > 0.0f ? CustomerState::Eating : CustomerState::WaitingCheckout;
                grantXP( staff, _settings._serveXP );
            }
            else if ( customer._bCheckingOut != SW_FALSE )
            {
                customer._bCheckingOut = SW_FALSE;
                settlePayment( customer );
                grantXP( staff, _settings._checkoutXP );
            }
        }
    }

    void RestaurantSimulation::updatePatience()
    {
        for ( RestaurantCustomer& customer : _listCustomer )
        {
            const bool bWaiting = customer._state == CustomerState::Queued || ( customer._state == CustomerState::WaitingFood && customer._bServing == SW_FALSE );
            if ( bWaiting == false )
                continue;
            customer._waited += kStepMinutes;
            if ( customer._waited > customer._patience )
                leaveCustomer( customer, true );
        }
    }

    void RestaurantSimulation::spawnArrivals()
    {
        const vector<CustomerTypeDef>& listType  = _pCatalog->getCustomerTypes();
        auto                           getWeight = []( const CustomerTypeDef& customerType )
        { return customerType._weight; };
        _arrival.add( computeArrivalRate() * kStepMinutes / RestaurantSimulationInternal::kMinutesPerHour );
        while ( _arrival.takeOne() )
        {
            const optional<int32> typeIndex = _random.pickWeightedIndex( listType, getWeight );
            if ( typeIndex.has_value() == false )
            {
                _arrival.reset(); // 올 수 있는 성향이 없다 — 쌓아 두지 않는다(누적은 1 아래라는 규칙을 지킨다)
                return;
            }
            (void)admitCustomer( listType[static_cast<size_t>( *typeIndex )]._id );
        }
    }

    void RestaurantSimulation::seatCustomers()
    {
        for ( RestaurantCustomer& customer : _listCustomer )
        {
            if ( customer._state != CustomerState::Queued )
                continue;
            int32 freeSeat = constant::invalid_index::kInt32;
            for ( int32 seat = 0; seat < _settings._seatCount && freeSeat < 0; ++seat )
            {
                bool bTaken = false;
                for ( const RestaurantCustomer& other : _listCustomer )
                {
                    if ( other._seat == seat && RestaurantSimulationInternal::isSeated( other ) )
                    {
                        bTaken = true;
                        break;
                    }
                }
                if ( bTaken == false )
                    freeSeat = seat;
            }
            if ( freeSeat < 0 )
                return; // 줄은 들어온 순서이므로, 앞 손님이 못 앉으면 뒤 손님도 못 앉는다
            customer._seat = freeSeat;
            if ( placeOrder( customer ) == false )
                leaveCustomer( customer, false );
        }
    }

    void RestaurantSimulation::startCooking()
    {
        for ( KitchenOrder& order : _listOrder )
        {
            if ( order._cook >= 0 || order._bReady != SW_FALSE )
                continue;
            if ( order._station.empty() == false && countCookingAt( order._station ) >= countStations( order._station ) )
                continue; // 그 스테이션이 다 찼으므로, 다른 스테이션의 주문은 먼저 시작해도 된다
            const optional<int32> foundCook = findIdleStaff( StaffRole::Cook );
            if ( foundCook.has_value() == false )
                return;
            const int32    cookIndex = *foundCook;
            StaffMember&   cook      = _listStaff[static_cast<size_t>( cookIndex )];
            const DishDef* pDish     = _pCatalog->findDish( order._dishID );
            const int32    level     = cook._level.getLevel();
            order._cook              = cookIndex;
            order._quality           = pDish != nullptr ? pDish->computeQuality( level ) : 1;
            if ( order._remaining <= 0.0f )
            {
                const RecipeDef* pRecipe   = pDish != nullptr ? findRecipe( *pDish ) : nullptr;
                const float32    timeScale = MathUtil::max( _settings._minCookTimeScale,
                                                            1.0f - _settings._cookSpeedPerLevel * static_cast<float32>( level - 1 ) );
                order._remaining           = MathUtil::max( kStepMinutes, ( pRecipe != nullptr ? pRecipe->_time : 0.0f ) * timeScale );
            }
            cook._busyMinutes  = order._remaining;
            cook._taskCustomer = order._customerID;
        }
    }

    void RestaurantSimulation::assignServers()
    {
        for ( const KitchenOrder& order : _listOrder )
        {
            if ( order._bReady == SW_FALSE )
                continue;
            const optional<int32> foundCustomer = findCustomerIndex( order._customerID );
            if ( foundCustomer.has_value() == false || _listCustomer[static_cast<size_t>( *foundCustomer )]._bServing != SW_FALSE )
                continue;
            const int32           customerIndex = *foundCustomer;
            const optional<int32> serverIndex   = findIdleStaff( StaffRole::Server );
            if ( serverIndex.has_value() == false )
                return;
            StaffMember& server                                           = _listStaff[static_cast<size_t>( *serverIndex )];
            server._taskCustomer                                          = order._customerID;
            server._busyMinutes                                           = _settings._serveMinutes;
            _listCustomer[static_cast<size_t>( customerIndex )]._bServing = SW_TRUE;
        }
    }

    void RestaurantSimulation::assignCheckouts()
    {
        for ( RestaurantCustomer& customer : _listCustomer )
        {
            if ( customer._state != CustomerState::WaitingCheckout || customer._bCheckingOut != SW_FALSE )
                continue;
            optional<int32> staffIndex = findIdleStaff( StaffRole::Cashier );
            if ( staffIndex.has_value() == false )
                staffIndex = findIdleStaff( StaffRole::Server );
            if ( staffIndex.has_value() == false )
                return;
            StaffMember& staff     = _listStaff[static_cast<size_t>( *staffIndex )];
            staff._taskCustomer    = customer._id;
            staff._busyMinutes     = _settings._checkoutMinutes;
            customer._bCheckingOut = SW_TRUE;
        }
    }

    bool RestaurantSimulation::placeOrder( RestaurantCustomer& customer )
    {
        const CustomerTypeDef* pType = _pCatalog->findCustomerType( customer._typeID );
        if ( pType == nullptr )
            return false;
        vector<RestaurantSimulationInternal::DishChoice> listChoice;
        for ( const MenuEntry& entry : _listMenu )
        {
            const DishDef* pDish = _pCatalog->findDish( entry._dishID );
            if ( pDish == nullptr || canServe( entry._dishID ) == false )
                continue;
            const float32 ratio = static_cast<float32>( entry._price ) / static_cast<float32>( pDish->_basePrice );
            if ( ratio > pType->_budget )
                continue; // 지불 의사를 넘는다
            float32 weight = computeDishDemand( *pDish, entry._price );
            if ( VectorUtil::contains( pType->_listCategory, pDish->_category ) )
                weight *= _settings._preferredWeight;
            if ( weight <= 0.0f )
                continue; // 고를 가중치가 없는 요리는 후보가 아니다. 모두 0 이면 시킬 것이 없어 나간다
            listChoice.push_back( RestaurantSimulationInternal::DishChoice{ pDish, weight } );
        }
        const optional<int32> pickIndex = _random.pickWeightedIndex( listChoice, &RestaurantSimulationInternal::getChoiceWeight );
        if ( pickIndex.has_value() == false )
            return false;
        const DishDef*   pDish   = listChoice[static_cast<size_t>( *pickIndex )]._pDish;
        const RecipeDef* pRecipe = findRecipe( *pDish );
        int64            cost    = 0;
        if ( pRecipe == nullptr || _stock.consumeItems( pRecipe->_inputs, 1, cost ) == false )
            return false;
        _today._ingredientCost += cost;
        customer._dishID = pDish->_id;
        customer._price  = getMenuPrice( pDish->_id );
        customer._state  = CustomerState::WaitingFood;
        KitchenOrder order;
        order._dishID     = pDish->_id;
        order._station    = pRecipe->_station;
        order._customerID = customer._id;
        _listOrder.push_back( order );
        _eventBuffer.push( RestaurantEvent{ pDish->_id, customer._price, customer._id, RestaurantEvent::Kind::OrderPlaced } );
        return true;
    }

    void RestaurantSimulation::settlePayment( RestaurantCustomer& customer )
    {
        const CustomerTypeDef* pType        = _pCatalog->findCustomerType( customer._typeID );
        const DishDef*         pDish        = _pCatalog->findDish( customer._dishID );
        const float32          quality      = pDish != nullptr ? static_cast<float32>( customer._quality ) / static_cast<float32>( pDish->getMaxQuality() ) : 0.0f;
        const float32          waitRatio    = MathUtil::saturate( customer._waited / MathUtil::max( 1.0f, customer._patience ) );
        const float32          tipRate      = pType != nullptr ? pType->_tipRate : 0.0f;
        const int64            tip          = static_cast<int64>( MathUtil::round( static_cast<float32>( customer._price ) * tipRate * quality * ( 1.0f - waitRatio ) ) );
        const float32          satisfaction = MathUtil::saturate( _settings._qualityWeight * quality + ( 1.0f - _settings._qualityWeight ) * ( 1.0f - waitRatio ) );
        _today._revenue += customer._price;
        _today._tips += tip;
        ++_today._served;
        if ( _pWallet != nullptr )
            _pWallet->add( getCurrency(), customer._price + tip );
        customer._state = CustomerState::Paid;
        customer._seat  = constant::invalid_index::kInt32;
        pushSatisfaction( satisfaction );
        const int32 reputationDelta = static_cast<int32>( MathUtil::round( ( satisfaction * 2.0f - 1.0f ) * static_cast<float32>( _settings._reputationPerServe ) ) );
        if ( reputationDelta != 0 )
            if ( _pReputation != nullptr )
                (void)_pReputation->changeValue( _settings._reputationFaction, reputationDelta );
        _eventBuffer.push( RestaurantEvent{ customer._dishID, customer._price + tip, customer._id, RestaurantEvent::Kind::CustomerPaid } );
    }

    void RestaurantSimulation::leaveCustomer( RestaurantCustomer& customer, bool bWalkout )
    {
        for ( auto orderIter = _listOrder.begin(); orderIter != _listOrder.end(); )
        {
            if ( orderIter->_customerID == customer._id )
                orderIter = _listOrder.erase( orderIter ); // 꺼낸 재료는 버린다(원가는 이미 반영했다)
            else
                ++orderIter;
        }
        for ( StaffMember& staff : _listStaff )
        {
            if ( staff._taskCustomer == customer._id )
            {
                staff._taskCustomer = constant::invalid_index::kInt32;
                staff._busyMinutes  = 0.0f;
            }
        }
        if ( bWalkout )
        {
            ++_today._walkouts;
            pushSatisfaction( 0.0f );
            if ( _pReputation != nullptr )
                (void)_pReputation->changeValue( _settings._reputationFaction, -_settings._walkoutPenalty );
        }
        else
        {
            ++_today._noChoice;
        }
        customer._state = CustomerState::Left;
        customer._seat  = constant::invalid_index::kInt32;
        _eventBuffer.push( RestaurantEvent{ customer._typeID, bWalkout ? 1 : 0, customer._id, RestaurantEvent::Kind::CustomerLeft } );
    }

    void RestaurantSimulation::pushSatisfaction( float32 satisfaction )
    {
        _listSatisfaction.push_back( satisfaction );
        const size_t window = static_cast<size_t>( _pCatalog != nullptr ? _pCatalog->getRatingWindow() : 10 );
        if ( _listSatisfaction.size() > window )
            _listSatisfaction.erase( _listSatisfaction.begin(), _listSatisfaction.begin() + static_cast<ptrdiff_t>( _listSatisfaction.size() - window ) );
    }

    void RestaurantSimulation::grantXP( StaffMember& staff, int64 amount )
    {
        if ( _pStaffCurve == nullptr || amount <= 0 )
            return;
        if ( staff._level.addXP( *_pStaffCurve, amount ) > 0 )
            _eventBuffer.push( RestaurantEvent{ staff._name, staff._level.getLevel(), nullopt, RestaurantEvent::Kind::StaffLevelUp } );
    }

    void RestaurantSimulation::rollMarketPrices()
    {
        if ( _pShopCatalog == nullptr )
            return;
        const vector<ShopDef>& listShop = _pShopCatalog->getAll();
        const int32            day      = _pClock != nullptr ? _pClock->getDay() : 0;
        for ( int32 shopIndex = 0; shopIndex < static_cast<int32>( listShop.size() ); ++shopIndex )
        {
            const float32 unit     = HashUtil::toUnitFloat( HashUtil::hashCoord( day, shopIndex, _settings._randomSeed ) );
            const float32 modifier = MathUtil::max( 0.1f, 1.0f + _settings._marketVolatility * ( unit * 2.0f - 1.0f ) );
            if ( _market.setPriceModifier( listShop[static_cast<size_t>( shopIndex )]._id, modifier, 1.0f ) == false )
                SW_LOG_ERROR_ID( "RestaurantSimulation.RestaurantSimulationMarketCouldNot", "RestaurantSimulation: the market could not price shop %#", static_cast<int32>( shopIndex ) );
        }
    }

    optional<int32> RestaurantSimulation::findCustomerIndex( int32 customerID ) const
    {
        for ( int32 customerIndex = 0; customerIndex < static_cast<int32>( _listCustomer.size() ); ++customerIndex )
        {
            if ( _listCustomer[static_cast<size_t>( customerIndex )]._id == customerID )
                return customerIndex;
        }
        return nullopt;
    }

    optional<int32> RestaurantSimulation::findIdleStaff( StaffRole role ) const
    {
        for ( int32 staffIndex = 0; staffIndex < static_cast<int32>( _listStaff.size() ); ++staffIndex )
        {
            const StaffMember& staff           = _listStaff[static_cast<size_t>( staffIndex )];
            const bool         bStaffAvailable = staff._bHired != SW_FALSE && staff._role == role && staff._taskCustomer < 0;
            if ( bStaffAvailable )
                return staffIndex;
        }
        return nullopt;
    }

    int32 RestaurantSimulation::findBestCookLevel() const
    {
        int32 bestLevel = 0;
        for ( const StaffMember& staff : _listStaff )
        {
            if ( staff._bHired != SW_FALSE && staff._role == StaffRole::Cook )
                bestLevel = MathUtil::max( bestLevel, staff._level.getLevel() );
        }
        return bestLevel;
    }

    int32 RestaurantSimulation::countStations( const hashed_string& station ) const
    {
        for ( const StationSlot& slot : _listStation )
        {
            if ( slot._station == station )
                return slot._count;
        }
        return 0;
    }

    int32 RestaurantSimulation::countCookingAt( const hashed_string& station ) const
    {
        int32 count = 0;
        for ( const KitchenOrder& order : _listOrder )
        {
            const bool bOrderCooking = order._cook >= 0 && order._bReady == SW_FALSE && order._station == station;
            if ( bOrderCooking )
                ++count;
        }
        return count;
    }

    const MenuEntry* RestaurantSimulation::findMenuEntry( const hashed_string& dishID ) const
    {
        return VectorUtil::findBy<&MenuEntry::_dishID>( _listMenu, dishID );
    }

    const RecipeDef* RestaurantSimulation::findRecipe( const DishDef& dish ) const
    {
        return _pRecipeCatalog != nullptr ? _pRecipeCatalog->find( dish._recipeID ) : nullptr;
    }

    float32 RestaurantSimulation::computeDishDemand( const DishDef& dish, int64 price ) const
    {
        const float32 ratio = static_cast<float32>( price ) / static_cast<float32>( dish._basePrice );
        return MathUtil::max( RestaurantSimulationInternal::kMinDemand, 1.0f - _settings._priceElasticity * ( ratio - 1.0f ) );
    }

    int32 RestaurantSimulation::getReputation() const
    {
        return _pReputation != nullptr ? _pReputation->getValue( _settings._reputationFaction ) : 0;
    }
} // namespace sw
