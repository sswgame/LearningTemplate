/**
 * @file GameFramework/Client/Shop/ShopScreen.h
 * @brief 상점 화면입니다. 구매 공급자(`IShopPurchaseProvider` — 오프라인 · 온라인)의 상품 목록을 줄마다 단추로 보이고, 고르면 확인 대화 상자 → 구매 → 결과 글을 보입니다.
 * @details 문서(`.ui.xml`)는 호출하는 게임이 경로를 준다. 갖출 위젯: 상품 줄을 담을 가상 목록 `Offers`(`ListViewWidget`), 잔액 글 `Balance`, 상태 글 `Status`, 명령 `Close` 단추
 *          (견본: `Content/common/ui/shop/shop.ui.xml`). 화면 스택 · 확인 대화 상자 · 패드 포커스는 엔진 런타임 UI 의 것을 그대로 쓴다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/UI/Menu/ConfirmDialogScreen.h"
#include "Engine/UI/Screen/UIScreen.h"

#include "GameFramework/Client/Shop/ShopViewModel.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ShopPurchase.h"

namespace sw
{
    class UISystem;

    /**
     * @class ShopScreen
     * @brief 상점 목록 → 구매 확인 → 결과 화면입니다(상용 상점의 목록 · 상세 · 확인 · 결과 · 재화 표시 흐름).
     * @details - 구매 확인 · 답을 기다리는 동안 새 고르기를 버린다(중복 누름). 공급자도 기다리는 구매가 있으면 `Busy` 로 답한다 — 두 겹이다.
     *          - 잔액이 모자라면 확인 창을 띄우지 않고 상태 글로 알린다(공급자 잔액 기준 — 온라인이면 서버 원장의 사본). 그래도 공급자 · 서버가 정본이라 그 답을 그대로 보인다.
     *          - 실패해도 화면은 재화를 되돌리지 않는다(온라인은 서버가 정본). 잔액은 공급자의 갱신 번호가 바뀔 때마다 다시 읽는다.
     *          - 답은 화면 핸들로 찾는다 — 답이 오기 전에 화면이 닫혔으면 결과를 로그로만 남긴다.
     *          - 공급자는 화면보다 오래 살아야 합니다(빌려 쓴다).
     */
    REFLECT( Abstract )
    class SW_GF_API ShopScreen : public UIScreen
    {
    public:
        REFLECT_BODY();

        static constexpr const utf8* kOffersName   = "Offers";
        static constexpr const utf8* kPickCommand  = "Pick";
        static constexpr const utf8* kCloseCommand = "Close";

        const TypeInfo* getTypeInfo() const override { return StaticType(); }

        ShopScreen( const UIScreenDef& desc, unique_ptr<Widget> root );
        ~ShopScreen() override;

        /** @brief 문서 @p documentPath 로 화면을 열고 공급자 @p provider 를 겁니다. 문서를 열지 못하면 무효 핸들입니다. */
        [[nodiscard]] static UIScreenHandle open( UISystem& ui, string_view documentPath, IShopPurchaseProvider& provider );

        bool onCommand( const hashed_string& command, Widget& source ) override;
        /** @brief 공급자의 갱신 번호가 바뀌었으면 줄과 잔액을 다시 만듭니다. */
        void onTick( float32 deltaSeconds ) override;

        /** @brief 끝난 구매 수입니다(감시 값 `Shop.PurchaseCount` — 성공 · 실패 모두). */
        uint32 getFinishedCount() const { return _finishedCount; }
        /** @brief 마지막 구매 결과입니다(아직 없으면 상태가 `Rejected` 인 빈 영수증). */
        const ShopPurchaseReceipt& getLastReceipt() const { return _lastReceipt; }
        /** @brief 구매 확인 · 답을 기다리는 중이면 true 입니다. */
        bool isBusy() const { return _bBusy == SW_TRUE; }

    private:
        /** @brief 확인 대화 상자의 답입니다. 확인이면 공급자에 구매를 맡깁니다. */
        static void onConfirmed( UISystem& ui, UIScreenHandle shop, uint32 offerIndex, ConfirmDialogResult result );
        /** @brief 공급자의 구매 답입니다. 화면이 남아 있으면 결과를 보입니다. */
        static void onPurchased( UISystem& ui, UIScreenHandle shop, const ShopPurchaseReceipt& receipt );
        /** @brief 공급자가 먼저 사라졌습니다(`IShopPurchaseProvider::bindScreen`) — 공급자를 놓고 화면을 닫습니다. */
        void          onProviderReleased();
        void          applyReceipt( const ShopPurchaseReceipt& receipt );
        void          rebuild();
        void          setStatus( const string& text );
        static string makeRowText( const ShopOffer& offer );

    private:
        vector<ShopOffer>      _listOffer;
        ShopPurchaseReceipt    _lastReceipt;
        IShopPurchaseProvider* _pProvider;
        uint32                 _shownRevision;
        uint32                 _finishedCount;
        uint8                  _bBuilt;
        uint8                  _bBusy;     ///< 확인 창이 떠 있거나 구매 답을 기다린다 — 새 고르기를 버린다
        ShopViewModel          _viewModel; ///< 문서가 바인딩으로 읽는 글(`{bind:…}`) — 화면이 위젯 이름으로 찾아 쓰지 않는다
    };
} // namespace sw
