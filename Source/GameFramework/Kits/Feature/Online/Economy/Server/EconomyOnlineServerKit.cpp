#include "pch.h"

#include "Core/File/FileUtil.h"

#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyService.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/Provider/Fake/FakeReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptProviderSet.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Shared/Catalog/OfferCatalog.h"

// 경제 키트의 서버 조립 — 화폐 · 상품 카탈로그를 읽어 경제 서비스를 만들고, 화폐 카탈로그를 원장 정책(계정 잔액 상한)으로 문맥에 내놓는다(우편함 · GM · 거래가 쓴다).
// 옵션:
//   currencyCatalog        화폐 카탈로그(리소스 경로, `<CurrencyCatalog>`). 필수.
//   offerCatalog           상품 카탈로그(리소스 경로, `<OfferCatalog>`). 필수.
//   fakeReceipts           1 이면 가짜 영수증 제공자("fake")를 등록한다(Dev 만 — Shipping 경제 서비스가 거절한다).
//   acceptSandboxReceipts  1 이면 스토어의 샌드박스 영수증을 받는다(스테이징 서버).
//   receiptProvidersFile   실제 영수증 제공자 설정 JSON(프로젝트 루트 기준, 형식은 `ReceiptProviderSet::readSettings`). 비우면 실제 제공자가 없다 —
//                          있으면 자격(키 · 서비스 계정)을 환경 변수에서 읽고, 읽지 못하면 서버 기동이 실패한다(OnlineKits H3).
//   receiptTrustFile       제공자 `https://` 서버를 검증할 CA 번들 PEM. `receiptProvidersFile` 이 https 주소를 쓰면 필수다.

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
                , _receiptProviders{}
                , _receiptRegistry{}
                , _service{}
                , _bServiceStarted{ false }
            {
            }

            [[nodiscard]] bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                string     currencyPath;
                string     offerPath;
                string     providersFile;
                string     trustFile;
                bool       bFakeReceipts  = false;
                bool       bAcceptSandbox = false;
                const bool bOptionOk      = entry.readOptions( {
                                                              {      "currencyCatalog",   currencyPath},
                                                              {         "offerCatalog",      offerPath},
                                                              {         "fakeReceipts",  bFakeReceipts},
                                                              {"acceptSandboxReceipts", bAcceptSandbox},
                                                              { "receiptProvidersFile",  providersFile},
                                                              {     "receiptTrustFile",      trustFile}
                },
                                                               outError );
                if ( bOptionOk == false )
                    return false;
                if ( currencyPath.empty() || offerPath.empty() )
                {
                    outError = "the economy kit needs the currencyCatalog and offerCatalog options";
                    return false;
                }
                if ( _currencyCatalog.loadFromResource( currencyPath ) == false || _offerCatalog.loadFromResource( offerPath ) == false )
                {
                    outError = "the economy catalogs could not be read ('" + currencyPath + "', '" + offerPath + "')";
                    return false;
                }
                if ( bFakeReceipts && _receiptRegistry.registerValidator( &_fakeValidator ) == false )
                {
                    outError = "the fake receipt validator could not be registered";
                    return false;
                }
                if ( createReceiptProviders( providersFile, trustFile, outError ) == false )
                    return false;
                EconomyServiceConfig settings;
                settings._pCurrencyCatalog       = &_currencyCatalog;
                settings._pOfferCatalog          = &_offerCatalog;
                settings._pReceiptRegistry       = &_receiptRegistry;
                settings._pMetricRegistry        = context._pMetricRegistry;
                settings._pScheduler             = context._pScheduler; // 원장 보존 검사를 매일 서버 하나가 돌린다
                settings._pPresence              = context._pPresence;  // 계정 키트(다른 서버에 붙은 계정에 잔액 변경 알림을 중계)
                settings._bAcceptSandboxReceipts = bAcceptSandbox ? SW_TRUE : SW_FALSE;
                if ( _service.initialize( context._pStore, settings ) == false )
                {
                    outError = "the economy service refused its settings (see the error above)";
                    return false;
                }
                _bServiceStarted = true;
                if ( registerHostService( context, entry, &_service, outError ) == false )
                    return false;
                context._pLedgerPolicy  = &_currencyCatalog;
                context._pBalanceNotice = &_service; // GM 키트가 지급 · 회수 뒤에 부른다
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
                // 맡긴 저장 일과 영수증 검증을 회수한다 — 저장소를 종료하기 전.
                if ( _bServiceStarted )
                    _service.shutdown();
                _bServiceStarted = false;
                _receiptRegistry.shutdown();
            }

            void shutdown() override
            {
                // 저장소를 종료하고 남은 완료를 돌린 뒤다 — 레지스트리가 빌려 쓰던 제공자를 내린다.
                _receiptProviders.shutdown();
            }

        private:
            /** @brief 설정 파일을 읽어 실제 영수증 제공자를 만들고 레지스트리에 등록합니다. 파일을 정하지 않았으면 실제 제공자가 없습니다. */
            [[nodiscard]] bool createReceiptProviders( const string& providersFile, const string& trustFile, string& outError )
            {
                if ( providersFile.empty() )
                {
                    if ( trustFile.empty() == false )
                    {
                        outError = "receiptTrustFile is set but receiptProvidersFile is not";
                        return false;
                    }
                    return true;
                }
                const string providersPath = NetSecurity::makeProjectPath( providersFile );
                string       jsonText;
                if ( FileUtil::readTextFile( providersPath, jsonText ) == false )
                {
                    outError = "cannot read the receipt providers file " + providersPath;
                    return false;
                }
                vector<ReceiptProviderConfig> listSettings;
                if ( ReceiptProviderSet::readSettings( jsonText, listSettings, outError ) == false || _receiptProviders.initialize( listSettings, trustFile, outError ) == false )
                    return false;
                for ( int32 index = 0; index < _receiptProviders.getProviderCount(); ++index )
                {
                    if ( _receiptRegistry.registerValidator( _receiptProviders.getProvider( index ) ) == false )
                    {
                        outError = "receipt provider store names must be unique";
                        return false;
                    }
                }
                return true;
            }

            CurrencyCatalog          _currencyCatalog;
            OfferCatalog             _offerCatalog;
            FakeReceiptValidator     _fakeValidator;
            ReceiptProviderSet       _receiptProviders; ///< 레지스트리보다 오래 산다(먼저 선언)
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
