/**
 * @file OfferCatalog.h
 * @brief 상품(가격표) — `<OfferCatalog><Offer id="starter_pack" limit="1" maxCount="1" startMs="0" endMs="0"><Price currency="cur.gem" amount="100"/>
 *        <Grant asset="item.sword" amount="1"/><Grant asset="cur.gold" amount="500"/></Offer><Offer id="gem_100"><Product store="apple" id="com.x.gem100"/>
 *        <Grant asset="cur.gem_paid" amount="100"/></Offer></OfferCatalog>`.
 * @details 상품은 가격(`<Price>` — 화폐로 산다)이나 스토어 상품(`<Product>` — 결제 영수증으로 받는다) 중 하나만 갖는다. 지급은 하나 이상.
 *          `limit` 는 계정당 평생 구매 수(0 = 없음), `maxCount` 는 한 번에 살 수 있는 수, `startMs`/`endMs` 는 판매 기간(유닉스 밀리초, 0 = 열림, 끝은 열린 구간).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 가격 다리 하나 — 화폐(가상이면 재원 순서로) · 양입니다. */
    struct OfferPrice
    {
        string _currencyId{};
        int64  _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 지급 다리 하나 — 자산 · 양입니다. */
    struct OfferGrant
    {
        string _assetId{};
        int64  _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 스토어 상품 하나입니다(스토어 이름 · 스토어의 상품 id). */
    struct OfferProduct
    {
        string _storeName{};
        string _productId{};
    };
} // namespace sw

namespace sw
{
    /** @brief 상품 하나입니다. */
    struct OfferDef
    {
        vector<OfferPrice>   _listPrice{};
        vector<OfferGrant>   _listGrant{};
        vector<OfferProduct> _listProduct{};
        string               _id{};
        int64                _startMs{ 0 };
        int64                _endMs{ 0 };
        int32                _limitPerAccount{ 0 };
        int32                _maxCountPerPurchase{ 1 };

        bool isRealMoney() const { return _listProduct.empty() == false; }
        bool isOnSale( int64 nowMs ) const { return ( _startMs == 0 || _startMs <= nowMs ) && ( _endMs == 0 || nowMs < _endMs ); }
    };
} // namespace sw

namespace sw
{
    /** @class OfferCatalog @brief 상품 모음입니다. 읽은 뒤에는 바꾸지 않는다(저장소 스레드가 읽는다). */
    class SW_GF_API OfferCatalog : public XmlCatalog<OfferCatalog>
    {
        friend class XmlCatalog<OfferCatalog>;

    public:
        /** @brief 더합니다(시험). 규칙(가격 xor 상품, 지급 하나 이상, id 글자, 같은 id 없음)을 어기면 false 입니다. */
        [[nodiscard]] bool addOffer( const OfferDef& def );

        const OfferDef*         findOffer( string_view id ) const;
        const OfferDef*         findOfferByProduct( string_view storeName, string_view productId ) const;
        const vector<OfferDef>& getOffers() const { return _listOffer; }

    private:
        static constexpr const utf8* kXmlRootName = "OfferCatalog";
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        vector<OfferDef> _listOffer{};
    };
} // namespace sw
