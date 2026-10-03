#include "pch.h"

#include "GameFramework/Kits/Simulation/RestaurantSim/RestaurantSimulation.h"

#include "Core/Math/MathUtil.h"

#include "GameFramework/Inventory/ItemCatalog.h"
#include "GameFramework/Kits/Simulation/RestaurantSim/RestaurantCatalog.h"

namespace sw
{
    SW_LOG_CALLER( "RestaurantSimulation" );

    namespace
    {
        struct RestaurantSimulationInternal
        {
            static constexpr float32 kMinCookTimeScale = 0.3f;
            static constexpr float32 kMinDemand        = 0.05f;
            static constexpr float32 kMinutesPerHour   = 60.0f;

            static bool isSeated( const RestaurantCustomer& customer )
            {
                return customer._state == CustomerState::WaitingFood || customer._state == CustomerState::Eating || customer._state == CustomerState::WaitingCheckout;
            }

            static bool contains( const vector<hashed_string>& listId, const hashed_string& id )
            {
                for ( const hashed_string& entry : listId )
                {
                    if ( entry == id )
                        return true;
                }
                return false;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( StaffRole role )
    {
        switch ( role )
        {
            case StaffRole::Cook:
                return "Cook";
            case StaffRole::Server:
                return "Server";
            case StaffRole::Cashier:
                return "Cashier";
        }
        return "Unknown";
    }

    RestaurantSimulation::RestaurantSimulation()
        : _listStaff{}
        , _listCustomer{}
        , _listOrder{}
        , _listMenu{}
        , _listStation{}
        , _listSatisfaction{}
        , _listEvent{}
        , _listReputationScratch{}
        , _listSpoilageScratch{}
        , _inventory{}
        , _stock{}
        , _crafter{}
        , _market{}
        , _wallet{}
        , _reputation{}
        , _settings{}
        , _today{}
        , _stepTimer{ kStepMinutes, 24.0f * 60.0f }
        , _random{}
        , _weatherId{}
        , _pCatalog{ nullptr }
        , _pRecipeCatalog{ nullptr }
        , _pShopCatalog{ nullptr }
        , _pStaffCurve{ nullptr }
        , _minutes{ 0.0f }
        , _arrivalAccumulator{ 0.0f }
        , _pendingSpoilageCost{ 0 }
        , _day{ 0 }
        , _nextCustomerId{ 1 }
        , _bOpen{ SW_FALSE }
    {
    }

    void RestaurantSimulation::initialize( const RestaurantCatalog* pCatalog, const RecipeCatalog* pRecipeCatalog, const ItemCatalog* pItemCatalog,
                                           const ShopCatalog* pShopCatalog, const ReputationCatalog* pReputationCatalog, const ExperienceCurve* pStaffCurve,
                                           const RestaurantSettings& settings )
    {
        _pCatalog       = pCatalog;
        _pRecipeCatalog = pRecipeCatalog;
        _pShopCatalog   = pShopCatalog;
        _pStaffCurve    = pStaffCurve;
        _settings       = settings;
        _inventory.initialize( pItemCatalog, MathUtil::max( 1, settings._inventorySlots ) );
        _stock.initialize( &_inventory );
        _crafter.initialize( pRecipeCatalog );
        _market.initialize( pShopCatalog, pItemCatalog );
        _reputation.initialize( pReputationCatalog );
        _wallet.clear();
        _random.setSeed( settings._randomSeed );
        _stepTimer = FixedStepTimer( kStepMinutes, 24.0f * 60.0f );
        _listStaff.clear();
        _listCustomer.clear();
        _listOrder.clear();
        _listStation.clear();
        _listSatisfaction.clear();
        _listEvent.clear();
        _listMenu.clear();
        if ( pCatalog != nullptr )
        {
            for ( const DishDef& dish : pCatalog->getDishes() )
                _listMenu.push_back( MenuEntry{ dish._id, dish._basePrice, SW_TRUE } );
        }
        _today               = RestaurantDaySummary{};
        _minutes             = 0.0f;
        _arrivalAccumulator  = 0.0f;
        _pendingSpoilageCost = 0;
        _day                 = 0;
        _nextCustomerId      = 1;
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
        if ( staffIndex < 0 || staffIndex >= static_cast<int32>( _listStaff.size() ) || _listStaff[static_cast<size_t>( staffIndex )]._bHired == SW_FALSE )
            return false;
        StaffMember& staff = _listStaff[static_cast<size_t>( staffIndex )];
        for ( KitchenOrder& order : _listOrder )
        {
            if ( order._cook == staffIndex )
                order._cook = -1; // 남은 시간은 다음 요리사가 잇는다
        }
        const int32 customerIndex = findCustomerIndex( staff._taskCustomer );
        if ( customerIndex >= 0 && staff._role != StaffRole::Cook )
        {
            RestaurantCustomer& customer = _listCustomer[static_cast<size_t>( customerIndex )];
            customer._bServing           = SW_FALSE;
            customer._bCheckingOut       = SW_FALSE;
        }
        staff._bHired       = SW_FALSE;
        staff._taskCustomer = -1;
        staff._busyMinutes  = 0.0f;
        return true;
    }

    bool RestaurantSimulation::setMenuPrice( const hashed_string& dishId, int64 price )
    {
        for ( MenuEntry& entry : _listMenu )
        {
            if ( entry._dishId == dishId )
            {
                entry._price = MathUtil::max<int64>( 1, price );
                return true;
            }
        }
        return false;
    }

    bool RestaurantSimulation::setDishOnMenu( const hashed_string& dishId, bool bOnMenu )
    {
        for ( MenuEntry& entry : _listMenu )
        {
            if ( entry._dishId == dishId )
            {
                entry._bOnMenu = bOnMenu ? SW_TRUE : SW_FALSE;
                return true;
            }
        }
        return false;
    }

    ShopResult RestaurantSimulation::buyIngredient( const hashed_string& shopId, const hashed_string& itemId, int32 count )
    {
        int64            spent  = 0;
        const ShopResult result = _market.buy( shopId, itemId, count, _wallet, _inventory, &spent );
        if ( result != ShopResult::Ok )
            return result;
        const int32 shelfLife = _pCatalog != nullptr ? _pCatalog->findShelfLife( itemId ) : 0;
        _stock.recordBatch( itemId, count, shelfLife, spent / count );
        return ShopResult::Ok;
    }

    int32 RestaurantSimulation::addIngredient( const hashed_string& itemId, int32 count, int64 unitCost )
    {
        const int32 shelfLife = _pCatalog != nullptr ? _pCatalog->findShelfLife( itemId ) : 0;
        return _stock.addFresh( itemId, count, shelfLife, unitCost );
    }

    void RestaurantSimulation::openDay( const hashed_string& weatherId )
    {
        _bOpen               = SW_TRUE;
        _weatherId           = weatherId;
        _minutes             = 0.0f;
        _arrivalAccumulator  = 0.0f;
        _today               = RestaurantDaySummary{};
        _today._spoilageCost = _pendingSpoilageCost;
        _pendingSpoilageCost = 0;
        _stepTimer.reset();
        _listCustomer.clear();
        _listOrder.clear();
        for ( StaffMember& staff : _listStaff )
        {
            staff._busyMinutes  = 0.0f;
            staff._taskCustomer = -1;
        }
    }

    void RestaurantSimulation::update( float32 deltaMinutes )
    {
        if ( _pCatalog == nullptr )
            return;
        const int32 stepCount = _stepTimer.consume( deltaMinutes );
        for ( int32 stepIndex = 0; stepIndex < stepCount; ++stepIndex )
            stepFixed();
    }

    int32 RestaurantSimulation::admitCustomer( const hashed_string& typeId )
    {
        const CustomerTypeDef* pType = _pCatalog != nullptr ? _pCatalog->findCustomerType( typeId ) : nullptr;
        if ( pType == nullptr )
            return -1;
        RestaurantCustomer customer;
        customer._typeId   = typeId;
        customer._id       = _nextCustomerId++;
        customer._patience = pType->_patience;
        _listCustomer.push_back( customer );
        ++_today._arrivals;
        _listEvent.push_back( RestaurantEvent{ typeId, 0, customer._id, RestaurantEvent::Kind::CustomerArrived } );
        return customer._id;
    }

    void RestaurantSimulation::closeDay( RestaurantDaySummary& outSummary )
    {
        for ( RestaurantCustomer& customer : _listCustomer )
        {
            if ( customer._state == CustomerState::Eating || customer._state == CustomerState::WaitingCheckout )
            {
                settlePayment( customer ); // 다 먹은 셈 치고 낸다
            }
            else if ( customer._state == CustomerState::Queued || customer._state == CustomerState::WaitingFood )
            {
                customer._state = CustomerState::Left;
                customer._seat  = -1;
                ++_today._unservedAtClose;
            }
        }
        _listOrder.clear();
        const hashed_string currency = getCurrency();
        for ( StaffMember& staff : _listStaff )
        {
            staff._busyMinutes  = 0.0f;
            staff._taskCustomer = -1;
            if ( staff._bHired == SW_FALSE )
                continue;
            _today._wages += staff._dailyWage;
            if ( _wallet.trySpend( currency, staff._dailyWage ) == false )
            {
                const int64 balance = _wallet.getBalance( currency );
                if ( balance > 0 && _wallet.trySpend( currency, balance ) == false )
                    SW_LOG_WARNING( "could not pay the remaining balance %# to '%#'", balance, staff._name.c_str() );
                SW_LOG_WARNING( "wage for '%#' is short by %#", staff._name.c_str(), staff._dailyWage - balance );
            }
        }
        _today._profit = _today._revenue + _today._tips - _today._ingredientCost - _today._wages - _today._spoilageCost;
        _today._rating = computeRating();
        _bOpen         = SW_FALSE;
        outSummary     = _today;
    }

    void RestaurantSimulation::advanceDay()
    {
        ++_day;
        _listSpoilageScratch.clear();
        _stock.advanceDay( _listSpoilageScratch );
        for ( const IngredientSpoilage& spoilage : _listSpoilageScratch )
        {
            _pendingSpoilageCost += spoilage._cost;
            _listEvent.push_back( RestaurantEvent{ spoilage._itemId, spoilage._count, -1, RestaurantEvent::Kind::IngredientSpoiled } );
        }
        _market.advanceDay();
        rollMarketPrices();
        _reputation.advanceDay();
        _listReputationScratch.clear();
        _reputation.drainEvents( _listReputationScratch );
    }

    void RestaurantSimulation::drainEvents( vector<RestaurantEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    bool RestaurantSimulation::canServe( const hashed_string& dishId ) const
    {
        const MenuEntry* pEntry = findMenuEntry( dishId );
        const DishDef*   pDish  = _pCatalog != nullptr ? _pCatalog->findDish( dishId ) : nullptr;
        if ( pEntry == nullptr || pEntry->_bOnMenu == SW_FALSE || pDish == nullptr )
            return false;
        const RecipeDef* pRecipe = findRecipe( *pDish );
        if ( pRecipe == nullptr )
            return false;
        if ( pRecipe->_station.empty() == false && countStations( pRecipe->_station ) <= 0 )
            return false;
        return _crafter.evaluate( pRecipe->_id, _inventory, pRecipe->_station, findBestCookLevel() ) == CraftResult::Ok;
    }

    int64 RestaurantSimulation::getMenuPrice( const hashed_string& dishId ) const
    {
        const MenuEntry* pEntry = findMenuEntry( dishId );
        return pEntry != nullptr ? pEntry->_price : 0;
    }

    float32 RestaurantSimulation::computePriceDemandScale() const
    {
        float32 ratioSum   = 0.0f;
        int32   entryCount = 0;
        for ( const MenuEntry& entry : _listMenu )
        {
            const DishDef* pDish = _pCatalog != nullptr ? _pCatalog->findDish( entry._dishId ) : nullptr;
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
        return _pCatalog->getArrivalRate( hour ) * reputationScale * _pCatalog->findWeatherScale( _weatherId ) * computePriceDemandScale();
    }

    float32 RestaurantSimulation::computeRating() const
    {
        if ( _listSatisfaction.empty() )
            return 0.0f;
        float32 total = 0.0f;
        for ( const float32 satisfaction : _listSatisfaction )
            total += satisfaction;
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
        tickPatience();
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
        // 조리 — 주문의 남은 시간이 요리사의 시계다.
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
            cook._taskCustomer = -1;
            _listEvent.push_back( RestaurantEvent{ order._dishId, order._quality, order._customerId, RestaurantEvent::Kind::DishCooked } );
            grantXp( cook, _settings._cookXp );
            order._cook = -1;
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

        // 서빙 · 계산.
        for ( StaffMember& staff : _listStaff )
        {
            if ( staff._bHired == SW_FALSE || staff._role == StaffRole::Cook || staff._taskCustomer < 0 )
                continue;
            staff._busyMinutes -= kStepMinutes;
            if ( staff._busyMinutes > 0.0f )
                continue;
            const int32 customerIndex = findCustomerIndex( staff._taskCustomer );
            staff._taskCustomer       = -1;
            staff._busyMinutes        = 0.0f;
            if ( customerIndex < 0 )
                continue;
            RestaurantCustomer& customer = _listCustomer[static_cast<size_t>( customerIndex )];
            if ( customer._bServing != SW_FALSE )
            {
                customer._bServing = SW_FALSE;
                for ( auto orderIter = _listOrder.begin(); orderIter != _listOrder.end(); ++orderIter )
                {
                    if ( orderIter->_customerId == customer._id && orderIter->_bReady != SW_FALSE )
                    {
                        customer._quality = orderIter->_quality;
                        _listOrder.erase( orderIter );
                        break;
                    }
                }
                const CustomerTypeDef* pType = _pCatalog->findCustomerType( customer._typeId );
                customer._eatRemaining       = pType != nullptr ? pType->_eatMinutes : 0.0f;
                customer._state              = customer._eatRemaining > 0.0f ? CustomerState::Eating : CustomerState::WaitingCheckout;
                grantXp( staff, _settings._serveXp );
            }
            else if ( customer._bCheckingOut != SW_FALSE )
            {
                customer._bCheckingOut = SW_FALSE;
                settlePayment( customer );
                grantXp( staff, _settings._checkoutXp );
            }
        }
    }

    void RestaurantSimulation::tickPatience()
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
        _arrivalAccumulator += computeArrivalRate() * kStepMinutes / RestaurantSimulationInternal::kMinutesPerHour;
        while ( _arrivalAccumulator >= 1.0f )
        {
            _arrivalAccumulator -= 1.0f;
            const int32 typeIndex = _random.pickWeightedIndex( listType, getWeight );
            if ( typeIndex < 0 )
                return;
            (void)admitCustomer( listType[static_cast<size_t>( typeIndex )]._id );
        }
    }

    void RestaurantSimulation::seatCustomers()
    {
        for ( RestaurantCustomer& customer : _listCustomer )
        {
            if ( customer._state != CustomerState::Queued )
                continue;
            int32 freeSeat = -1;
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
                return; // 줄은 들어온 순서 — 앞 손님이 못 앉으면 뒤도 못 앉는다
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
                continue; // 그 스테이션이 다 찼다 — 다른 스테이션의 주문은 먼저 시작해도 된다
            const int32 cookIndex = findIdleStaff( StaffRole::Cook );
            if ( cookIndex < 0 )
                return;
            StaffMember&   cook  = _listStaff[static_cast<size_t>( cookIndex )];
            const DishDef* pDish = _pCatalog->findDish( order._dishId );
            const int32    level = cook._level.getLevel();
            order._cook          = cookIndex;
            order._quality       = pDish != nullptr ? pDish->computeQuality( level ) : 1;
            if ( order._remaining <= 0.0f )
            {
                const RecipeDef* pRecipe   = pDish != nullptr ? findRecipe( *pDish ) : nullptr;
                const float32    timeScale = MathUtil::max( RestaurantSimulationInternal::kMinCookTimeScale,
                                                            1.0f - _settings._cookSpeedPerLevel * static_cast<float32>( level - 1 ) );
                order._remaining           = MathUtil::max( kStepMinutes, ( pRecipe != nullptr ? pRecipe->_time : 0.0f ) * timeScale );
            }
            cook._busyMinutes  = order._remaining;
            cook._taskCustomer = order._customerId;
        }
    }

    void RestaurantSimulation::assignServers()
    {
        for ( const KitchenOrder& order : _listOrder )
        {
            if ( order._bReady == SW_FALSE )
                continue;
            const int32 customerIndex = findCustomerIndex( order._customerId );
            if ( customerIndex < 0 || _listCustomer[static_cast<size_t>( customerIndex )]._bServing != SW_FALSE )
                continue;
            const int32 serverIndex = findIdleStaff( StaffRole::Server );
            if ( serverIndex < 0 )
                return;
            StaffMember& server                                           = _listStaff[static_cast<size_t>( serverIndex )];
            server._taskCustomer                                          = order._customerId;
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
            int32 staffIndex = findIdleStaff( StaffRole::Cashier );
            if ( staffIndex < 0 )
                staffIndex = findIdleStaff( StaffRole::Server );
            if ( staffIndex < 0 )
                return;
            StaffMember& staff     = _listStaff[static_cast<size_t>( staffIndex )];
            staff._taskCustomer    = customer._id;
            staff._busyMinutes     = _settings._checkoutMinutes;
            customer._bCheckingOut = SW_TRUE;
        }
    }

    bool RestaurantSimulation::placeOrder( RestaurantCustomer& customer )
    {
        const CustomerTypeDef* pType = _pCatalog->findCustomerType( customer._typeId );
        if ( pType == nullptr )
            return false;
        vector<const DishDef*> listCandidate;
        vector<float32>        listWeight;
        for ( const MenuEntry& entry : _listMenu )
        {
            const DishDef* pDish = _pCatalog->findDish( entry._dishId );
            if ( pDish == nullptr || canServe( entry._dishId ) == false )
                continue;
            const float32 ratio = static_cast<float32>( entry._price ) / static_cast<float32>( pDish->_basePrice );
            if ( ratio > pType->_budget )
                continue; // 지불 의사를 넘는다
            float32 weight = computeDishDemand( *pDish, entry._price );
            if ( RestaurantSimulationInternal::contains( pType->_listCategory, pDish->_category ) )
                weight *= _settings._preferredWeight;
            listCandidate.push_back( pDish );
            listWeight.push_back( weight );
        }
        if ( listCandidate.empty() )
            return false;
        const int32      pickIndex = _random.pickWeightedIndex( listWeight, []( float32 weight )
             { return weight; } );
        const DishDef*   pDish     = pickIndex >= 0 ? listCandidate[static_cast<size_t>( pickIndex )] : listCandidate.back();
        const RecipeDef* pRecipe   = findRecipe( *pDish );
        int64            cost      = 0;
        if ( pRecipe == nullptr || _stock.consumeBag( pRecipe->_inputs, 1, cost ) == false )
            return false;
        _today._ingredientCost += cost;
        customer._dishId = pDish->_id;
        customer._price  = getMenuPrice( pDish->_id );
        customer._state  = CustomerState::WaitingFood;
        KitchenOrder order;
        order._dishId     = pDish->_id;
        order._station    = pRecipe->_station;
        order._customerId = customer._id;
        _listOrder.push_back( order );
        _listEvent.push_back( RestaurantEvent{ pDish->_id, customer._price, customer._id, RestaurantEvent::Kind::OrderPlaced } );
        return true;
    }

    void RestaurantSimulation::settlePayment( RestaurantCustomer& customer )
    {
        const CustomerTypeDef* pType        = _pCatalog->findCustomerType( customer._typeId );
        const DishDef*         pDish        = _pCatalog->findDish( customer._dishId );
        const float32          quality      = pDish != nullptr ? static_cast<float32>( customer._quality ) / static_cast<float32>( pDish->getMaxQuality() ) : 0.0f;
        const float32          waitRatio    = MathUtil::saturate( customer._waited / MathUtil::max( 1.0f, customer._patience ) );
        const float32          tipRate      = pType != nullptr ? pType->_tipRate : 0.0f;
        const int64            tip          = static_cast<int64>( MathUtil::round( static_cast<float32>( customer._price ) * tipRate * quality * ( 1.0f - waitRatio ) ) );
        const float32          satisfaction = MathUtil::saturate( _settings._qualityWeight * quality + ( 1.0f - _settings._qualityWeight ) * ( 1.0f - waitRatio ) );
        _today._revenue += customer._price;
        _today._tips += tip;
        ++_today._served;
        _wallet.add( getCurrency(), customer._price + tip );
        customer._state = CustomerState::Paid;
        customer._seat  = -1;
        pushSatisfaction( satisfaction );
        const int32 reputationDelta = static_cast<int32>( MathUtil::round( ( satisfaction * 2.0f - 1.0f ) * static_cast<float32>( _settings._reputationPerServe ) ) );
        if ( reputationDelta != 0 )
            (void)_reputation.changeValue( _settings._reputationFaction, reputationDelta );
        _listEvent.push_back( RestaurantEvent{ customer._dishId, customer._price + tip, customer._id, RestaurantEvent::Kind::CustomerPaid } );
    }

    void RestaurantSimulation::leaveCustomer( RestaurantCustomer& customer, bool bWalkout )
    {
        for ( auto orderIter = _listOrder.begin(); orderIter != _listOrder.end(); )
        {
            if ( orderIter->_customerId == customer._id )
                orderIter = _listOrder.erase( orderIter ); // 거둔 재료는 버린다(원가는 이미 들어갔다)
            else
                ++orderIter;
        }
        for ( StaffMember& staff : _listStaff )
        {
            if ( staff._taskCustomer == customer._id )
            {
                staff._taskCustomer = -1;
                staff._busyMinutes  = 0.0f;
            }
        }
        if ( bWalkout )
        {
            ++_today._walkouts;
            pushSatisfaction( 0.0f );
            (void)_reputation.changeValue( _settings._reputationFaction, -_settings._walkoutPenalty );
        }
        else
        {
            ++_today._noChoice;
        }
        customer._state = CustomerState::Left;
        customer._seat  = -1;
        _listEvent.push_back( RestaurantEvent{ customer._typeId, bWalkout ? 1 : 0, customer._id, RestaurantEvent::Kind::CustomerLeft } );
    }

    void RestaurantSimulation::pushSatisfaction( float32 satisfaction )
    {
        _listSatisfaction.push_back( satisfaction );
        const size_t window = static_cast<size_t>( _pCatalog != nullptr ? _pCatalog->getRatingWindow() : 10 );
        if ( _listSatisfaction.size() > window )
            _listSatisfaction.erase( _listSatisfaction.begin(), _listSatisfaction.begin() + static_cast<ptrdiff_t>( _listSatisfaction.size() - window ) );
        _listReputationScratch.clear();
        _reputation.drainEvents( _listReputationScratch ); // 단계 알림은 쓰지 않는다 — 쌓이지 않게 비운다
    }

    void RestaurantSimulation::grantXp( StaffMember& staff, int64 amount )
    {
        if ( _pStaffCurve == nullptr || amount <= 0 )
            return;
        if ( staff._level.addXp( *_pStaffCurve, amount ) > 0 )
            _listEvent.push_back( RestaurantEvent{ staff._name, staff._level.getLevel(), -1, RestaurantEvent::Kind::StaffLevelUp } );
    }

    void RestaurantSimulation::rollMarketPrices()
    {
        if ( _pShopCatalog == nullptr )
            return;
        const vector<ShopDef>& listShop = _pShopCatalog->getShops();
        for ( int32 shopIndex = 0; shopIndex < static_cast<int32>( listShop.size() ); ++shopIndex )
        {
            const float32 unit     = GameHash::toUnitFloat( GameHash::hashCoord( _day, shopIndex, _settings._randomSeed ) );
            const float32 modifier = MathUtil::max( 0.1f, 1.0f + _settings._marketVolatility * ( unit * 2.0f - 1.0f ) );
            _market.setPriceModifier( listShop[static_cast<size_t>( shopIndex )]._id, modifier, 1.0f );
        }
    }

    int32 RestaurantSimulation::findCustomerIndex( int32 customerId ) const
    {
        for ( int32 customerIndex = 0; customerIndex < static_cast<int32>( _listCustomer.size() ); ++customerIndex )
        {
            if ( _listCustomer[static_cast<size_t>( customerIndex )]._id == customerId )
                return customerIndex;
        }
        return -1;
    }

    int32 RestaurantSimulation::findIdleStaff( StaffRole role ) const
    {
        for ( int32 staffIndex = 0; staffIndex < static_cast<int32>( _listStaff.size() ); ++staffIndex )
        {
            const StaffMember& staff = _listStaff[static_cast<size_t>( staffIndex )];
            if ( staff._bHired != SW_FALSE && staff._role == role && staff._taskCustomer < 0 )
                return staffIndex;
        }
        return -1;
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
            if ( order._cook >= 0 && order._bReady == SW_FALSE && order._station == station )
                ++count;
        }
        return count;
    }

    const MenuEntry* RestaurantSimulation::findMenuEntry( const hashed_string& dishId ) const
    {
        for ( const MenuEntry& entry : _listMenu )
        {
            if ( entry._dishId == dishId )
                return &entry;
        }
        return nullptr;
    }

    const RecipeDef* RestaurantSimulation::findRecipe( const DishDef& dish ) const
    {
        return _pRecipeCatalog != nullptr ? _pRecipeCatalog->findRecipe( dish._recipeId ) : nullptr;
    }

    float32 RestaurantSimulation::computeDishDemand( const DishDef& dish, int64 price ) const
    {
        const float32 ratio = static_cast<float32>( price ) / static_cast<float32>( dish._basePrice );
        return MathUtil::max( RestaurantSimulationInternal::kMinDemand, 1.0f - _settings._priceElasticity * ( ratio - 1.0f ) );
    }
} // namespace sw
