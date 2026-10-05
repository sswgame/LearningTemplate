/**
 * @file RestaurantSimulation.h
 * @brief 식당 하루 — 손님 도착(시간대 · 평판 · 날씨 · 가격), 자리 · 주문 · 조리 대기열(요리사 · 스테이션 수만큼 병렬) · 서빙 · 계산 · 팁, 인내 초과 이탈,
 *        직원(고용 · 급여 · 숙련), 시장 시세, 재료 신선도, 별점 이동 평균, 일 결산입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Inventory/Crafting.h"
#include "GameFramework/Base/Inventory/Inventory.h"
#include "GameFramework/Base/Inventory/Shop.h"
#include "GameFramework/Base/Progression/LevelProgress.h"
#include "GameFramework/Base/Progression/Reputation.h"
#include "GameFramework/Base/Utility/Countdown.h"
#include "GameFramework/Base/Utility/EventBuffer.h"
#include "GameFramework/Base/Utility/FixedStepTimer.h"
#include "GameFramework/Base/Utility/GameRandom.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Simulation/RestaurantSim/IngredientStock.h"

namespace sw
{
    struct DishDef;

    class ItemCatalog;
    class RestaurantCatalog;

    /** @brief 식당 규칙의 수치입니다. 시간은 게임 분입니다. */
    struct RestaurantSettings
    {
        hashed_string _reputationFaction{ "restaurant" }; ///< 평판 세력 id(기반 `ReputationState`)
        float32       _serveMinutes{ 2.0f };              ///< 서버가 요리 하나를 나르는 분
        float32       _checkoutMinutes{ 1.0f };           ///< 계산 하나의 분
        float32       _priceElasticity{ 1.0f };           ///< 가격 / 기본 가격이 1 오를 때 줄어드는 수요 몫
        float32       _preferredWeight{ 3.0f };           ///< 좋아하는 분류의 요리를 고르는 배율(0 이면 그 분류는 시키지 않는다)
        float32       _reputationArrivalScale{ 0.001f };  ///< 평판 1 당 손님 배율 증가
        float32       _cookSpeedPerLevel{ 0.1f };         ///< 요리사 레벨 1 당 조리 시간 감소 몫(최소 30 %)
        float32       _marketVolatility{ 0.2f };          ///< 날마다 시장 사는 값이 ±이만큼 흔들린다
        float32       _qualityWeight{ 0.6f };             ///< 만족도 = 품질 몫 × 이것 + (1 − 기다림 몫) × (1 − 이것)
        int32         _reputationPerServe{ 10 };          ///< 만족도 1 이면 +이것, 0 이면 −이것
        int32         _walkoutPenalty{ 15 };              ///< 기다리다 떠나면 평판에서 뺀다
        int32         _seatCount{ 4 };
        int64         _cookXp{ 10 };
        int64         _serveXp{ 5 };
        int64         _checkoutXp{ 5 };
        uint32        _randomSeed{ 5150u }; ///< 손님 성향 · 요리 고르기 · 시세
    };
} // namespace sw

namespace sw
{
    /** @brief 직원 역할입니다. */
    enum class StaffRole : uint8
    {
        Cook = 0,
        Server,
        Cashier ///< 없으면 쉬는 서버가 계산한다
    };

    SW_GF_API const utf8* toString( StaffRole role );

