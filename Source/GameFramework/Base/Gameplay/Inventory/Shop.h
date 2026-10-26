/**
 * @file Shop.h
 * @brief 지갑과 가게입니다. 통화를 여러 개 가진 지갑, XML 가게 카탈로그(재고, 가격, 배율, 재입고, 잠금 조건), 사기와 팔기, 시세를 다룹니다. 시세는 많이 팔면 값이 떨어지고 날마다 회복됩니다.
 * @details 식당 경영(재료 사기, 요리 팔기), JRPG 상점, 리썰 컴퍼니 터미널(매입률), 생활 게임(출하 시세)이 같은 규칙을 쓴다.
 *          아이템은 기존 `Inventory` 와 `ItemCatalog` 를 그대로 쓴다. 가격이 적혀 있지 않은 재고는 `ItemDef::_value` 를 가격으로 쓴다.
 *          이 파일은 오프라인과 로컬 상점을 위한 것이다. 온라인(서버 권위) 게임의 상점은 원장 위에 만든 GF_Economy 이고, 이때 지갑은 원장의 읽기 전용 사본(`EconomyMirror`)이다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class Inventory;
    class ItemCatalog;
    class XMLNode;

    /** @brief 지갑의 통화 하나입니다. */
    struct WalletBalance
    {
        hashed_string _currency{};
        int64         _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 지갑의 변화 하나입니다. 화면의 "+20 Gold" 같은 표시에 씁니다. */
    struct WalletEvent
    {
        hashed_string _currency{};
        int64         _delta{ 0 };   ///< 더한 값(쓰면 음수)
        int64         _balance{ 0 }; ///< 바뀐 뒤 잔액
    };
} // namespace sw

namespace sw
{
    /**
     * @class Wallet
     * @brief 통화 여러 개(이름 → int64)를 담습니다. 통화 이름은 데이터이므로 장르마다 정합니다("Gold", "Credits", "Souls", "Tickets").
     * @details 잔액이 음수(빚)가 되는 것은 `charge` 로만 가능하다. 쓰기는 `trySpend` 로 하고, 잔액이 모자라거나 빚이 있으면 아무것도 바꾸지 않는다. 통화 순서는 처음 만난 순서이다. 화면과 세이브가 같은 순서를 쓰도록 결정적으로 정한 것이다.
     */
    class SW_GF_API Wallet
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "WALT" );
        static constexpr uint32 kStateVersion = 1;

        /** @brief 통화를 적지 않으면 쓰는 기본 통화 "Gold" 입니다. */
        static hashed_string getDefaultCurrency();

        /** @brief @p amount 를 더합니다. 0 보다 커야 합니다. */
        void add( const hashed_string& currency, int64 amount );
        /** @brief 잔액이 @p amount 이상이면 빼고 true 를 반환합니다. 모자라면 그대로 두고 false 를 반환합니다. */
        [[nodiscard]] bool trySpend( const hashed_string& currency, int64 amount );
        /** @brief @p amount 를 거절 없이 뺍니다. 0 보다 커야 합니다. 운영비나 급여처럼 미룰 수 없는 지출에 쓰며, 잔액이 음수(빚)가 될 수 있습니다. 빚이 있으면 `trySpend` 는 거절됩니다. */
        void charge( const hashed_string& currency, int64 amount );
        /** @brief 잔액을 @p amount 로 설정합니다. 세이브 불러오기와 치트에 씁니다. 이벤트는 내지 않습니다. */
        void setBalance( const hashed_string& currency, int64 amount );
        void clear();
        /** @brief 쌓인 변화를 넘겨 주고 비웁니다. */
        void drainEvents( vector<WalletEvent>& outListEvent );
        /** @brief 통화마다 이름과 잔액을 처음 만난 순서로 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 모두 복원합니다. 알림은 없습니다. 통화 이름이 비어 있으면 false 를 반환하고 상태는 바뀌지 않습니다. 음수 잔액(빚)도 그대로 복원합니다. */
        [[nodiscard]] bool readState( Archive& archive );

        int64                        getBalance( const hashed_string& currency ) const;
        bool                         canAfford( const hashed_string& currency, int64 amount ) const { return amount <= getBalance( currency ); }
        const vector<WalletBalance>& getBalances() const { return _listBalance; }
        uint32                       getRevision() const { return _revision; }

    private:
        WalletBalance& findOrAddBalance( const hashed_string& currency );

        vector<WalletBalance>    _listBalance{};
        EventBuffer<WalletEvent> _eventBuffer{};
        uint32                   _revision{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 가게 진열 항목 하나입니다. */
    struct ShopStockDef
    {
        hashed_string _itemID{};
        string        _requirement{}; ///< 잠금 조건 식입니다. 키트나 게임이 평가하며, 예를 들어 GameFlags 조건이 있습니다. 비어 있으면 항상 열려 있습니다
        int32         _price{ -1 };   ///< 단가(-1 이면 `ItemDef::_value`)
        int32         _count{ -1 };   ///< 처음 재고이자 최대 재고(-1 이면 무한)
        int32         _restock{ 0 };  ///< 재입고할 때 더하는 수(최대 재고까지)
    };
} // namespace sw

