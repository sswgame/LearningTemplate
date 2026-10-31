/**
 * @file CurrencyCatalog.h
 * @brief 화폐 정의입니다. 형식은 `<CurrencyCatalog><Currency id="cur.gold" cap="999999999"/><Currency id="cur.gem_paid" paid="true"/><Currency id="cur.gem_free"/>
 *        <Currency id="cur.gem"><Funding asset="cur.gem_free"/><Funding asset="cur.gem_paid"/></Currency></CurrencyCatalog>` 입니다.
 * @details `<Funding>` 이 있는 화폐는 **가상** 화폐(가격 단위)이다. 가상 화폐에는 잔액이 없고, 그 가격은 재원 자산을 적힌 순서대로 차감한다.
 *          유상과 무상을 구분하며 차감 순서는 데이터가 정한다. 관례는 무상을 먼저 쓰는 것이다.
 *          재원은 가상이 아닌, 정의된 화폐여야 한다. 아니면 그 가상 화폐를 버리는 오류다.
 *          멤버로 든 `ILedgerPolicy`(`getLedgerPolicy`)로 계정 상한을 답한다(`cap`, 0이면 원장 상한만 적용한다).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Foundation/GameData/XMLCatalog.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief `<Funding>` 한 줄의 스키마입니다. 가상 화폐가 차감할 재원 자산입니다. */
    REFLECT()
    struct CurrencyFundingDef
    {
        REFLECT_BODY();

        PROPERTY( Required )
        string _asset{};
    };
} // namespace sw

namespace sw
{
    /** @brief 화폐 하나입니다. */
    REFLECT()
    struct CurrencyDef
    {
        REFLECT_BODY();

        vector<string> _listFundingAsset{}; ///< 비어 있지 않으면 가상 화폐이다. 차감 순서를 나타낸다(`<Funding>` 자식에서 읽는다)
        PROPERTY( Required )
        string _id{};
        PROPERTY( Min = 0 )
        int64 _cap{ 0 }; ///< 계정 잔액 상한이다. 0이면 원장 상한만 적용한다
        PROPERTY()
        bool _bPaid{ false }; ///< 결제로 산 재화이다. 청약 철회와 표시 구분에 쓴다

        bool isVirtual() const { return _listFundingAsset.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class CurrencyCatalog
     * @brief 화폐 정의 모음입니다. 읽은 뒤에는 바꾸지 않습니다. 저장소 스레드가 `getBalanceCap` 을 읽기 때문입니다.
     */
    class SW_KIT_API CurrencyCatalog : public XMLCatalog<CurrencyCatalog>
    {
        friend class XMLCatalog<CurrencyCatalog>;

    public:
        CurrencyCatalog();

        // 상한 정책(`_ledgerPolicy`)이 이 객체를 참조로 든다 — 옮기면 옛 주소를 가리킨다.
        CurrencyCatalog( const CurrencyCatalog& )            = delete;
        CurrencyCatalog& operator=( const CurrencyCatalog& ) = delete;

        /** @brief 정의를 추가합니다. 테스트와 코드로 정의를 만드는 게임이 씁니다. 같은 id 는 교체합니다. */
        void addCurrency( const CurrencyDef& def );

        const CurrencyDef*         findCurrency( string_view id ) const;
        const vector<CurrencyDef>& getCurrencies() const { return _listCurrency; }
        /** @brief @p assetID 의 계정 잔액 상한입니다. 정의가 없거나 0이면 0(원장 상한만 적용)입니다. */
        int64 getBalanceCap( string_view assetID ) const;
        /** @brief 원장에 넘기는 상한 정책입니다(`getBalanceCap` 으로 넘긴다). 이 카탈로그와 수명이 같습니다. 키트 범위에 거는 쪽은 비 const 판을 쓴다(`bind` 가 const 를 떼지 않는다). */
        ILedgerPolicy&       getLedgerPolicy() { return _ledgerPolicy; }
        const ILedgerPolicy& getLedgerPolicy() const { return _ledgerPolicy; }

    private:
        /** @brief 카탈로그의 `getBalanceCap` 을 원장의 상한 정책으로 내보입니다. 카탈로그가 멤버로 들어 수명이 같다. */
        class LedgerPolicy final : public ILedgerPolicy
        {
        public:
            explicit LedgerPolicy( const CurrencyCatalog& catalog )
                : _catalog{ catalog }
            {
            }

            int64 getBalanceCap( string_view assetID ) const override { return _catalog.getBalanceCap( assetID ); }

        private:
            const CurrencyCatalog& _catalog;
        };

        static constexpr const utf8* kXMLRootName = "CurrencyCatalog";
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );
        /** @brief `<Currency>` 와 그 `<Funding>` 을 스키마로 읽습니다. 오류면 넣지 않고 false 입니다(오류는 이름과 함께 남긴다). */
        [[nodiscard]] bool loadCurrency( const XMLNode& node, string_view sourceName );

        vector<CurrencyDef> _listCurrency;
        LedgerPolicy        _ledgerPolicy; ///< 원장 상한 정책(`getLedgerPolicy`)
    };
} // namespace sw
