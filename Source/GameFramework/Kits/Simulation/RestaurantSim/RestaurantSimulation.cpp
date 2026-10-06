#include "pch.h"

#include "GameFramework/Kits/Simulation/RestaurantSim/RestaurantSimulation.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Framework/GameStateRefs.h"
#include "GameFramework/Base/Inventory/ItemCatalog.h"
#include "GameFramework/Base/Utility/StateArchiveUtil.h"
#include "GameFramework/Base/World/WorldClock.h"
#include "GameFramework/Kits/Simulation/RestaurantSim/RestaurantCatalog.h"

namespace sw
{
    SW_LOG_CALLER( "RestaurantSimulation" );

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

            static bool contains( const vector<hashed_string>& listId, const hashed_string& id )
            {
                for ( const hashed_string& entry : listId )
                {
                    if ( entry == id )
                        return true;
                }
                return false;
            }

            /** @brief 주문 후보 하나 — 요리와 그 손님의 고르기 가중치(양수만 담는다)입니다. */
            struct DishChoice
            {
                const DishDef* _pDish{ nullptr };
                float32        _weight{ 0.0f };
            };

            static float32 getChoiceWeight( const DishChoice& choice )
            {
                return choice._weight;
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
        , _eventBuffer{}
        , _listSpoilageScratch{}
        , _stock{}
        , _crafter{}
        , _market{}
        , _settings{}
        , _today{}
        , _stepTimer{ kStepMinutes, 24.0f * 60.0f }
        , _random{}
        , _weatherId{}
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
        , _nextCustomerId{ 1 }
        , _bOpen{ SW_FALSE }
    {
    }

    void RestaurantSimulation::initialize( const RestaurantCatalog* pCatalog, const RecipeCatalog* pRecipeCatalog, const ItemCatalog* pItemCatalog,
                                           const ShopCatalog* pShopCatalog, const ExperienceCurve* pStaffCurve,
                                           const GameStateRefs& refs, Inventory& pantry, const RestaurantSettings& settings )
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
                _listMenu.push_back( MenuEntry{ dish._id, dish._basePrice, SW_TRUE } );
        }
        _today   = RestaurantDaySummary{};
        _minutes = 0.0f;
        _arrival.reset();
        _pendingSpoilageCost = 0;
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
        if ( _pWallet == nullptr )
            return ShopResult::NotEnoughMoney;
        int64            spent  = 0;
        const ShopResult result = _market.buy( shopId, itemId, count, *_pWallet, *_pPantry, &spent );
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
        _bOpen     = SW_TRUE;
        _weatherId = weatherId;
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
        _eventBuffer.push( RestaurantEvent{ typeId, 0, customer._id, RestaurantEvent::Kind::CustomerArrived } );
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
            if ( _pWallet == nullptr || _pWallet->trySpend( currency, staff._dailyWage ) == false )
            {
                const int64 balance = _pWallet != nullptr ? _pWallet->getBalance( currency ) : 0;
                if ( balance > 0 && _pWallet->trySpend( currency, balance ) == false )
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
        _listSpoilageScratch.clear();
        _stock.advanceDay( _listSpoilageScratch );
        for ( const IngredientSpoilage& spoilage : _listSpoilageScratch )
        {
            _pendingSpoilageCost += spoilage._cost;
            _eventBuffer.push( RestaurantEvent{ spoilage._itemId, spoilage._count, -1, RestaurantEvent::Kind::IngredientSpoiled } );
        }
        _market.advanceDay();
        rollMarketPrices();
    }

