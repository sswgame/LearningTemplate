/**
 * @file CurrencyCatalog.h
 * @brief 화폐 정의 — `<CurrencyCatalog><Currency id="cur.gold" cap="999999999"/><Currency id="cur.gem_paid" paid="true"/><Currency id="cur.gem_free"/>
 *        <Currency id="cur.gem"><Funding asset="cur.gem_free"/><Funding asset="cur.gem_paid"/></Currency></CurrencyCatalog>`.
 * @details `<Funding>` 이 있는 화폐는 **가상**(가격 단위)이다 — 잔액이 없고, 그 가격은 재원 자산을 적힌 순서대로 쓴다(유상 · 무상 구분 — 차감 순서는 데이터,
 *          관례는 무상 먼저). 재원은 가상이 아닌 정의된 화폐여야 한다(아니면 그 가상 화폐를 버리고 경고). `ILedgerPolicy` 로 계정 상한을 답한다(`cap`, 0 = 원장 상한만).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Foundation/Data/XmlCatalog.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

    /** @brief 화폐 하나입니다. */
    struct CurrencyDef
    {
        vector<string> _listFundingAsset{}; ///< 비지 않으면 가상 화폐 — 차감 순서
        string         _id{};
        int64          _cap{ 0 };          ///< 계정 잔액 상한(0 = 원장 상한만)
        uint8          _bPaid{ SW_FALSE }; ///< 결제로 산 재화(청약철회 · 표시 구분)

        bool isVirtual() const { return _listFundingAsset.empty() == false; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class CurrencyCatalog
     * @brief 화폐 정의 모음입니다. 읽은 뒤에는 바꾸지 않는다 — 저장소 스레드가 `getBalanceCap` 을 읽는다.
     */
    class SW_GF_API CurrencyCatalog : public XmlCatalog<CurrencyCatalog>, public ILedgerPolicy
    {
        friend class XmlCatalog<CurrencyCatalog>;

    public:
        /** @brief 정의를 더합니다(시험 · 코드로 만드는 게임). 같은 id 는 바꾼다. */
        void addCurrency( const CurrencyDef& def );

        const CurrencyDef*         findCurrency( string_view id ) const;
        const vector<CurrencyDef>& getCurrencies() const { return _listCurrency; }
        int64                      getBalanceCap( string_view assetID ) const override;

    private:
        static constexpr const utf8* kXmlRootName = "CurrencyCatalog";
        uint32                       loadRoot( const XmlNode& root, string_view sourceName );

        vector<CurrencyDef> _listCurrency{};
    };
} // namespace sw
