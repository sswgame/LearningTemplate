#include "pch.h"

#include "Core/Network/DevOnlineExtension.h"

#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/CurrencyCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Runtime/Catalog/OfferCatalog.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyDevOptions.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/EconomyService.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptProviderSet.h"
#include "GameFramework/Kits/Feature/Online/Economy/Server/Receipt/ReceiptValidator.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Provider/OnlineProviderFile.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// 경제 키트의 서버 조립 — 화폐 · 상품 카탈로그를 읽어 경제 서비스를 만들고, 화폐 카탈로그를 원장 정책(계정 잔액 상한)으로 문맥에 내놓는다(우편함 · GM · 거래가 쓴다).
// 설정은 `EconomyKitConfig` 이다.

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
                : _pCurrencyCatalog{}
                , _pOfferCatalog{}
                , _pFakeValidator{}
                , _receiptProviders{}
                , _receiptRegistry{}
                , _service{}
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) override
            {
                EconomyKitConfig kitConfig;
                if ( expected<void> settings = entry.readSettings( kitConfig ); settings.has_value() == false )
                    return settings;
                const expected<optional<AbsolutePath>> providersFile = entry.resolveFile( kitConfig._receiptProvidersFile, "_receiptProvidersFile" );
                if ( providersFile.has_value() == false )
                    return unexpected( providersFile.error() );
                const expected<optional<AbsolutePath>> trustFile = entry.resolveFile( kitConfig._receiptTrustFile, "_receiptTrustFile" );
                if ( trustFile.has_value() == false )
                    return unexpected( trustFile.error() );
                const string& currencyPath = kitConfig._currencyCatalog;
                const string& offerPath    = kitConfig._offerCatalog;
                _pCurrencyCatalog          = SharedAssetCache<CurrencyCatalog>::acquire( currencyPath );
                if ( _pCurrencyCatalog == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "the currency catalog could not be read", currencyPath } );
                _pOfferCatalog = SharedAssetCache<OfferCatalog>::acquire( offerPath );
                if ( _pOfferCatalog == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "the offer catalog could not be read", offerPath } );
                if ( kitConfig._bFakeReceipts )
                {
                    if ( expected<void> fake = registerFakeReceipts(); fake.has_value() == false )
                        return fake;
                }
                // 여기부터 실패하면 세운 것을 스스로 내린다 — 초기화에 실패한 키트는 stop · shutdown 을 받지 않는다.
                if ( expected<void> providers = createReceiptProviders( *providersFile, *trustFile, context._clock ); providers.has_value() == false )
                {
                    _receiptRegistry.shutdown();
                    shutdown();
                    return providers;
                }
                EconomyServiceConfig settings;
                settings._pCurrencyCatalog       = _pCurrencyCatalog.get();
                settings._pOfferCatalog          = _pOfferCatalog.get();
                settings._pReceiptRegistry       = &_receiptRegistry;
                settings._pMetricRegistry        = context._pMetricRegistry;
                settings._pScheduler             = context._pScheduler;                               // 원장 보존 검사를 매일 서버 하나가 돌린다
                settings._pPresence              = context._kits.find<IAccountPresence>();            // 계정 키트(다른 서버에 붙은 계정에 잔액 변경 알림을 중계). 선택 의존이라 먼저 섰다
                settings._pExternalLinks         = context._kits.find<const IAccountExternalLinks>(); // 계정 키트(영수증 구매자를 외부 로그인으로 묶는 제공자가 쓴다 — 그런 제공자가 있는데 없으면 서비스가 거절한다)
                settings._bAcceptSandboxReceipts = kitConfig._bAcceptSandboxReceipts ? SW_TRUE : SW_FALSE;
                if ( _service.initialize( context._pStore, settings ) == false )
                {
                    _receiptRegistry.shutdown();
                    shutdown();
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the economy service refused its settings (see the error above)" } );
                }
                // 원장 정책은 우편함 · GM · 거래가, 잔액 변경 알림은 GM 키트가 지급 · 회수 뒤에 쓴다.
                expected<void> exposed = registerServerService( context, entry, &_service )
                                             .and_then( [&]
                { return exposeInterface<const ILedgerPolicy>( context, entry, _pCurrencyCatalog->getLedgerPolicy() ); } )
                                             .and_then( [&]
                { return exposeInterface<IBalanceChangedListener>( context, entry, _service.getBalanceNotice() ); } );
                if ( exposed.has_value() == false )
                {
                    stop();
                    shutdown();
                }
                return exposed;
            }

            expected<void> start( OnlineServerKitContext& context ) override
            {
                (void)context;
                return {};
            }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 끝나지 않은 지급 · 구매 · 환불 호출(돈이 오가는 일이라 끊지 않고 마무리한다). */
            int32 getOpenJobCount() const override { return _service.getOpenJobCount(); }

            void stop() override
            {
                // 맡긴 저장 일과 영수증 검증을 회수한다 — 저장소를 종료하기 전.
                _service.shutdown(); // `initialize` 에서 섰다
                _receiptRegistry.shutdown();
            }

            void shutdown() override
            {
                // 저장소를 종료하고 남은 완료를 돌린 뒤다 — 레지스트리가 빌려 쓰던 제공자를 내린다.
                _receiptProviders.shutdown();
            }

            span<const utf8* const> getOptionalKits() const override
            {
                static constexpr const utf8* kArrKit[] = { OnlineServerKitName::kAccount }; // 접속 상태 · 외부 로그인 연동(`initialize` 에서 받는다)
                return kArrKit;
            }

        private:
            /** @brief 설정 파일을 읽어 실제 영수증 제공자를 만들고 레지스트리에 등록합니다. 파일을 정하지 않았으면 실제 제공자가 없습니다. */
            [[nodiscard]] expected<void> createReceiptProviders( const optional<AbsolutePath>& providersFile, const optional<AbsolutePath>& trustFile, const IClock& clock )
            {
                vector<ReceiptProviderConfig> listSettings;
                if ( expected<void> read = OnlineProviderFile::read<ReceiptProviderConfig>( providersFile, trustFile, "_receiptProvidersFile", "_receiptTrustFile",
                                                                                            &ReceiptProviderSet::readEntry, listSettings );
                     read.has_value() == false )
                    return read;
                if ( expected<void> providers = _receiptProviders.initialize( listSettings, OnlineProviderFile::getTrustText( trustFile ), clock ); providers.has_value() == false )
                    return providers;
                for ( int32 index = 0; index < _receiptProviders.getProviderCount(); ++index )
                {
                    if ( _receiptRegistry.registerValidator( _receiptProviders.getProvider( index ) ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "receipt provider store names must be unique" } );
                }
                return {};
            }

            /** @brief 설정 `_bFakeReceipts` 의 가짜 스토어를 레지스트리에 올립니다(DevOnline 단위가 만든다 — 그 단위가 없는 구성은 거절한다). */
            [[nodiscard]] expected<void> registerFakeReceipts()
            {
                const expected<const EconomyDevOptions*> devOptions = DevOnlineExtension<EconomyDevOptions>::require();
                if ( devOptions.has_value() == false )
                    return unexpected( devOptions.error() );
                _pFakeValidator = ( *devOptions )->_pCreateFakeReceiptValidator();
                if ( _receiptRegistry.registerValidator( _pFakeValidator.get() ) )
                    return {};
                return unexpected( Error{ ErrorKind::InvalidArgument, "the fake receipt validator could not be registered" } );
            }

        private:
            shared_ptr<const CurrencyCatalog> _pCurrencyCatalog; ///< 공유 캐시의 한 벌
            shared_ptr<const OfferCatalog>    _pOfferCatalog;    ///< 공유 캐시의 한 벌
            unique_ptr<IReceiptValidator>     _pFakeValidator;   ///< `_bFakeReceipts` 의 가짜 스토어(DevOnline 단위가 만든다). 레지스트리보다 오래 산다(먼저 선언)
            ReceiptProviderSet                _receiptProviders; ///< 레지스트리보다 오래 산다(먼저 선언)
            ReceiptValidatorRegistry          _receiptRegistry;
            EconomyService                    _service;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( EconomyOnlineServerKit, OnlineServerKitName::kEconomy );
} // namespace sw
