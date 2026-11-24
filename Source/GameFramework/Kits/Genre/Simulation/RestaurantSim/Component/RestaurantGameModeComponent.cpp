#include "pch.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/Component/RestaurantGameModeComponent.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Encoding/FourCCUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Foundation/Framework/Minigame/MinigameStack.h"

namespace sw
{
    namespace
    {
        struct RestaurantGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "RSGM" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kWinResult      = "win";
            static constexpr const utf8* kLoseResult     = "lose";
            static constexpr const utf8* kCookName       = "cook"; ///< 직원 이름(레벨 오름 알림의 id)
            static constexpr const utf8* kServerName     = "server";
            static constexpr float32     kMinutesPerHour = 60.0f;
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( restaurantPhase, "Restaurant.Phase", "Restaurant phase (0 prepare, 1 open, 2 closed, 3 victory, 4 defeat)", RestaurantGameModeComponent,
                                &RestaurantGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( restaurantDay, "Restaurant.Day", "Business day (1 = first)", RestaurantGameModeComponent, &RestaurantGameModeComponent::getDay );
    SW_AUTOMATION_GETTER_WATCH( restaurantGold, "Restaurant.Gold", "Gold of the restaurant", RestaurantGameModeComponent, &RestaurantGameModeComponent::getGold );
    SW_AUTOMATION_GETTER_WATCH( restaurantServed, "Restaurant.Served", "Customers who paid today", RestaurantGameModeComponent, &RestaurantGameModeComponent::getServed );
    SW_AUTOMATION_GETTER_WATCH( restaurantTotalProfit, "Restaurant.TotalProfit", "Profit of the closed days", RestaurantGameModeComponent, &RestaurantGameModeComponent::getTotalProfit );
    SW_AUTOMATION_GETTER_WATCH( restaurantMenuPrice, "Restaurant.MenuPrice", "Menu price of the first dish", RestaurantGameModeComponent, &RestaurantGameModeComponent::getFirstMenuPrice );

    RestaurantGameModeComponent::RestaurantGameModeComponent()
        : _shops{}
        , _shop{}
        , _listIngredient{}
        , _buyCount{ 5 }
        , _pantrySlotCount{ 40 }
        , _startGold{ 200 }
        , _cookCount{ 1 }
        , _serverCount{ 1 }
        , _dailyWage{ 15 }
        , _settings{}
        , _weather{}
        , _priceStep{ 5 }
        , _minutesPerSecond{ 10.0f }
        , _dayCount{ 2 }
        , _targetProfit{ 100 }
        , _resultHoldSeconds{ 3.0f }
        , _pCatalog{}
        , _pRecipes{}
        , _pShops{}
        , _wallet{}
        , _pantry{}
        , _simulation{}
        , _listEvent{}
        , _resultHold{}
        , _totalProfit{ 0 }
        , _day{ 1 }
        , _phase{ RestaurantPhase::Prepare }
    {
    }

    RestaurantGameModeComponent::~RestaurantGameModeComponent() = default;

    void RestaurantGameModeComponent::initializeSimulation( RestaurantSimulation& outSimulation )
    {
        // 평판 · 시계 · 아이템 카탈로그 · 직원 경험치 곡선은 쓰지 않는 판이다(nullptr 이면 끈다).
        outSimulation.initialize( _pCatalog.get(), _pRecipes.get(), nullptr, _pShops.get(), nullptr, &_wallet, nullptr, nullptr, _pantry, _settings );
    }

    bool RestaurantGameModeComponent::startGame()
    {
        if ( acquireBoundCatalog( _pCatalog ) == false || acquireBoundCatalog( _pRecipes ) == false || acquireRequired( _shops.path(), _pShops ) == false )
            return false;
        if ( _listIngredient.empty() || _pShops->find( _shop ) == nullptr || _pCatalog->getDishes().empty() )
        {
            SW_LOG_ERROR_ID( "Restaurant.SetupIncomplete", "Restaurant game mode: ingredients (%#), dishes (%#) and a known shop ('%#') are all required - the restaurant does not open", _listIngredient.size(),
                             _pCatalog->getDishes().size(), _shop.c_str() );
            return false;
        }
        if ( _pantry.initialize( nullptr, _pantrySlotCount ) == false )
            return false;
        _wallet.clear();
        if ( _startGold > 0 )
            _wallet.add( Wallet::getDefaultCurrency(), _startGold );
        _simulation = RestaurantSimulation{};
        initializeSimulation( _simulation );
        for ( int32 cookIndex = 0; cookIndex < _cookCount; ++cookIndex )
        {
            (void)_simulation.hireStaff( hashed_string( RestaurantGameModeComponentInternal::kCookName ), StaffRole::Cook, _dailyWage ); // 번호는 쓰지 않는다
        }
        for ( int32 serverIndex = 0; serverIndex < _serverCount; ++serverIndex )
        {
            (void)_simulation.hireStaff( hashed_string( RestaurantGameModeComponentInternal::kServerName ), StaffRole::Server, _dailyWage );
        }
        _resultHold.clear();
        return true;
    }

    void RestaurantGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "Restaurant.Started", "[Restaurant] %# gold, %# days to make %# profit - the input arrives as commands (buy, price, open, next day)", getGold(), _dayCount, _targetProfit );
    }

