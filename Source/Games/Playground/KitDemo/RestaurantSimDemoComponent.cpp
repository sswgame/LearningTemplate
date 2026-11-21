#include "pch.h"

#include "Games/Playground/KitDemo/RestaurantSimDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantSimulation.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"

namespace sw
{
    namespace
    {
        struct RestaurantSimDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDRS" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr int32   kPantrySlots        = 8;     ///< 주방 창고 칸(아이템 카탈로그가 없어 한 칸에 하나씩 든다)
            static constexpr int32   kCustomerCount      = 2;     ///< 디렉터가 들이는 손님 수(재료도 이만큼 넣는다)
            static constexpr int64   kIngredientUnitCost = 5;     ///< 면 하나의 원가
            static constexpr int64   kDailyWage          = 10;    ///< 요리사 · 서버 하루 급여
            static constexpr float32 kOpenMinutes        = 60.0f; ///< 닫기 전에 돌리는 시간(분) — 두 손님의 조리 · 서빙 · 식사 · 계산이 끝나고도 남는다
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoRestaurantSimState, "KitDemo.RestaurantSim.State", "Restaurant demo state (0 not started, 1 business day closed)", RestaurantSimDemoComponent,
                                &RestaurantSimDemoComponent::getDemoState );
    SW_AUTOMATION_GETTER_WATCH( kitDemoRestaurantSimServed, "KitDemo.RestaurantSim.Served", "Customers who paid during the day", RestaurantSimDemoComponent,
                                &RestaurantSimDemoComponent::getServed );
    SW_AUTOMATION_GETTER_WATCH( kitDemoRestaurantSimRevenue, "KitDemo.RestaurantSim.Revenue", "Day revenue without tips", RestaurantSimDemoComponent,
                                &RestaurantSimDemoComponent::getRevenue );
    SW_AUTOMATION_GETTER_WATCH( kitDemoRestaurantSimProfit, "KitDemo.RestaurantSim.Profit", "Day profit (revenue + tips - ingredients - wages - spoilage)", RestaurantSimDemoComponent,
                                &RestaurantSimDemoComponent::getProfit );

    RestaurantSimDemoComponent::RestaurantSimDemoComponent()
        : _catalogPath{}
        , _catalog{}
        , _pCatalog{}
        , _pRecipeCatalog{}
        , _served{ 0 }
        , _revenue{ 0 }
        , _profit{ 0 }
    {
    }

    RestaurantSimDemoComponent::~RestaurantSimDemoComponent() = default;

    bool RestaurantSimDemoComponent::runDay()
    {
        // 지갑 · 창고는 이 하루만 빌려 준다. 평판 · 시계 · 시장은 쓰지 않는다(nullptr 이면 끈다).
        Wallet    wallet;
        Inventory pantry;
        if ( pantry.initialize( nullptr, RestaurantSimDemoComponentInternal::kPantrySlots ) == false )
            return false;

        RestaurantSimulation simulation;
        simulation.initialize( _pCatalog.get(), _pRecipeCatalog.get(), nullptr, nullptr, nullptr, &wallet, nullptr, nullptr, pantry, RestaurantSettings{} );
        (void)simulation.hireStaff( hashed_string( "cook" ), StaffRole::Cook, RestaurantSimDemoComponentInternal::kDailyWage );
        (void)simulation.hireStaff( hashed_string( "server" ), StaffRole::Server, RestaurantSimDemoComponentInternal::kDailyWage );
        (void)simulation.addIngredient( hashed_string( "noodle" ), RestaurantSimDemoComponentInternal::kCustomerCount, RestaurantSimDemoComponentInternal::kIngredientUnitCost );

        // 문을 연 뒤 손님을 들인다(문을 열면 지난 손님 목록을 비운다). 카탈로그에 도착이 없어 저절로 오는 손님은 없다.
        simulation.openDay( hashed_string( "clear" ) );
        for ( int32 index = 0; index < RestaurantSimDemoComponentInternal::kCustomerCount; ++index )
        {
            if ( simulation.admitCustomer( hashed_string( "student" ) ) < 0 )
            {
                SW_LOG_ERROR_ID( "KitDemo.RestaurantSimUnknownCustomer", "Restaurant demo: the catalog has no customer type student - the day does not run" );
                return false;
            }
        }
        simulation.update( RestaurantSimDemoComponentInternal::kOpenMinutes );

        RestaurantDaySummary summary;
        simulation.closeDay( summary );
        _served  = summary._served;
        _revenue = static_cast<int32>( summary._revenue );
        _profit  = static_cast<int32>( summary._profit );
        markRunFinished();
        return true;
    }

    bool RestaurantSimDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false || acquireRequired( _catalog, _pRecipeCatalog ) == false )
            return false;
        return runDay();
    }

    void RestaurantSimDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.RestaurantSimDone", "[KitDemo] restaurant: served %#, revenue %#, profit %#", _served, _revenue, _profit );
    }

    void RestaurantSimDemoComponent::serializeGameState( StateIO& io )
    {
        serializeResults( io, RestaurantSimDemoComponentInternal::kStateTag, RestaurantSimDemoComponentInternal::kStateVersion );
    }
} // namespace sw
