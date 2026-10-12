#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyService.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Fake/FakeReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/OfferCatalog.h"

// 경제 키트의 서버 조립 — 화폐 · 상품 카탈로그를 읽어 경제 서비스를 올리고, 화폐 카탈로그를 원장 정책(계정 잔액 상한)으로 문맥에 내놓는다(우편함 · GM · 거래가 쓴다).
// 옵션:
//   currencyCatalog        화폐 카탈로그(리소스 경로, `<CurrencyCatalog>`). 필수.
//   offerCatalog           상품 카탈로그(리소스 경로, `<OfferCatalog>`). 필수.
//   fakeReceipts           1 이면 가짜 영수증 제공자("fake")를 올린다(Dev 만 — Shipping 경제 서비스가 거절한다).
//   acceptSandboxReceipts  1 이면 스토어의 샌드박스 영수증을 받는다(스테이징 서버).

namespace sw
{
    namespace
    {
        /**
         * @class EconomyOnlineServerKit
         * @brief 경제 키트의 서버 조립입니다. 카탈로그는 저장소 스레드가 읽으므로 서비스보다 오래 살고 읽은 뒤 바꾸지 않습니다.
         */
        class EconomyOnlineServerKit final : public IOnlineServerKit
        {
        public:
            EconomyOnlineServerKit()
                : _currencyCatalog{}
                , _offerCatalog{}
                , _fakeValidator{}
                , _receiptRegistry{}
                , _service{}
                , _bServiceStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                const string* pUnknown = entry.findUnknownOption( { "currencyCatalog", "offerCatalog", "fakeReceipts", "acceptSandboxReceipts" } );
                if ( pUnknown != nullptr )
                {
                    outError = "unknown option '" + *pUnknown + "'";
                    return false;
                }
                const string* pCurrencyPath = entry.findOption( "currencyCatalog" );
                const string* pOfferPath    = entry.findOption( "offerCatalog" );
                if ( pCurrencyPath == nullptr || pOfferPath == nullptr )
                {
                    outError = "the economy kit needs the currencyCatalog and offerCatalog options";
                    return false;
                }
                if ( _currencyCatalog.loadFromResource( *pCurrencyPath ) == false || _offerCatalog.loadFromResource( *pOfferPath ) == false )
                {
                    outError = "the economy catalogs could not be read ('" + *pCurrencyPath + "', '" + *pOfferPath + "')";
                    return false;
                }
                bool       bFakeReceipts  = false;
                bool       bAcceptSandbox = false;
                const bool bOptionOk      = entry.readBoolOption( "fakeReceipts", bFakeReceipts, outError ) && entry.readBoolOption( "acceptSandboxReceipts", bAcceptSandbox, outError );
                if ( bOptionOk == false )
                    return false;
                if ( bFakeReceipts && _receiptRegistry.registerValidator( &_fakeValidator ) == false )
                {
                    outError = "the fake receipt validator could not be registered";
                    return false;
                }
                EconomyServiceSettings settings;
                settings._pCurrencyCatalog       = &_currencyCatalog;
                settings._pOfferCatalog          = &_offerCatalog;
                settings._pReceiptRegistry       = &_receiptRegistry;
                settings._pMetricRegistry        = context._pMetricRegistry;
                settings._bAcceptSandboxReceipts = bAcceptSandbox ? SW_TRUE : SW_FALSE;
                if ( _service.initialize( context._pStore, settings ) == false )
                {
                    outError = "the economy service refused its settings (see the error above)";
                    return false;
                }
                _bServiceStarted = true;
                if ( context._pHost->registerHostService( &_service ) == false )
                {
                    outError = "the economy method range is already taken";
                    return false;
                }
                context._pLedgerPolicy = &_currencyCatalog;
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                (void)context;
                (void)outError;
                return true;
            }

            void stop() override
            {
                // 맡긴 저장 일과 영수증 검증을 거둔다 — 저장소를 내리기 전.
                if ( _bServiceStarted )
                    _service.shutdown();
                _bServiceStarted = false;
                _receiptRegistry.shutdown();
            }

        private:
            CurrencyCatalog          _currencyCatalog;
            OfferCatalog             _offerCatalog;
            FakeReceiptValidator     _fakeValidator;
            ReceiptValidatorRegistry _receiptRegistry;
            EconomyService           _service;
            bool                     _bServiceStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( EconomyOnlineServerKit, "economy", OnlineServerKitOrder::kEconomy );
} // namespace sw
