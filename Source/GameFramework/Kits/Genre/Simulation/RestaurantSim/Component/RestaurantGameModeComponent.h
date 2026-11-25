/**
 * @file RestaurantGameModeComponent.h
 * @brief 식당 경영 키트(`GF_RestaurantSim`)의 디렉터 — 정한 날 수 동안 장을 보고 문을 열어 목표 이익을 남기는 한 판을 진행합니다. 키트의 식당(`RestaurantSimulation`)을 씬 데이터로 열고 명령으로 돌립니다.
 * @details 키트 세 갈래의 가운데다: 규칙(키트 루트, 엔진 시계 · 렌더를 모른다)을 들고 씬과 잇는다. 입력 · 모습 · HUD 는 클라이언트 모듈
 *          (`GF_Client_RestaurantSim` 의 `RestaurantCounterComponent`)이 이 디렉터의 명령 · 읽기 API 로만 다룬다 — 전용 서버는 그 모듈을 짓지 않는다.
 *          하루는 준비(장보기 · 가격) → 영업(`_minutesPerSecond` 로 흐르는 시간, 손님이 다 나가면 닫는다) → 결산이다. 지갑 · 창고는 이 판의 것이다(디렉터 계약 ③).
 *          마지막 날의 결산 뒤 이익 합이 목표 이상이면 승리, 아니면 패배이고 `_resultHoldSeconds` 뒤 미니게임을 끝내며 번 이익을 결과 값으로 낸다.
 *          장르 완결 루프 시나리오: `game/playground/automation/restaurantsim_slice.scenario.xml`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantCatalog.h"
#include "GameFramework/Kits/Genre/Simulation/RestaurantSim/RestaurantSimulation.h"
#include "GameFramework/Runtime/Foundation/GameMode/BoundAssetRef.h"
#include "GameFramework/Runtime/Foundation/GameMode/MatchGameModeComponent.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Crafting.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Inventory.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Shop.h"
#include "GameFramework/Runtime/Gameplay/Inventory/Wallet.h"
#include "GameFramework/Runtime/Gameplay/Progression/Reputation.h"
#include "GameFramework/Runtime/World/Environment/WorldClock.h"

namespace sw
{
    /** @brief 식당 판의 단계입니다(감시 값 `Restaurant.Phase` 의 수). */
    ENUM()
    enum class RestaurantPhase : uint8
    {
        Prepare = 0, ///< 장을 보고 가격을 정한다
        Open,        ///< 영업 중이다
        Closed,      ///< 하루 결산을 보인다
        Victory,     ///< 목표 이익을 남겼다
        Defeat       ///< 정한 날 안에 목표에 닿지 못했다
    };
} // namespace sw

namespace sw
{
    /**
     * @class RestaurantGameModeComponent
     * @brief 준비 → 영업 → 결산을 정한 날 수만큼 명령과 시간으로 진행하고 이익 합으로 승패를 정합니다.
     */
    REFLECT( Version = 2, Category = "RestaurantSim", DisplayName = "Restaurant Game Mode", Tooltip = "Runs a few RestaurantSim business days: shopping and prices from commands, the open hours by time, the profit target at the end" )
    class SW_KIT_API RestaurantGameModeComponent : public MatchGameModeComponent
    {
    public:
        REFLECT_BODY();

        RestaurantGameModeComponent();
        virtual ~RestaurantGameModeComponent() override;

        void serializeGameState( StateIO& io ) override;

