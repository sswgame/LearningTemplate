/**
 * @file ShopPurchase.h
 * @brief 상점 화면이 쓰는 "구매 공급자" 형식입니다. 같은 상점 화면(`ShopScreen`)에 오프라인 공급자(`LocalShopPurchaseProvider` — 지갑 · 가게 · 인벤토리)와
 *        온라인 공급자(`EconomyShopPurchaseProvider` — 경제 키트 서버 구매)를 갈아 끼운다(합성).
 * @details 유니티 Gaming Services Economy 클라이언트의 흐름(가상 구매 목록 → `MakeVirtualPurchaseAsync` → 결과 · 잔액 갱신)과 같은 나눔이다. 화면은 공급자가 낸 목록 · 잔액만 그리고,
 *          구매의 정본은 공급자다(온라인이면 서버 — 실패해도 클라이언트가 재화를 되돌리지 않는다). 구매는 비동기이며 답은 델리게이트로 정확히 한 번 온다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class Inventory;
    class ItemCatalog;
    class ShopCatalog;
    class ShopManager;
    class Wallet;
} // namespace sw

namespace sw
{
    /** @brief 상점 목록의 상품 한 줄입니다. 공급자가 채우고 화면은 읽기만 합니다. */
    struct SW_GF_API ShopOffer
    {
        static constexpr int32 kUnlimitedStock = -1; ///< `_stock` 이 이 값이면 재고가 무한이다

        string        _offerID{};     ///< 공급자 안의 id(오프라인은 아이템 id, 온라인은 상품 id)
        string        _displayName{}; ///< 보일 이름(현지화 키이거나 원문)
        hashed_string _currency{};    ///< 값을 치르는 재화(오프라인 지갑 통화 · 온라인 원장 자산 `cur.*`)
        int64         _unitPrice{ 0 };
        int32         _maxCount{ 1 };            ///< 한 번에 살 수 있는 수
        int32         _stock{ kUnlimitedStock }; ///< 남은 재고

        /**
         * @brief @p count 개를 살 때의 합계를 @p outTotal 에 씁니다. 수량이 1 ~ `_maxCount` 밖이거나, 단가가 음수이거나, 곱이 int64 를 넘으면 false 입니다.
         * @details 화면 · 서버 응답 어느 쪽의 값도 믿기 전에 이 검사를 거친다 — 넘친 곱이 음수 값이 되어 재화가 느는 결함을 형식 한 곳에서 막는다.
         */
        [[nodiscard]] bool computeTotal( int32 count, int64& outTotal ) const;
    };

    /** @brief 구매 하나의 끝입니다. 로그 · 감시 값의 이름은 `getEnumName` 입니다. */
    ENUM()
    enum class ShopPurchaseStatus : uint8
    {
        Succeeded = 0,
        NotEnoughMoney,
        OutOfStock,
        LimitReached, ///< 계정당 구매 한도
        NoRoom,       ///< 인벤토리에 자리가 없다
        Rejected,     ///< 공급자가 거절했다(판매 기간 밖 · 모르는 상품 · 잠김 · 상한)
        ServerError,  ///< 서버에 닿지 못했거나 일시 장애다(재화는 서버가 정본 — 클라이언트는 되돌리지 않는다)
        Unverified,   ///< 서버는 성공이라 했지만 답이 요청과 맞지 않았다(가격 · 자산 · 범위) — 지갑을 서버에서 다시 받는다
        Busy          ///< 앞 구매의 답을 기다리는 중이다(중복 누름)
    };

    /** @brief 구매 하나의 결과(영수증)입니다. */
    struct ShopPurchaseReceipt
    {
        string             _offerID{};
        string             _receiptID{}; ///< 공급자가 붙인 거래 표지(온라인은 멱등 키 — 같은 키의 재시도는 서버가 한 번만 적용한다). 실패면 비어 있을 수 있다
        int64              _paid{ 0 };   ///< 치른 값(실패면 0)
        int32              _count{ 0 };
        ShopPurchaseStatus _status{ ShopPurchaseStatus::Rejected };
    };

    using ShopPurchaseDelegate = Delegate<void( const ShopPurchaseReceipt& receipt )>;

} // namespace sw