namespace sw
{
    /** @brief 가게 하나입니다. */
    struct SW_GF_API ShopDef
    {
        hashed_string         _id{};
        hashed_string         _currency{};             ///< 이 가게가 받고 주는 통화(비어 있으면 "Gold")
        vector<ShopStockDef>  _listStock{};            ///< 진열 순서
        vector<hashed_string> _listRefusedCategory{};  ///< 사들이지 않는 분류("Quest", "Key")
        float32               _buyMultiplier{ 1.0f };  ///< 손님이 살 때 값에 곱한다
        float32               _sellMultiplier{ 0.5f }; ///< 손님이 팔 때 값에 곱한다
        float32               _saturation{ 0.0f };     ///< 한 개 팔릴 때마다 그 아이템의 매입 시세가 떨어지는 양(0 이면 시세 없음)
        float32               _minSellFactor{ 0.2f };  ///< 시세의 하한
        float32               _recoveryPerDay{ 0.1f }; ///< 하루마다 시세가 1 로 돌아오는 양
        int32                 _restockDays{ 1 };       ///< 재입고 주기(일). 0 이면 재입고하지 않는다

        const ShopStockDef* findStock( const hashed_string& itemID ) const;
        bool                refusesCategory( const hashed_string& category ) const;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShopCatalog
     * @brief `<ShopCatalog><Shop id="general" currency="Gold" buyMultiplier="1" sellMultiplier="0.5" restockDays="1" saturation="0.05"
     *        minSellFactor="0.3" recovery="0.1" refuses="Quest,Key"><Stock item="potion" price="20" count="10" restock="10" requires="flagExpr"/>
     *        </Shop></ShopCatalog>` 를 읽습니다.
     */
    class SW_GF_API ShopCatalog : public XMLCatalog<ShopCatalog>, public GameCatalog<ShopDef>
    {
        friend class XMLCatalog<ShopCatalog>;

    public:
    private:
        static constexpr const utf8* kXMLRootName = "ShopCatalog"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
    };
} // namespace sw

namespace sw
{
    /** @brief 사기와 팔기의 결과입니다. */
    ENUM()
    enum class ShopResult : uint8
    {
        Ok = 0,
        UnknownShop,
        UnknownItem, ///< 가게가 팔지 않는 아이템(사기), 또는 카탈로그에 없는 아이템(팔기)
        InvalidCount,
        Locked, ///< 진열 항목의 조건이 맞지 않는다
        OutOfStock,
        NotEnoughMoney,
        NoRoom,  ///< 인벤토리에 공간(슬롯과 무게)이 없다
        Refused, ///< 가게가 그 분류를 사들이지 않는다
        NotOwned ///< 팔 만큼 가지고 있지 않다
    };

    /**
     * @class IShopConditionEvaluator
     * @brief 진열 항목의 `requires` 식을 평가합니다. 가게는 식을 보관만 하고 뜻은 모릅니다. 키트나 게임이 플래그, 퀘스트, 평판으로 판정해서 답합니다.
     */
    class IShopConditionEvaluator
    {
    public:
        IShopConditionEvaluator()          = default;
        virtual ~IShopConditionEvaluator() = default;

        IShopConditionEvaluator( const IShopConditionEvaluator& )            = default;
        IShopConditionEvaluator& operator=( const IShopConditionEvaluator& ) = default;