        // ---- 명령(게임 스레드) ----
        /** @brief 씬의 재료 @p ingredientIndex 를 `_buyCount` 개 삽니다. 준비 · 영업 중이 아니거나 없는 재료이거나 돈 · 자리가 모자라면 false 입니다. */
        [[nodiscard]] bool submitBuy( int32 ingredientIndex );
        /** @brief 카탈로그의 @p dishIndex 번째 요리 가격을 @p steps × `_priceStep` 만큼 바꿉니다(1 아래로 내려가지 않는다). 준비 중이 아니거나 없는 요리이면 false 입니다. */
        [[nodiscard]] bool submitPriceChange( int32 dishIndex, int32 steps );
        /** @brief 카탈로그의 @p dishIndex 번째 요리를 만드는 조리 스테이션을 하나 더 들입니다(`_stationPrice`). 준비 중이 아니거나 스테이션이 없는 요리이거나 돈이 모자라면 false 입니다. */
        [[nodiscard]] bool submitAddStation( int32 dishIndex );
        /** @brief 문을 엽니다. 준비 중이 아니면 false 입니다. */
        [[nodiscard]] bool submitOpen();
        /** @brief 결산을 덮고 다음 날 준비로 넘어갑니다(재료가 하루 늙는다). 결산 중이 아니면 false 입니다. */
        [[nodiscard]] bool submitNextDay();

        // ---- 읽기(클라이언트 모듈 · 감시 값) ----
        /** @brief 이 판의 카탈로그입니다. 열기 전이면 nullptr 입니다. */
        const RestaurantCatalog*     findCatalog() const { return _pCatalog.get(); }
        const RestaurantSimulation&  getSimulation() const { return _simulation; }
        const Inventory&             getPantry() const { return _pantry; }
        const RestaurantSettings&    getSettings() const { return _settings; }
        const vector<hashed_string>& getIngredients() const { return _listIngredient; }
        RestaurantPhase              getGamePhase() const { return _phase; }
        int32                        getPhase() const { return static_cast<int32>( _phase ); }
        /** @brief 오늘이 몇째 날인지입니다(1 부터). */
        int32 getDay() const { return _day; }
        int32 getDayCount() const { return _dayCount; }
        int32 getGold() const;
        /** @brief 오늘 계산한 손님 수입니다. */
        int32 getServed() const { return _simulation.getToday()._served; }
        /** @brief 지난 결산까지의 이익 합입니다. */
        int32 getTotalProfit() const { return static_cast<int32>( _totalProfit ); }
        int32 getTargetProfit() const { return _targetProfit; }
        /** @brief 손님 평판(공유 평판의 세력 `_settings._reputationFaction`)입니다. */
        int32 getReputation() const { return _simulation.getReputation(); }
        /** @brief 요리사 가운데 가장 높은 레벨입니다. */
        int32 getBestCookLevel() const { return _simulation.findBestCookLevel(); }
        /** @brief 첫 요리의 조리 스테이션 수입니다(감시 값). 판이 열리기 전이면 0 입니다. */
        int32 getFirstStationCount() const;
        /** @brief 카탈로그의 첫 요리 가격입니다(감시 값). 판이 열리기 전이면 0 입니다. */
        int32 getFirstMenuPrice() const;

    protected:
        [[nodiscard]] bool startGame() override;
        void               onGameStarted() override;
        void               updateMatch( float32 deltaTime ) override;
        void               onFlush( GameObjectManager& manager, bool bRespawnViews ) override;
        /** @brief 남긴 이익 합을 돈으로 냅니다. */
        void collectMinigameResult( vector<MinigameResultValue>& outListValue ) const override;

    private:
        /** @brief 식당 @p outSimulation 을 이 판의 카탈로그 · 지갑 · 평판 · 시계 · 창고로 엽니다(직원은 고용하지 않는다). */
        void initializeSimulation( RestaurantSimulation& outSimulation );
        /** @brief 카탈로그의 @p dishIndex 번째 요리를 만드는 스테이션입니다. 없는 요리이거나 아무 데서나 만드는 요리면 비어 있습니다. */
        hashed_string findDishStation( int32 dishIndex ) const;
        /** @brief 영업 시간이 지났고 남은 손님이 없으면 true 입니다. */
        bool isDayOver() const;
        /** @brief 문을 닫고 결산합니다. 마지막 날이면 승패를 정합니다. */
        void closeDay();
        /** @brief 쌓인 식당 알림을 로그로 냅니다. */
        void reportEvents();