namespace sw
{
    /**
     * @class IShopPurchaseProvider
     * @brief 상점 화면이 부르는 구매 공급자입니다. 게임 스레드에서만 씁니다.
     */
    class IShopPurchaseProvider
    {
    public:
        IShopPurchaseProvider() = default;
        /** @brief 화면이 묶여 있으면 공급자가 사라진다고 알립니다 — 화면은 공급자를 놓고 닫힌다(화면이 공급자보다 오래 살아 날 포인터로 부르지 않게). */
        virtual ~IShopPurchaseProvider()
        {
            if ( _onReleased.isBound() )
                _onReleased();
        }

        IShopPurchaseProvider( const IShopPurchaseProvider& )            = delete;
        IShopPurchaseProvider& operator=( const IShopPurchaseProvider& ) = delete;

        /** @brief 지금 살 수 있는 상품을 진열 순서로 @p outListOffer 에 씁니다(앞 내용은 지운다). */
        virtual void collectOffers( vector<ShopOffer>& outListOffer ) const = 0;
        /** @brief @p currency 의 지금 잔액입니다. */
        virtual int64 getBalance( const hashed_string& currency ) const = 0;
        /** @brief 목록 · 잔액이 바뀔 때마다 오르는 번호입니다. 화면이 다시 그릴지 정합니다. */
        virtual uint32 getRevision() const = 0;
        /** @brief 앞 구매의 답을 기다리는 중이면 true 입니다. */
        virtual bool isPurchasePending() const = 0;
        /**
         * @brief @p offer 를 @p count 개 삽니다. 답은 @p onFinished 로 정확히 한 번 옵니다(오프라인은 이 안에서 바로).
         * @details 기다리는 구매가 있으면 시작하지 않고 `Busy` 로 바로 답한다(중복 누름). 수량 · 합계 검사(`ShopOffer::computeTotal`)를 먼저 한다.
         */
        virtual void beginPurchase( const ShopOffer& offer, int32 count, const ShopPurchaseDelegate& onFinished ) = 0;

        /**
         * @brief 이 공급자를 쓰는 화면을 겁니다. 공급자가 먼저 사라지면 @p onReleased 가 불린다. 화면은 하나 — 다시 걸면 앞 화면의 묶기를 갈아 끼운다.
         * @details 공급자를 소유한 쪽(게임 인스턴스의 서비스)은 화면 시스템보다 먼저 내려갈 수 있다(인스턴스 재시작 · 레벨 이동). 화면은 닫힐 때 `unbindScreen` 을 부른다.
         */
        void bindScreen( Delegate<void()> onReleased ) { _onReleased = std::move( onReleased ); }
        /** @brief 화면 묶기를 풉니다(화면이 먼저 닫힐 때). */
        void unbindScreen() { _onReleased = Delegate<void()>{}; }

    private:
        Delegate<void()> _onReleased{}; ///< 묶인 화면에 공급자가 사라짐을 알린다
    };
} // namespace sw

namespace sw
{
    /**
     * @class LocalShopPurchaseProvider
     * @brief 오프라인 상점의 공급자입니다. 가게(`ShopManager` — 재고 · 가격 배율 · 잠금)에서 지갑으로 사서 인벤토리에 넣습니다. 넷 모두 빌려 쓰며 이 객체보다 오래 살아야 합니다.
     * @details 사기는 `ShopManager::buy` 하나라 전부 되거나 아무것도 바뀌지 않는다. 목록은 가게 카탈로그의 진열 순서이고, 잠긴 항목은 빠진다. 보일 이름은 아이템 정의의 `_name` 이다.
     */
    class SW_GF_API LocalShopPurchaseProvider final : public IShopPurchaseProvider
    {
    public:
        LocalShopPurchaseProvider( const ShopCatalog& shops, const ItemCatalog& items, ShopManager& shop, Wallet& wallet, Inventory& inventory, const hashed_string& shopID );

        void   collectOffers( vector<ShopOffer>& outListOffer ) const override;
        int64  getBalance( const hashed_string& currency ) const override;
        uint32 getRevision() const override;
        bool   isPurchasePending() const override { return false; }
        void   beginPurchase( const ShopOffer& offer, int32 count, const ShopPurchaseDelegate& onFinished ) override;

    private:
        const ShopCatalog& _shops;
        const ItemCatalog& _items;
        ShopManager&       _shop;
        Wallet&            _wallet;
        Inventory&         _inventory;
        hashed_string      _shopID;
        uint32             _purchaseCount; ///< 끝난 구매 수(재고가 바뀐 것을 화면이 알게 번호에 더한다)
    };
} // namespace sw