    void RestaurantSimulation::drainEvents( vector<RestaurantEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void RestaurantSimulation::writeState( Archive& outArchive ) const
    {
        outArchive << static_cast<uint32>( _listStaff.size() );
        for ( const StaffMember& staff : _listStaff )
        {
            StateArchiveUtil::writeName( outArchive, staff._name );
            staff._level.writeState( outArchive );
            outArchive << staff._busyMinutes;
            outArchive << staff._dailyWage;
            outArchive << staff._taskCustomer;
            outArchive << static_cast<uint8>( staff._role );
            outArchive << staff._bHired;
        }
        outArchive << static_cast<uint32>( _listCustomer.size() );
        for ( const RestaurantCustomer& customer : _listCustomer )
        {
            StateArchiveUtil::writeName( outArchive, customer._typeId );
            StateArchiveUtil::writeName( outArchive, customer._dishId );
            outArchive << customer._waited;
            outArchive << customer._patience;
            outArchive << customer._eatRemaining;
            outArchive << customer._price;
            outArchive << customer._id;
            outArchive << customer._seat;
            outArchive << customer._quality;
            outArchive << static_cast<uint8>( customer._state );
            outArchive << customer._bServing;
            outArchive << customer._bCheckingOut;
        }
        outArchive << static_cast<uint32>( _listOrder.size() );
        for ( const KitchenOrder& order : _listOrder )
        {
            StateArchiveUtil::writeName( outArchive, order._dishId );
            StateArchiveUtil::writeName( outArchive, order._station );
            outArchive << order._remaining;
            outArchive << order._customerId;
            outArchive << order._cook;
            outArchive << order._quality;
            outArchive << order._bReady;
        }
        outArchive << static_cast<uint32>( _listMenu.size() );
        for ( const MenuEntry& entry : _listMenu )
        {
            StateArchiveUtil::writeName( outArchive, entry._dishId );
            outArchive << entry._price;
            outArchive << entry._bOnMenu;
        }
        outArchive << static_cast<uint32>( _listStation.size() );
        for ( const StationSlot& slot : _listStation )
        {
            StateArchiveUtil::writeName( outArchive, slot._station );
            outArchive << slot._count;
        }
        outArchive << static_cast<uint32>( _listSatisfaction.size() );
        for ( const float32 satisfaction : _listSatisfaction )
        {
            outArchive << satisfaction;
        }
        _stock.writeState( outArchive );
        _crafter.writeState( outArchive );
        _market.writeState( outArchive );
        outArchive << _today._revenue;
        outArchive << _today._tips;
        outArchive << _today._ingredientCost;
        outArchive << _today._wages;
        outArchive << _today._spoilageCost;
        outArchive << _today._profit;
        outArchive << _today._rating;
        outArchive << _today._arrivals;
        outArchive << _today._served;
        outArchive << _today._walkouts;
        outArchive << _today._noChoice;
        outArchive << _today._unservedAtClose;
        StateArchiveUtil::writeStepTimer( outArchive, _stepTimer );
        StateArchiveUtil::writeRandom( outArchive, _random );
        StateArchiveUtil::writeName( outArchive, _weatherId );
        outArchive << _minutes;
        StateArchiveUtil::writeRateAccumulator( outArchive, _arrival );
        outArchive << _pendingSpoilageCost;
        outArchive << _nextCustomerId;
        outArchive << _bOpen;
    }

    bool RestaurantSimulation::readState( Archive& archive )
    {
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 카탈로그 · 설정 · 빌린 포인터는 사본이 그대로 든다.
        RestaurantSimulation restored = *this;
        uint32               count    = 0;
        // 직원마다 이름(4) + 레벨(경험치 · 누적 16 + 레벨 4) + 바쁨(4) + 일당(8) + 맡은 손님(4) + 역할 · 고용(2)
        if ( StateArchiveUtil::readCount( archive, 42, count ) == false )
            return false;
        restored._listStaff.assign( count, StaffMember{} );
        for ( StaffMember& staff : restored._listStaff )
        {
            uint8 role = 0;
            if ( StateArchiveUtil::readName( archive, staff._name ) == false || staff._level.readState( archive ) == false )
                return false;
            archive >> staff._busyMinutes;
            archive >> staff._dailyWage;
            archive >> staff._taskCustomer;
            archive >> role;
            archive >> staff._bHired;
            if ( archive.isError() || role > static_cast<uint8>( StaffRole::Cashier ) || staff._bHired > SW_TRUE )
                return false;
            staff._role = static_cast<StaffRole>( role );
        }

        // 손님마다 이름 둘(8) + 기다림 · 인내 · 먹기(12) + 값(8) + 번호 · 자리 · 품질(12) + 상태 · 나름 · 계산(3)
        if ( StateArchiveUtil::readCount( archive, 43, count ) == false )
            return false;
        restored._listCustomer.assign( count, RestaurantCustomer{} );
        for ( RestaurantCustomer& customer : restored._listCustomer )
        {
            uint8 state = 0;
            if ( StateArchiveUtil::readName( archive, customer._typeId ) == false || StateArchiveUtil::readName( archive, customer._dishId ) == false )
                return false;
            archive >> customer._waited;
            archive >> customer._patience;
            archive >> customer._eatRemaining;
            archive >> customer._price;
            archive >> customer._id;
            archive >> customer._seat;
            archive >> customer._quality;
            archive >> state;
            archive >> customer._bServing;
            archive >> customer._bCheckingOut;
            const bool bValid = archive.isOk() && state <= static_cast<uint8>( CustomerState::Left ) && -1 <= customer._seat &&
                                customer._seat < _settings._seatCount && customer._bServing <= SW_TRUE && customer._bCheckingOut <= SW_TRUE;
            if ( bValid == false )
                return false;
            customer._state = static_cast<CustomerState>( state );
        }

        // 주문마다 이름 둘(8) + 남은 시간 · 손님 · 요리사 · 품질(16) + 준비(1)
        if ( StateArchiveUtil::readCount( archive, 25, count ) == false )
            return false;
        restored._listOrder.clear();
        for ( uint32 index = 0; index < count; ++index )
        {
            KitchenOrder order;
            if ( StateArchiveUtil::readName( archive, order._dishId ) == false || StateArchiveUtil::readName( archive, order._station ) == false )
                return false;
            archive >> order._remaining;
            archive >> order._customerId;
            archive >> order._cook;
            archive >> order._quality;
            archive >> order._bReady;
            const bool bValid = archive.isOk() && -1 <= order._cook && order._cook < static_cast<int32>( restored._listStaff.size() ) && order._bReady <= SW_TRUE;
            if ( bValid == false )
                return false;
            restored._listOrder.push_back( order );
        }

        // 메뉴는 카탈로그 요리마다 하나다 — 이름(4) + 값(8) + 올림(1)
        if ( StateArchiveUtil::readCount( archive, 13, count ) == false || count != _listMenu.size() )
            return false;
        for ( MenuEntry& entry : restored._listMenu )
        {
            if ( StateArchiveUtil::readName( archive, entry._dishId ) == false )
                return false;
            archive >> entry._price;
            archive >> entry._bOnMenu;
            if ( archive.isError() || entry._bOnMenu > SW_TRUE )
                return false;
        }

        // 스테이션마다 이름(4) + 수(4)
        if ( StateArchiveUtil::readCount( archive, 8, count ) == false )
            return false;
        restored._listStation.assign( count, StationSlot{} );
        for ( StationSlot& slot : restored._listStation )
        {
            if ( StateArchiveUtil::readName( archive, slot._station ) == false )
                return false;
            archive >> slot._count;
        }
        if ( archive.isError() || StateArchiveUtil::readCount( archive, 4, count ) == false )
            return false;
        restored._listSatisfaction.assign( count, 0.0f );
        for ( float32& satisfaction : restored._listSatisfaction )
        {
            archive >> satisfaction;
        }

        const bool bPartRead = archive.isOk() && restored._stock.readState( archive ) && restored._crafter.readState( archive ) && restored._market.readState( archive );
        if ( bPartRead == false )
            return false;
        archive >> restored._today._revenue;
        archive >> restored._today._tips;
        archive >> restored._today._ingredientCost;
        archive >> restored._today._wages;
        archive >> restored._today._spoilageCost;
        archive >> restored._today._profit;
        archive >> restored._today._rating;
        archive >> restored._today._arrivals;
        archive >> restored._today._served;
        archive >> restored._today._walkouts;
        archive >> restored._today._noChoice;
        archive >> restored._today._unservedAtClose;
        const bool bTailRead = StateArchiveUtil::readStepTimer( archive, restored._stepTimer ) && StateArchiveUtil::readRandom( archive, restored._random ) &&
                               StateArchiveUtil::readName( archive, restored._weatherId );
        archive >> restored._minutes;
        const bool bArrivalRead = StateArchiveUtil::readRateAccumulator( archive, restored._arrival );
        archive >> restored._pendingSpoilageCost;
        archive >> restored._nextCustomerId;
        archive >> restored._bOpen;
        if ( bTailRead == false || bArrivalRead == false || archive.isError() || restored._bOpen > SW_TRUE )
            return false;

        // 직원이 맡은 손님 · 주문의 손님은 있는 손님이어야 한다.
        for ( const StaffMember& staff : restored._listStaff )
        {
            if ( staff._taskCustomer >= 0 && restored.findCustomerIndex( staff._taskCustomer ) < 0 )
                return false;
        }
        for ( const KitchenOrder& order : restored._listOrder )
        {
            if ( restored.findCustomerIndex( order._customerId ) < 0 )
                return false;
        }
        restored._eventBuffer.clear();
        restored._listSpoilageScratch.clear();
        *this = std::move( restored );
        return true;
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
        return _crafter.evaluate( pRecipe->_id, *_pPantry, pRecipe->_station, findBestCookLevel() ) == CraftResult::Ok;
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
            _eventBuffer.push( RestaurantEvent{ order._dishId, order._quality, order._customerId, RestaurantEvent::Kind::DishCooked } );
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
        _arrival.add( computeArrivalRate() * kStepMinutes / RestaurantSimulationInternal::kMinutesPerHour );
        while ( _arrival.takeOne() )
        {
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
                const float32    timeScale = MathUtil::max( _settings._minCookTimeScale,
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
        vector<RestaurantSimulationInternal::DishChoice> listChoice;
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
            if ( weight <= 0.0f )
                continue; // 고를 몫이 없는 요리는 후보가 아니다 — 모두 0 이면 시킬 것이 없어 나간다
            listChoice.push_back( RestaurantSimulationInternal::DishChoice{ pDish, weight } );
        }
        const int32 pickIndex = _random.pickWeightedIndex( listChoice, &RestaurantSimulationInternal::getChoiceWeight );
        if ( pickIndex < 0 )
            return false;
        const DishDef*   pDish   = listChoice[static_cast<size_t>( pickIndex )]._pDish;
        const RecipeDef* pRecipe = findRecipe( *pDish );
        int64            cost    = 0;
        if ( pRecipe == nullptr || _stock.consumeItems( pRecipe->_inputs, 1, cost ) == false )
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
        _eventBuffer.push( RestaurantEvent{ pDish->_id, customer._price, customer._id, RestaurantEvent::Kind::OrderPlaced } );
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
        if ( _pWallet != nullptr )
            _pWallet->add( getCurrency(), customer._price + tip );
        customer._state = CustomerState::Paid;
        customer._seat  = -1;
        pushSatisfaction( satisfaction );
        const int32 reputationDelta = static_cast<int32>( MathUtil::round( ( satisfaction * 2.0f - 1.0f ) * static_cast<float32>( _settings._reputationPerServe ) ) );
        if ( reputationDelta != 0 )
            if ( _pReputation != nullptr )
                (void)_pReputation->changeValue( _settings._reputationFaction, reputationDelta );
        _eventBuffer.push( RestaurantEvent{ customer._dishId, customer._price + tip, customer._id, RestaurantEvent::Kind::CustomerPaid } );
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
            if ( _pReputation != nullptr )
                (void)_pReputation->changeValue( _settings._reputationFaction, -_settings._walkoutPenalty );
        }
        else
        {
            ++_today._noChoice;
        }
        customer._state = CustomerState::Left;
        customer._seat  = -1;
        _eventBuffer.push( RestaurantEvent{ customer._typeId, bWalkout ? 1 : 0, customer._id, RestaurantEvent::Kind::CustomerLeft } );
    }

    void RestaurantSimulation::pushSatisfaction( float32 satisfaction )
    {
        _listSatisfaction.push_back( satisfaction );
        const size_t window = static_cast<size_t>( _pCatalog != nullptr ? _pCatalog->getRatingWindow() : 10 );
        if ( _listSatisfaction.size() > window )
            _listSatisfaction.erase( _listSatisfaction.begin(), _listSatisfaction.begin() + static_cast<ptrdiff_t>( _listSatisfaction.size() - window ) );
    }

    void RestaurantSimulation::grantXp( StaffMember& staff, int64 amount )
    {
        if ( _pStaffCurve == nullptr || amount <= 0 )
            return;
        if ( staff._level.addXp( *_pStaffCurve, amount ) > 0 )
            _eventBuffer.push( RestaurantEvent{ staff._name, staff._level.getLevel(), -1, RestaurantEvent::Kind::StaffLevelUp } );
    }

    void RestaurantSimulation::rollMarketPrices()
    {
        if ( _pShopCatalog == nullptr )
            return;
        const vector<ShopDef>& listShop = _pShopCatalog->getShops();
        const int32            day      = _pClock != nullptr ? _pClock->getDay() : 0;
        for ( int32 shopIndex = 0; shopIndex < static_cast<int32>( listShop.size() ); ++shopIndex )
        {
            const float32 unit     = GameHash::toUnitFloat( GameHash::hashCoord( day, shopIndex, _settings._randomSeed ) );
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

    int32 RestaurantSimulation::getReputation() const { return _pReputation != nullptr ? _pReputation->getValue( _settings._reputationFaction ) : 0; }
} // namespace sw
