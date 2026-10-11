/**
 * @file OfferCatalog.h
 * @brief 상품(가격표)입니다. 형식은 `<OfferCatalog><Offer id="starter_pack" limit="1" maxCount="1" startMs="0" endMs="0"><Price currency="cur.gem" amount="100"/>
 *        <Grant asset="item.sword" amount="1"/><Grant asset="cur.gold" amount="500"/></Offer><Offer id="gem_100"><Product store="apple" id="com.x.gem100"/>
 *        <Grant asset="cur.gem_paid" amount="100"/></Offer></OfferCatalog>` 입니다.
 * @details 상품은 가격(`<Price>`, 화폐로 산다)과 스토어 상품(`<Product>`, 결제 영수증으로 받는다) 중 하나만 가진다. 지급은 하나 이상이다.
 *          `limit` 는 계정당 평생 구매 수이며 0이면 제한이 없다. `maxCount` 는 한 번에 살 수 있는 수이다.
 *          `startMs` 와 `endMs` 는 판매 기간이며 유닉스 밀리초이다. 0이면 열려 있고 끝은 열린 구간이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 가격 항목 하나입니다. 화폐(가상이면 재원 순서로)와 양을 가집니다. */
    struct OfferPrice
    {
        string _currencyID{};
        int64  _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 지급 항목 하나입니다. 자산과 양을 가집니다. */
    struct OfferGrant
    {
        string _assetID{};
        int64  _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 스토어 상품 하나입니다. 스토어 이름과 스토어의 상품 id 를 가집니다. */
    struct OfferProduct
    {
        string _storeName{};
        string _productID{};
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
    /** @class OfferCatalog @brief 상품 모음입니다. 읽은 뒤에는 바꾸지 않습니다. 저장소 스레드가 읽기 때문입니다. */
    class SW_GF_API OfferCatalog : public XMLCatalog<OfferCatalog>
    {
        friend class XMLCatalog<OfferCatalog>;

    public:
        /** @brief 상품을 추가합니다. 테스트에서 씁니다. 규칙을 어기면 false 를 반환합니다. 규칙은 가격과 스토어 상품 중 하나만 있을 것, 지급이 하나 이상일 것, id 에 쓸 수 있는 글자만 쓸 것, 같은 id 가 없을 것입니다. */
        [[nodiscard]] bool addOffer( const OfferDef& def );

        const OfferDef*         findOffer( string_view id ) const;
        const OfferDef*         findOfferByProduct( string_view storeName, string_view productID ) const;
        const vector<OfferDef>& getOffers() const { return _listOffer; }

    private:
        static constexpr const utf8* kXMLRootName = "OfferCatalog";
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

        vector<OfferDef> _listOffer{};
    };
} // namespace sw
