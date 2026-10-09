/**
 * @file Shop.h
 * @brief 지갑과 가게 — 통화 여럿인 지갑, XML 가게 카탈로그(재고 · 가격 · 배율 · 재입고 · 잠금 조건), 사기 · 팔기 · 시세(많이 팔면 값이 떨어지고 날마다 회복)입니다.
 * @details 식당 경영(재료 사기 · 요리 팔기) · JRPG 상점 · 리썰 컴퍼니 터미널(매입률) · 생활 게임(출하 시세)이 같은 규칙을 씁니다.
 *          아이템은 기존 `Inventory` · `ItemCatalog` 를 그대로 씁니다. 가격이 적히지 않은 재고는 `ItemDef::_value` 입니다.
 *          이 파일은 오프라인 · 로컬 상점입니다 — 온라인(서버 권위) 게임의 상점은 GF_Economy(원장 위)이고 지갑은 원장의 읽기 사본(`EconomyMirror`)입니다.
 */
#pragma once
#include "Core/Common/FourCcUtil.h"
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Archive;
    class Inventory;
    class ItemCatalog;
    class XmlNode;

    /** @brief 지갑의 통화 하나입니다. */
    struct WalletBalance
    {
        hashed_string _currency{};
        int64         _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 지갑이 바뀐 일 하나입니다(화면의 "+20 Gold"). */
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
     * @brief 통화 여럿(이름 → int64)을 담습니다. 통화 이름은 데이터라 장르마다 정합니다("Gold" · "Credits" · "Souls" · "Tickets").
     * @details 잔액은 `charge` 로만 음수(빚)가 됩니다 — 쓰기는 `trySpend` 로 모자라면(빚이 있으면) 아무것도 바꾸지 않습니다. 통화 순서는 처음 만난 순서입니다(결정적 — 화면 · 세이브).
     */
    class SW_GF_API Wallet
    {
    public:
        static constexpr uint32 kStateTag     = FourCcUtil::make( "WALT" );
        static constexpr uint32 kStateVersion = 1;

        /** @brief 통화를 적지 않으면 쓰는 "Gold" 입니다. */
        static hashed_string getDefaultCurrency();

        /** @brief @p amount(0 보다 커야 한다)를 더합니다. */
        void add( const hashed_string& currency, int64 amount );
        /** @brief 잔액이 @p amount 이상이면 빼고 true, 모자라면 그대로 두고 false 입니다. */
        [[nodiscard]] bool trySpend( const hashed_string& currency, int64 amount );
        /** @brief @p amount(0 보다 커야 한다)를 거절 없이 뺍니다 — 운영비 · 급여처럼 미룰 수 없는 지출이고 잔액이 음수(빚)가 될 수 있습니다. 빚이 있으면 `trySpend` 는 거절됩니다. */
        void charge( const hashed_string& currency, int64 amount );
        /** @brief 잔액을 그대로 둡니다(세이브 불러오기 · 치트). 이벤트는 내지 않습니다. */
        void setBalance( const hashed_string& currency, int64 amount );
        void clear();
        /** @brief 쌓인 변화를 넘기고 비웁니다. */
        void drainEvents( vector<WalletEvent>& outListEvent );
        /** @brief 통화마다 이름 · 잔액을 처음 만난 순서로 씁니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 모두 바꿉니다(알림 없음). 빈 통화 이름이면 false 이고 그대로입니다(음수 잔액 — 빚 — 은 그대로 온다). */
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
    /** @brief 가게 진열 한 줄입니다. */
    struct ShopStockDef
    {
        hashed_string _itemId{};
        string        _requirement{}; ///< 잠금 조건 식(키트 · 게임이 평가 — 예: GameFlags 조건). 비면 늘 열림
        int32         _price{ -1 };   ///< 단가(−1 이면 `ItemDef::_value`)
        int32         _count{ -1 };   ///< 처음 · 최대 재고(−1 = 끝없음)
        int32         _restock{ 0 };  ///< 재입고 때 더하는 수(최대 재고까지)
    };
} // namespace sw

namespace sw
{
    /** @brief 가게 하나입니다. */
    struct SW_GF_API ShopDef
    {
        hashed_string         _id{};
        hashed_string         _currency{};             ///< 이 가게가 받고 주는 통화(비면 "Gold")
        vector<ShopStockDef>  _listStock{};            ///< 진열 순서
        vector<hashed_string> _listRefusedCategory{};  ///< 사들이지 않는 분류("Quest" · "Key")
        float32               _buyMultiplier{ 1.0f };  ///< 손님이 살 때 값에 곱한다
        float32               _sellMultiplier{ 0.5f }; ///< 손님이 팔 때 값에 곱한다
        float32               _saturation{ 0.0f };     ///< 한 개 팔릴 때마다 그 아이템 매입 시세가 떨어지는 양(0 = 시세 없음)
        float32               _minSellFactor{ 0.2f };  ///< 시세 바닥
        float32               _recoveryPerDay{ 0.1f }; ///< 하루마다 시세가 1 쪽으로 돌아오는 양
        int32                 _restockDays{ 1 };       ///< 며칠마다 재입고(0 = 안 한다)

        const ShopStockDef* findStock( const hashed_string& itemId ) const;
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
    class SW_GF_API ShopCatalog : public XmlCatalog<ShopCatalog>
    {
        friend class XmlCatalog<ShopCatalog>;

    public:
        void addShop( const ShopDef& def ) { (void)_catalog.add( def ); }

        const ShopDef*         findShop( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<ShopDef>& getShops() const { return _catalog.getAll(); }

    private:
        static constexpr const utf8* kXmlRootName = "ShopCatalog"; ///< 루트 원소(`XmlCatalog`)
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        GameCatalog<ShopDef> _catalog{};
    };
} // namespace sw

namespace sw
{
    /** @brief 사기 · 팔기 결과입니다. */
    enum class ShopResult : uint8
    {
        Ok = 0,
        UnknownShop,
        UnknownItem, ///< 가게가 팔지 않는(사기) · 카탈로그에 없는(팔기) 아이템
        InvalidCount,
        Locked, ///< 진열 줄의 조건이 맞지 않는다
        OutOfStock,
        NotEnoughMoney,
        NoRoom,  ///< 인벤토리에 자리(칸 · 무게)가 없다
        Refused, ///< 가게가 그 분류를 사들이지 않는다
        NotOwned ///< 팔 만큼 가지고 있지 않다
    };

    SW_GF_API const utf8* toString( ShopResult result );

    /**
     * @class IShopConditionEvaluator
     * @brief 진열 줄의 `requires` 식을 평가합니다. 가게는 식을 보관만 하고 뜻은 모릅니다 — 키트나 게임이 플래그 · 퀘스트 · 평판으로 답합니다.
     */
    class SW_GF_API IShopConditionEvaluator
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
    /** @brief 가게에서 일어난 일입니다. */
    struct ShopEvent
    {
        enum class Kind : uint8
        {
            Bought = 0, ///< 손님이 샀다
            Sold,       ///< 손님이 팔았다
            Restocked   ///< 재입고(아이템마다 하나)
        };
        hashed_string _shopId{};
        hashed_string _itemId{};
        int64         _money{ 0 }; ///< 낸 돈 · 받은 돈
        int32         _count{ 0 };
        Kind          _kind{ Kind::Bought };
    };
} // namespace sw

namespace sw
{
    /** @brief 가게 하나의 지금 상태입니다. */
    struct ShopRuntime
    {
        hashed_string         _shopId{};
        vector<int32>         _listStockCount{};    ///< `ShopDef::_listStock` 과 같은 자리(−1 = 끝없음)
        vector<hashed_string> _listSaturatedItem{}; ///< 시세가 떨어진 아이템
        vector<float32>       _listSellFactor{};    ///< 그 아이템의 시세(1 = 보통)
        vector<hashed_string> _listExtraRefused{};  ///< 실행 중에 더한 "사들이지 않는 분류"
        float32               _buyModifier{ 1.0f };
        float32               _sellModifier{ 1.0f };
        int32                 _daysSinceRestock{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShopState
     * @brief 모든 가게의 재고 · 시세 · 가격 배율입니다. 카탈로그 둘은 빌려 씁니다(이 객체보다 오래 살아야 한다).
     * @details 사기 단가 = round( 값 × 가게 `buyMultiplier` × 사기 배율 ), 팔기는 한 개씩 round( `ItemDef::_value` × `sellMultiplier` × 팔기 배율 × 시세 ) 를 더하고
     *          한 개마다 시세가 `saturation` 만큼 떨어집니다(바닥 `minSellFactor`). `advanceDay` 가 시세를 되돌리고 `restockDays` 마다 재입고합니다.
     *          사기 · 팔기는 다 되거나 아무것도 바뀌지 않습니다.
     */
    class SW_GF_API ShopState
    {
    public:
        ShopState();

        void initialize( const ShopCatalog* pShopCatalog, const ItemCatalog* pItemCatalog );
        /** @brief 잠금 조건을 평가할 쪽입니다. 없으면 조건이 있는 줄은 늘 잠겨 있습니다. */
        void setConditionEvaluator( const IShopConditionEvaluator* pEvaluator ) { _pConditionEvaluator = pEvaluator; }
        /** @brief 가격 배율 — 평판 · 할인 · 그날 매입률(리썰 컴퍼니). 사기와 팔기를 따로 둡니다. */
        void setPriceModifier( const hashed_string& shopId, float32 buyModifier, float32 sellModifier );
        /** @brief 이 가게가 @p category 를 더는 사들이지 않게 합니다. */
        void refuseCategory( const hashed_string& shopId, const hashed_string& category );

        ShopResult evaluateBuy( const hashed_string& shopId, const hashed_string& itemId, int32 count, const Wallet& wallet, const Inventory& inventory ) const;
        /** @brief 삽니다. @p pOutPaid 가 있으면 실제로 낸 금액(성공일 때만)을 적습니다. */
        ShopResult buy( const hashed_string& shopId, const hashed_string& itemId, int32 count, Wallet& wallet, Inventory& inventory, int64* pOutPaid = nullptr );
        ShopResult evaluateSell( const hashed_string& shopId, const hashed_string& itemId, int32 count, const Inventory& inventory ) const;
        /** @brief 팝니다. @p pOutReceived 가 있으면 실제로 받은 금액(성공일 때만)을 적습니다. */
        ShopResult sell( const hashed_string& shopId, const hashed_string& itemId, int32 count, Wallet& wallet, Inventory& inventory, int64* pOutReceived = nullptr );
        /** @brief 하루를 넘깁니다 — 시세 회복과(때가 되면) 재입고입니다. */
        void advanceDay();
        void drainEvents( vector<ShopEvent>& outListEvent );

        /** @brief 사는 단가입니다. 가게가 팔지 않으면 −1 입니다. */
        int32 computeBuyPrice( const hashed_string& shopId, const hashed_string& itemId ) const;
        /** @brief @p count 개를 지금 팔면 받는 돈입니다(시세 하락 포함). */
        int64 computeSellTotal( const hashed_string& shopId, const hashed_string& itemId, int32 count ) const;
        /** @brief 남은 재고입니다(−1 = 끝없음, 진열에 없으면 0). */
        int32 getStockCount( const hashed_string& shopId, const hashed_string& itemId ) const;
        /** @brief 그 아이템의 매입 시세(1 = 보통)입니다. */
        float32 getSellFactor( const hashed_string& shopId, const hashed_string& itemId ) const;
        /** @brief 진열 줄이 열려 있는가입니다(조건 없음 또는 평가가 참). */
        bool               isUnlocked( const ShopStockDef& stock ) const;
        hashed_string      getCurrency( const hashed_string& shopId ) const;
        const ShopRuntime* findRuntime( const hashed_string& shopId ) const;

        /** @brief 가게마다 런타임(가게 id · 재고 · 시세 · 사들이지 않는 분류 · 가격 배율 · 재입고 뒤 날)을 씁니다. 카탈로그는 `initialize` 의 것이라 싣지 않습니다. */
        void writeState( Archive& outArchive ) const;
        /** @brief `writeState` 의 바이트로 바꿉니다. 깨졌으면 false 이고 그대로입니다. */
        [[nodiscard]] bool readState( Archive& archive );

    private:
        ShopRuntime* findRuntime( const hashed_string& shopId );
        int32        findStockIndex( const ShopDef& shop, const hashed_string& itemId ) const;
        int32        computeBasePrice( const ShopStockDef& stock ) const;
        bool         isRefused( const ShopDef& shop, const ShopRuntime& runtime, const hashed_string& itemId ) const;

        vector<ShopRuntime>            _listRuntime;
        EventBuffer<ShopEvent>         _eventBuffer;
        const ShopCatalog*             _pShopCatalog;
        const ItemCatalog*             _pItemCatalog;
        const IShopConditionEvaluator* _pConditionEvaluator;
    };
} // namespace sw