        virtual bool isConditionMet( string_view expression ) const = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 가게에서 일어난 일 하나입니다. */
    struct ShopEvent
    {
        enum class Kind : uint8
        {
            Bought = 0, ///< 손님이 샀다
            Sold,       ///< 손님이 팔았다
            Restocked   ///< 재입고(아이템마다 하나씩)
        };
        hashed_string _shopID{};
        hashed_string _itemID{};
        int64         _money{ 0 }; ///< 낸 돈이나 받은 돈
        int32         _count{ 0 };
        Kind          _kind{ Kind::Bought };
    };
} // namespace sw

namespace sw
{
    /** @brief 가게 하나의 현재 상태입니다. */
    struct ShopRuntime
    {
        hashed_string         _shopID{};
        vector<int32>         _listStockCount{};    ///< `ShopDef::_listStock` 과 같은 순서의 재고(-1 이면 무한)
        vector<hashed_string> _listSaturatedItem{}; ///< 시세가 떨어진 아이템
        vector<float32>       _listSellFactor{};    ///< 그 아이템의 시세(1 이면 보통)
        vector<hashed_string> _listExtraRefused{};  ///< 실행 중에 추가한 "사들이지 않는 분류"
        float32               _buyModifier{ 1.0f };
        float32               _sellModifier{ 1.0f };
        int32                 _daysSinceRestock{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShopState
     * @brief 모든 가게의 재고, 시세, 가격 배율을 관리합니다. 카탈로그 두 개는 소유하지 않고 참조만 하므로 이 객체보다 오래 살아 있어야 합니다.
     * @details 사기 단가는 round( 값 × 가게 `buyMultiplier` × 사기 배율 ) 이다. 팔기는 한 개씩 round( `ItemDef::_value` × `sellMultiplier` × 팔기 배율 × 시세 ) 를 더하고,
     *          한 개를 팔 때마다 시세가 `saturation` 만큼 떨어진다(하한은 `minSellFactor`). `advanceDay` 가 시세를 되돌리고 `restockDays` 마다 재입고한다.
     *          사기와 팔기는 전부 성공하거나 아무것도 바뀌지 않는다.
     */
    class SW_GF_API ShopState
    {
    public:
        ShopState();

        void initialize( const ShopCatalog* pShopCatalog, const ItemCatalog* pItemCatalog );
        /** @brief 잠금 조건을 평가할 쪽입니다. 없으면 조건이 있는 항목은 항상 잠겨 있습니다. */
        void setConditionEvaluator( const IShopConditionEvaluator* pEvaluator ) { _pConditionEvaluator = pEvaluator; }
        /** @brief 가격 배율입니다. 평판, 할인, 그날의 매입률(리썰 컴퍼니)에 씁니다. 사기와 팔기를 따로 정합니다. */
        /** @details 음수이거나 없는 가게이면 false 입니다(음수는 오류를 남기고 0 으로 자르지 않습니다). */
        [[nodiscard]] bool setPriceModifier( const hashed_string& shopID, float32 buyModifier, float32 sellModifier );
        /** @brief 이 가게가 @p category 를 더 이상 사들이지 않게 합니다. */
        void refuseCategory( const hashed_string& shopID, const hashed_string& category );

        ShopResult evaluateBuy( const hashed_string& shopID, const hashed_string& itemID, int32 count, const Wallet& wallet, const Inventory& inventory ) const;
        /** @brief 삽니다. @p pOutPaid 가 있으면 실제로 낸 금액을 기록합니다. 성공했을 때만 기록합니다. */
        ShopResult buy( const hashed_string& shopID, const hashed_string& itemID, int32 count, Wallet& wallet, Inventory& inventory, int64* pOutPaid = nullptr );
        ShopResult evaluateSell( const hashed_string& shopID, const hashed_string& itemID, int32 count, const Inventory& inventory ) const;
        /** @brief 팝니다. @p pOutReceived 가 있으면 실제로 받은 금액을 기록합니다. 성공했을 때만 기록합니다. */
        ShopResult sell( const hashed_string& shopID, const hashed_string& itemID, int32 count, Wallet& wallet, Inventory& inventory, int64* pOutReceived = nullptr );
        /** @brief 하루를 넘깁니다. 시세를 회복하고, 재입고할 때가 되면 재입고합니다. */
        void advanceDay();
        void drainEvents( vector<ShopEvent>& outListEvent );

        /** @brief 사는 단가입니다. 가게가 팔지 않는 아이템이면 -1 입니다. */
        int32 computeBuyPrice( const hashed_string& shopID, const hashed_string& itemID ) const;
        /** @brief @p count 개를 지금 팔면 받는 돈입니다. 시세 하락을 반영합니다. */
        int64 computeSellTotal( const hashed_string& shopID, const hashed_string& itemID, int32 count ) const;
        /** @brief 남은 재고입니다. 무한이면 -1 이고, 진열에 없으면 0 입니다. */
        int32 getStockCount( const hashed_string& shopID, const hashed_string& itemID ) const;
        /** @brief 그 아이템의 매입 시세입니다. 1 이면 보통입니다. */
        float32 getSellFactor( const hashed_string& shopID, const hashed_string& itemID ) const;
        /** @brief 진열 항목이 열려 있으면 true 입니다. 조건이 없거나 조건 평가가 참일 때 열립니다. */
        bool               isUnlocked( const ShopStockDef& stock ) const;
        hashed_string      getCurrency( const hashed_string& shopID ) const;
        const ShopRuntime* findRuntime( const hashed_string& shopID ) const;

        /** @brief 가게마다 런타임 상태(가게 id, 재고, 시세, 사들이지 않는 분류, 가격 배율, 재입고 후 지난 날)를 씁니다. 카탈로그는 `initialize` 가 받은 것이므로 저장하지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 가 쓴 바이트로 상태를 복원합니다. 데이터가 깨졌으면 false 를 반환하고 상태는 바뀌지 않습니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        ShopRuntime* findRuntime( const hashed_string& shopID );
        int32        findStockIndex( const ShopDef& shop, const hashed_string& itemID ) const;
        int32        computeBasePrice( const ShopStockDef& stock ) const;
        bool         isRefused( const ShopDef& shop, const ShopRuntime& runtime, const hashed_string& itemID ) const;

        vector<ShopRuntime>            _listRuntime;
        EventBuffer<ShopEvent>         _eventBuffer;
        const ShopCatalog*             _pShopCatalog;
        const ItemCatalog*             _pItemCatalog;
        const IShopConditionEvaluator* _pConditionEvaluator;
    };
} // namespace sw