    /** @brief 직원 하나입니다. */
    struct StaffMember
    {
        hashed_string _name{};
        LevelProgress _level{};
        float32       _busyMinutes{ 0.0f }; ///< 지금 일이 끝나기까지
        int64         _dailyWage{ 0 };
        int32         _taskCustomer{ -1 }; ///< 맡은 손님 id(−1 = 쉼)
        StaffRole     _role{ StaffRole::Cook };
        uint8         _bHired{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 손님 상태입니다. */
    enum class CustomerState : uint8
    {
        Queued = 0,  ///< 문 앞에서 자리를 기다린다(인내가 준다)
        WaitingFood, ///< 주문했다(인내가 준다)
        Eating,
        WaitingCheckout,
        Paid,
        Left ///< 기다리다 떠났거나 고를 요리가 없었다
    };

    /** @brief 손님 하나입니다. */
    struct RestaurantCustomer
    {
        hashed_string _typeId{};
        hashed_string _dishId{};
        float32       _waited{ 0.0f }; ///< 자리 · 요리를 기다린 분
        float32       _patience{ 30.0f };
        float32       _eatRemaining{ 0.0f };
        int64         _price{ 0 }; ///< 주문할 때의 메뉴 가격
        int32         _id{ 0 };
        int32         _seat{ -1 };
        int32         _quality{ 0 }; ///< 받은 요리의 품질(0 = 아직)
        CustomerState _state{ CustomerState::Queued };
        uint8         _bServing{ SW_FALSE }; ///< 서버가 나르는 중
        uint8         _bCheckingOut{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 주방 주문 하나입니다. */
    struct KitchenOrder
    {
        hashed_string _dishId{};
        hashed_string _station{}; ///< 레시피의 조리 스테이션(비면 아무 데서나)
        float32       _remaining{ 0.0f };
        int32         _customerId{ 0 };
        int32         _cook{ -1 }; ///< 맡은 요리사(−1 = 기다림)
        int32         _quality{ 0 };
        uint8         _bReady{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 메뉴 한 줄 — 가격은 바꿀 수 있습니다. */
    struct MenuEntry
    {
        hashed_string _dishId{};
        int64         _price{ 0 };
        uint8         _bOnMenu{ SW_TRUE };
    };
} // namespace sw

namespace sw
{
    /** @brief 하루 결산입니다. 이익 = 매출 + 팁 − 재료비 − 급여 − 버린 재료입니다. */
    struct RestaurantDaySummary
    {
        int64   _revenue{ 0 };
        int64   _tips{ 0 };
        int64   _ingredientCost{ 0 }; ///< 오늘 쓴 재료의 원가
        int64   _wages{ 0 };
        int64   _spoilageCost{ 0 }; ///< 오늘 아침(지난 `advanceDay`)에 버린 재료
        int64   _profit{ 0 };
        float32 _rating{ 0.0f }; ///< 닫을 때의 별점
        int32   _arrivals{ 0 };
        int32   _served{ 0 };
        int32   _walkouts{ 0 };        ///< 기다리다 떠남(평판 감소)
        int32   _noChoice{ 0 };        ///< 살 만한 요리가 없어 그냥 나감
        int32   _unservedAtClose{ 0 }; ///< 닫을 때 아직 기다리던 손님(평판 그대로)
    };
} // namespace sw

namespace sw
{
    /** @brief 식당에 생긴 일입니다. */
    struct RestaurantEvent
    {
        enum class Kind : uint8
        {
            CustomerArrived = 0, ///< _id = 성향
            OrderPlaced,         ///< _id = 요리
            DishCooked,          ///< _id = 요리, _value = 품질
            CustomerPaid,        ///< _id = 요리, _value = 낸 돈(팁 포함)
            CustomerLeft,        ///< _id = 성향
            StaffLevelUp,        ///< _id = 직원 이름, _value = 새 레벨
            IngredientSpoiled    ///< _id = 재료, _value = 개수
        };
        hashed_string _id{};
        int64         _value{ 0 };
        int32         _customerId{ -1 };
        Kind          _kind{ Kind::CustomerArrived };
    };
} // namespace sw

namespace sw
{
    /**
     * @class RestaurantSimulation
     * @brief 셰프 RPG 류의 식당 경영입니다. 결정적입니다(씨앗 · 고정 1 분 걸음).
     * @details 1 분마다:
     *          1. 일이 끝난 직원을 풀어 준다(조리 완료 → 나를 요리, 서빙 완료 → 먹기 시작, 계산 완료 → 돈 · 팁 · 만족도 · 평판).
     *          2. 기다리는 손님(문 앞 · 요리)의 인내를 줄이고, 다 쓰면 떠난다(주문은 취소, 평판 −`_walkoutPenalty`).
     *          3. 영업 중이면 손님이 온다 — 시간당 수 × (1 + 평판 × `_reputationArrivalScale`) × 날씨 배율 × 가격 수요 배율을 쌓아 1 이 찰 때마다.
     *          4. 빈자리에 앉히고 주문받는다 — 지불 의사 안의 요리 중 (좋아하는 분류 × `_preferredWeight`) × 가격 수요로 고르고, 재료는 이때 거둔다. 가중치가 0 인 요리는 후보가 아니다 — 모두 0 이면 시키지 않고 나간다(`_noChoice`).
     *          5. 주방 — 기다리는 주문을 들어온 순서로, 쉬는 요리사와 그 스테이션의 빈 자리가 있으면 시작한다(요리사 · 스테이션 수만큼 병렬).
     *          6. 다 된 요리는 쉬는 서버가, 다 먹은 손님은 쉬는 계산원(없으면 서버)이 맡는다.
     *          팁 = 가격 × 성향 팁 몫 × 품질 몫 × (1 − 기다림 몫), 별점 = 1 + 4 × 최근 N 손님 만족도 평균(떠난 손님은 0).
     *          카탈로그는 빌려 씁니다(시뮬레이션보다 오래 살아야 합니다).
     */
    class SW_GF_API RestaurantSimulation
    {
    public:
        static constexpr float32 kStepMinutes = 1.0f;

        RestaurantSimulation();

        /**
         * @brief 새 식당을 엽니다. 재료는 빌린 창고(@p pantry — 식당보다 오래 살아야 한다)에 듭니다 — 섞인 게임은 플레이어 가방을 넘겨 "밭 → 식탁" 이 그대로 된다.
         */
        void initialize( const RestaurantCatalog* pCatalog, const RecipeCatalog* pRecipeCatalog, const ItemCatalog* pItemCatalog, const ShopCatalog* pShopCatalog,
                         const ReputationCatalog* pReputationCatalog, const ExperienceCurve* pStaffCurve, Inventory& pantry, const RestaurantSettings& settings );
        /** @brief 조리 스테이션 수를 정합니다("Stove" 2 개 · "Oven" 1 개). */
        void setStationCount( const hashed_string& station, int32 count );
        /** @brief 직원을 고용합니다. 직원 자리 번호입니다. */
        int32 hireStaff( const hashed_string& name, StaffRole role, int64 dailyWage, int32 level = 1 );
        /** @brief 내보냅니다(맡은 일이 있으면 그 일은 다시 기다린다). 없는 직원이면 false 입니다. */
        bool fireStaff( int32 staffIndex );
        /** @brief 메뉴 가격을 바꿉니다. 메뉴에 없는 요리면 false 입니다. */
        bool setMenuPrice( const hashed_string& dishId, int64 price );
        bool setDishOnMenu( const hashed_string& dishId, bool bOnMenu );

        /** @brief 시장에서 재료를 사서 신선도 묶음으로 넣습니다(단가는 그날 시세). */
        ShopResult buyIngredient( const hashed_string& shopId, const hashed_string& itemId, int32 count );
        /** @brief 재료를 그냥 넣습니다(텃밭 · 선물 · 시험). */
        int32 addIngredient( const hashed_string& itemId, int32 count, int64 unitCost );

        /** @brief 문을 엽니다 — 그날 날씨 · 집계를 새로 둡니다. */
        void openDay( const hashed_string& weatherId );
        /** @brief 시간(게임 분)을 흘립니다. 닫는 시각이 지나도 안에 있는 손님은 계속 받습니다. */
        void update( float32 deltaMinutes );
        /** @brief 손님 하나를 바로 들입니다(예약 · 시험). 손님 id 이고 모르는 성향이면 −1 입니다. */
        int32 admitCustomer( const hashed_string& typeId );
        /** @brief 닫습니다 — 남은 손님을 정리하고 급여를 치른 뒤 결산을 @p outSummary 에 적습니다. */
        void closeDay( RestaurantDaySummary& outSummary );
        /** @brief 밤을 넘깁니다 — 재료가 하루 늙어 상한 것은 버리고(다음 결산에 들어간다), 시세 · 평판이 바뀝니다. */
        void advanceDay();
        void drainEvents( vector<RestaurantEvent>& outListEvent );

        /** @brief 지금 주문할 수 있는 요리인가입니다(메뉴 · 레시피 · 스테이션 · 레벨 · 재료). */
        bool  canServe( const hashed_string& dishId ) const;
        int64 getMenuPrice( const hashed_string& dishId ) const;
        /** @brief 지금 가격이 손님 도착에 주는 배율입니다(메뉴 평균 가격 비율로). */
        float32 computePriceDemandScale() const;
        /** @brief 지금의 손님 도착률(시간당)입니다. */
        float32 computeArrivalRate() const;
        /** @brief 최근 N 손님의 별점(1..5)입니다. 아직 손님이 없으면 0 입니다. */
        float32 computeRating() const;
        int32   getReputation() const { return _reputation.getValue( _settings._reputationFaction ); }
        /** @brief 조리 중인 주문 수입니다. */
        int32                             countCooking() const;
        int32                             countOccupiedSeats() const;
        const vector<StaffMember>&        getStaff() const { return _listStaff; }
        const vector<RestaurantCustomer>& getCustomers() const { return _listCustomer; }
        const deque<KitchenOrder>&        getOrders() const { return _listOrder; }
        const RestaurantDaySummary&       getToday() const { return _today; }
        const IngredientStock&            getStock() const { return _stock; }
        const ShopState&                  getMarket() const { return _market; }
        Wallet&                           getWallet() { return _wallet; }
        const Wallet&                     getWallet() const { return _wallet; }
        hashed_string                     getCurrency() const { return Wallet::getDefaultCurrency(); }
        float32                           getMinutes() const { return _minutes; }
        int32                             getDay() const { return _day; }
        bool                              isOpen() const { return _bOpen != SW_FALSE; }

    private:
        /** @brief 조리 스테이션 종류 하나의 수입니다. */
        struct StationSlot
        {
            hashed_string _station{};
            int32         _count{ 0 };
        };

        void stepFixed();
        void finishStaffWork();
        void tickPatience();
        void spawnArrivals();
        void seatCustomers();
        void startCooking();
        void assignServers();
        void assignCheckouts();
        /** @brief 손님 @p customer 에게 요리를 고르고 재료를 거둡니다. 고를 것이 없으면 false 입니다. */
        bool placeOrder( RestaurantCustomer& customer );
        void settlePayment( RestaurantCustomer& customer );
        void leaveCustomer( RestaurantCustomer& customer, bool bWalkout );
        void pushSatisfaction( float32 satisfaction );
        void grantXp( StaffMember& staff, int64 amount );
        void rollMarketPrices();

        int32            findCustomerIndex( int32 customerId ) const;
        int32            findIdleStaff( StaffRole role ) const;
        int32            findBestCookLevel() const;
        int32            countStations( const hashed_string& station ) const;
        int32            countCookingAt( const hashed_string& station ) const;
        const MenuEntry* findMenuEntry( const hashed_string& dishId ) const;
        const RecipeDef* findRecipe( const DishDef& dish ) const;
        float32          computeDishDemand( const DishDef& dish, int64 price ) const;

        vector<StaffMember>          _listStaff;
        vector<RestaurantCustomer>   _listCustomer;
        deque<KitchenOrder>          _listOrder;
        vector<MenuEntry>            _listMenu;
        vector<StationSlot>          _listStation;
        vector<float32>              _listSatisfaction; ///< 최근 N 손님(오래된 것이 앞)
        EventBuffer<RestaurantEvent> _eventBuffer;
        vector<ReputationEvent>      _listReputationScratch;
        vector<IngredientSpoilage>   _listSpoilageScratch;
        IngredientStock              _stock;
        Crafter                      _crafter;
        ShopState                    _market;
        Wallet                       _wallet;
        ReputationState              _reputation;
        RestaurantSettings           _settings;
        RestaurantDaySummary         _today;
        FixedStepTimer               _stepTimer;
        GameRandom                   _random;
        hashed_string                _weatherId;
        const RestaurantCatalog*     _pCatalog;
        Inventory*                   _pPantry; ///< 주방 창고(빌림)
        const RecipeCatalog*         _pRecipeCatalog;
        const ShopCatalog*           _pShopCatalog;
        const ExperienceCurve*       _pStaffCurve;
        float32                      _minutes; ///< 문을 연 뒤 지난 분
        RateAccumulator              _arrival; ///< 손님 도착(명)
        int64                        _pendingSpoilageCost;
        int32                        _day;
        int32                        _nextCustomerId;
        uint8                        _bOpen;
    };
} // namespace sw