    int32 RestaurantGameModeComponent::getGold() const
    {
        return static_cast<int32>( _wallet.getBalance( Wallet::getDefaultCurrency() ) );
    }

    int32 RestaurantGameModeComponent::getFirstMenuPrice() const
    {
        if ( isStarted() == false || _pCatalog->getDishes().empty() )
            return 0;
        return static_cast<int32>( _simulation.getMenuPrice( _pCatalog->getDishes().front()._id ) );
    }

    bool RestaurantGameModeComponent::submitBuy( int32 ingredientIndex )
    {
        const bool bMarketOpen = isStarted() && ( _phase == RestaurantPhase::Prepare || _phase == RestaurantPhase::Open );
        if ( bMarketOpen == false || ingredientIndex < 0 || ingredientIndex >= static_cast<int32>( _listIngredient.size() ) )
            return false;
        const hashed_string& itemID = _listIngredient[static_cast<size_t>( ingredientIndex )];
        const ShopResult     result = _simulation.buyIngredient( _shop, itemID, _buyCount );
        if ( result != ShopResult::Ok )
        {
            SW_LOG_INFO_ID( "Restaurant.BuyRefused", "[Restaurant] could not buy %# '%#' (result %#)", _buyCount, itemID.c_str(), static_cast<int32>( result ) );
            return false;
        }
        SW_LOG_INFO_ID( "Restaurant.Bought", "[Restaurant] bought %# '%#', %# gold left", _buyCount, itemID.c_str(), getGold() );
        return true;
    }

    bool RestaurantGameModeComponent::submitPriceChange( int32 dishIndex, int32 steps )
    {
        const bool bMenuOpen = isStarted() && _phase == RestaurantPhase::Prepare;
        if ( bMenuOpen == false || dishIndex < 0 || dishIndex >= static_cast<int32>( _pCatalog->getDishes().size() ) )
            return false;
        const hashed_string& dishID = _pCatalog->getDishes()[static_cast<size_t>( dishIndex )]._id;
        return _simulation.setMenuPrice( dishID, _simulation.getMenuPrice( dishID ) + static_cast<int64>( steps ) * _priceStep );
    }

    bool RestaurantGameModeComponent::submitOpen()
    {
        if ( isStarted() == false || _phase != RestaurantPhase::Prepare )
            return false;
        _simulation.openDay( _weather );
        _phase = RestaurantPhase::Open;
        SW_LOG_INFO_ID( "Restaurant.Opened", "[Restaurant] day %# is open", _day );
        return true;
    }

    bool RestaurantGameModeComponent::submitNextDay()
    {
        if ( isStarted() == false || _phase != RestaurantPhase::Closed )
            return false;
        _simulation.advanceDay();
        reportEvents();
        ++_day;
        _phase = RestaurantPhase::Prepare;
        return true;
    }

    bool RestaurantGameModeComponent::isDayOver() const
    {
        const float32 openMinutes = static_cast<float32>( _pCatalog->getCloseHour() - _pCatalog->getOpenHour() ) * RestaurantGameModeComponentInternal::kMinutesPerHour;
        if ( _simulation.getMinutes() < openMinutes )
            return false;
        return VectorUtil::containsIf( _simulation.getCustomers(), []( const RestaurantCustomer& customer )
        { return customer._state != CustomerState::Paid && customer._state != CustomerState::Left; } ) == false;
    }