    private:
        PROPERTY( Category = "Market", DisplayName = "Shops", Tooltip = "Shop catalog the ingredients are bought from (required)" )
        AssetRef<ShopCatalog> _shops;
        PROPERTY( Category = "Market", DisplayName = "Shop", Tooltip = "Shop id in the shop catalog" )
        hashed_string _shop;
        PROPERTY( Category = "Market", DisplayName = "Ingredients", Tooltip = "Item ids the player can buy, in the order they are chosen" )
        vector<hashed_string> _listIngredient;
        PROPERTY( Category = "Market", DisplayName = "Buy Count", Tooltip = "Items bought per purchase", Min = 1 )
        int32 _buyCount;
        PROPERTY( Category = "Market", DisplayName = "Pantry Slots", Min = 1 )
        int32 _pantrySlotCount;
        PROPERTY( Category = "Market", DisplayName = "Start Gold", Min = 0 )
        int32 _startGold;
        PROPERTY( Category = "Kitchen", DisplayName = "Stations", Tooltip = "Cooking stations of the kitchen, one entry per station (list a kind twice for two)" )
        vector<hashed_string> _listStation;
        PROPERTY( Category = "Kitchen", DisplayName = "Station Price", Tooltip = "Gold for one more station of a kind", Min = 0 )
        int32 _stationPrice;
        PROPERTY( Category = "Business", DisplayName = "Reputation", Tooltip = "Faction catalog of the guests' reputation; empty = reputation without tiers" )
        AssetRef<ReputationCatalog> _reputationCatalog;
        PROPERTY( Category = "Staff", DisplayName = "Cooks", Min = 1 )
        int32 _cookCount;
        PROPERTY( Category = "Staff", DisplayName = "Servers", Min = 1 )
        int32 _serverCount;
        PROPERTY( Category = "Staff", DisplayName = "Daily Wage", Min = 0 )
        int32 _dailyWage;
        PROPERTY( Category = "Business", DisplayName = "Settings" )
        RestaurantSettings _settings;
        PROPERTY( Category = "Business", DisplayName = "Weather", Tooltip = "Weather id (catalog) of every day; empty = plain" )
        hashed_string _weather;
        PROPERTY( Category = "Business", DisplayName = "Price Step", Min = 1 )
        int32 _priceStep;
        PROPERTY( Category = "Business", DisplayName = "Minutes Per Second", Tooltip = "Game minutes that pass per second while open", Min = 0.1 )
        float32 _minutesPerSecond;
        PROPERTY( Category = "Business", DisplayName = "Days", Min = 1 )
        int32 _dayCount;
        PROPERTY( Category = "Business", DisplayName = "Target Profit", Min = 0 )
        int32 _targetProfit;

        BoundAssetRef<RestaurantCatalog> _pCatalog;
        BoundAssetRef<RecipeCatalog>     _pRecipes;
        BoundAssetRef<ShopCatalog>       _pShops;
        BoundAssetRef<ItemCatalog>       _pItemCatalog;
        WorldClock                       _clock;              ///< 이 판의 날(식당이 빌린다 — 시세의 시드). 하루를 덮을 때 다음 날로 넘긴다
        BoundAssetRef<ReputationCatalog> _pReputationCatalog; ///< 단계를 적지 않은 판이면 비었다
        ReputationState                  _reputation;         ///< 손님 평판(식당이 빌린다)
        Wallet                           _wallet;             ///< 이 판의 돈(식당이 빌린다) — 결과 값으로만 바탕 판에 나간다
        Inventory                        _pantry;             ///< 이 판의 창고(식당이 빌린다)
        RestaurantSimulation             _simulation;
        vector<RestaurantEvent>          _listEvent;
        PROPERTY( RuntimeState )
        int64 _totalProfit;
        PROPERTY( RuntimeState, Min = 1 )
        int32 _day;
        PROPERTY( RuntimeState )
        RestaurantPhase _phase;
    };
} // namespace sw