    void RestaurantGameModeComponent::closeDay()
    {
        RestaurantDaySummary summary;
        _simulation.closeDay( summary );
        _totalProfit += summary._profit;
        SW_LOG_INFO_ID( "Restaurant.DayClosed", "[Restaurant] day %#: served %#, revenue %#, profit %# (total %#)", _day, summary._served, summary._revenue, summary._profit, _totalProfit );
        if ( _day < _dayCount )
        {
            _phase = RestaurantPhase::Closed;
            return;
        }
        _resultHold.start( _resultHoldSeconds );
        if ( _targetProfit <= _totalProfit )
        {
            _phase = RestaurantPhase::Victory;
            SW_LOG_INFO_ID( "Restaurant.Victory", "[Restaurant] the target %# is reached", _targetProfit );
        }
        else
        {
            _phase = RestaurantPhase::Defeat;
            SW_LOG_INFO_ID( "Restaurant.Defeat", "[Restaurant] the target %# is not reached", _targetProfit );
        }
    }

    void RestaurantGameModeComponent::reportEvents()
    {
        _listEvent.clear();
        _simulation.pollEvents( _listEvent );
        for ( const RestaurantEvent& event : _listEvent )
        {
            if ( event._kind == RestaurantEvent::Kind::IngredientSpoiled )
                SW_LOG_INFO_ID( "Restaurant.IngredientSpoiled", "[Restaurant] %# '%#' spoiled", event._value, event._id.c_str() );
        }
    }

    void RestaurantGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _phase == RestaurantPhase::Prepare || _phase == RestaurantPhase::Closed )
            return; // 준비 · 결산은 명령이 진행한다
        if ( _phase == RestaurantPhase::Open )
        {
            _simulation.update( deltaTime * _minutesPerSecond );
            reportEvents();
            if ( isDayOver() )
                closeDay();
            return;
        }
        if ( _resultHold.update( deltaTime ) )
        {
            const bool bWon = _phase == RestaurantPhase::Victory;
            (void)finishMinigame( hashed_string( bWon ? RestaurantGameModeComponentInternal::kWinResult : RestaurantGameModeComponentInternal::kLoseResult ) ); // 흐름이 없으면 베이스가 알린다
        }
    }

    void RestaurantGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`RestaurantCounterComponent`)이 읽어 그린다.
        (void)manager;
        (void)bRespawnViews;
    }

    void RestaurantGameModeComponent::collectMinigameResult( vector<MinigameResultValue>& outListValue ) const
    {
        addMoneyResult( outListValue, _totalProfit );
    }

    void RestaurantGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( RestaurantGameModeComponentInternal::kStateTag, RestaurantGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 날 · 이익 합 · 단계는 상태 칸이다(`GameModeComponent::serializeState`). 식당은 같은 카탈로그 · 설정으로 연 뒤에 읽는다(키트 계약).
        Wallet               restoredWallet;
        Inventory            restoredPantry;
        RestaurantSimulation restoredSimulation;
        Countdown            resultHold = _resultHold;
        if ( io.isReading() )
        {
            if ( _pCatalog == nullptr || _pRecipes == nullptr || _pShops == nullptr || restoredPantry.initialize( nullptr, _pantrySlotCount ) == false )
            {
                SW_LOG_ERROR_ID( "Restaurant.StateNeedsCatalog", "Restaurant game mode: state read before the restaurant, recipe and shop catalogs are loaded - refused" );
                io.fail();
                return;
            }
            initializeSimulation( restoredSimulation ); // 빌린 지갑 · 창고는 이 디렉터의 칸이다 — 읽는 동안은 건드리지 않는다
        }
        io.value( io.isWriting() ? _wallet : restoredWallet );
        io.value( io.isWriting() ? _pantry : restoredPantry );
        io.value( io.isWriting() ? _simulation : restoredSimulation );
        io.value( resultHold );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        if ( io.check( _day <= _dayCount && ( _phase == RestaurantPhase::Open ) == restoredSimulation.isOpen() ) == false )
            return;
        _wallet     = std::move( restoredWallet );
        _pantry     = std::move( restoredPantry );
        _simulation = std::move( restoredSimulation );
        _resultHold = resultHold;
    }
} // namespace sw
