#include "pch.h"

#include "Core/Network/DevOnlineExtension.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsDevOptions.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsKitConfig.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsServer.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fake/FakePushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushProviderSet.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Provider/OnlineProviderFile.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// 라이브 운영 키트의 서버 조립 — 기간 이벤트 · 원격 설정 값 · 기기 등록 · 푸시 발송 서비스와 바인딩을 만든다.
// 실제 푸시 제공자는 설정이 있을 때만 만든다 — 자격(서비스 계정 키)은 환경 변수에서 읽고, 없으면 서버 기동이 실패한다. 지금은 FCM 이다.
// 설정은 `LiveOpsKitConfig` 이다.

namespace sw
{
    namespace
    {
        /** @brief 푸시 발송기를 기반의 오프라인 푸시 창으로 내놓습니다. 우편함 키트가 접속하지 않은 계정의 새 우편에 부릅니다. */
        class LiveOpsOfflinePush final : public IOfflinePushSender
        {
        public:
            explicit LiveOpsOfflinePush( PushNotificationDispatcher* pDispatcher )
                : _pDispatcher{ pDispatcher }
            {
            }

            bool sendOfflinePush( AccountID accountID, string_view titleKey, string_view bodyKey, string_view deepLink, UnixMilliseconds nowMs ) override
            {
                PushNotificationMessage message;
                message._titleKey = string( titleKey );
                message._bodyKey  = string( bodyKey );
                message._deepLink = string( deepLink );
                return _pDispatcher->notifyAccount( accountID, message, nowMs );
            }

        private:
            PushNotificationDispatcher* _pDispatcher;
        };

        /**
         * @class LiveOpsOnlineServerKit
         * @brief 라이브 운영 키트의 서버 조립입니다. 발송기는 맡긴 저장 일이 끝날 때까지(저장소를 내린 뒤) 살아 있어야 하므로 키트와 수명이 같습니다.
         */
        class LiveOpsOnlineServerKit final : public IOnlineServerKit
        {
        public:
            LiveOpsOnlineServerKit()
                : _service{}
                , _server{}
                , _dispatcher{}
                , _offlinePush{ &_dispatcher }
                , _pFakeProvider{}
                , _providerSet{}
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) override
            {
                LiveOpsKitConfig kitConfig;
                if ( expected<void> settings = entry.readSettings( kitConfig ); settings.has_value() == false )
                    return settings;
                const expected<optional<AbsolutePath>> providersFile = entry.resolveFile( kitConfig._pushProvidersFile, "_pushProvidersFile" );
                if ( providersFile.has_value() == false )
                    return unexpected( providersFile.error() );
                const expected<optional<AbsolutePath>> trustFile = entry.resolveFile( kitConfig._pushTrustFile, "_pushTrustFile" );
                if ( trustFile.has_value() == false )
                    return unexpected( trustFile.error() );
                _dispatcher.initialize( context._pStore, PushDispatcherConfig{} );
                // 오프라인 푸시는 우편함 키트가 접속하지 않은 계정의 새 우편에, 이벤트 운영은 GM 키트가 쓴다. 부르는 것은 모든 키트가 시작한 뒤다.
                expected<void> assembled = createPushProviders( *providersFile, *trustFile, context._clock )
                                               .and_then( [&]
                { return registerFakePush( kitConfig._bFakePush ); } )
                                               .and_then( [&]
                { return registerServerService( context, entry, &_server ); } )
                                               .and_then( [&]
                { return exposeInterface<IOfflinePushSender>( context, entry, _offlinePush ); } )
                                               .and_then( [&]
                { return exposeInterface<ILiveEventControl>( context, entry, _service ); } )
                                               .and_then( [&]
                { return addTTLRule( context, entry, PushNotificationDispatcher::makeTopicClaimTTLRule( kitConfig._pushClaimTTL.getExpireAfterMs() ), kitConfig._pushClaimTTL ); } );
                if ( assembled.has_value() == false )
                    shutdown(); // 초기화에 실패한 키트는 shutdown 을 받지 않는다 — 만든 제공자를 여기서 내린다
                return assembled;
            }

            expected<void> start( OnlineServerKitContext& context ) override
            {
                LiveOpsDependencies dependencies;
                dependencies._pStore        = context._pStore;
                dependencies._pBus          = context._pBus;
                dependencies._pRemoteConfig = context._pRemoteConfig;
                _service.initialize( dependencies );
                _server.initialize( &_service, &_dispatcher, context._pMetricRegistry );
                return {};
            }

            /** @brief 탈퇴한 계정의 기기 푸시 토큰을 지운다(`PushNotificationDispatcher::eraseAccount`). 운영 이벤트 정의 · 토픽 알림 표에는 계정이 없다. */
            const IAfterDeleteAccount* getAfterDeleteAccount() const override { return &_afterDeleteAccount; }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 저장소에 맡긴 요청. */
            int32 getOpenJobCount() const override { return _service.getPendingCount(); }

            void stop() override
            {
                if ( isStarted() == false )
                    return;
                _server.shutdown();
                _service.shutdown();
            }

            void shutdown() override
            {
                // 저장소를 종료하고 남은 완료(배달 시작)를 돌린 뒤다 — 발송기가 빌려 쓰던 제공자를 내린다.
                _providerSet.shutdown();
            }

        private:
            /** @brief 설정 `_bFakePush` 이면 가짜 제공자를 발송기에 올립니다(DevOnline 단위가 만든다 — 그 단위가 없는 구성은 거절한다). */
            [[nodiscard]] expected<void> registerFakePush( bool bFakePush )
            {
                if ( bFakePush == false )
                    return {};
                const expected<const LiveOpsDevOptions*> devOptions = DevOnlineExtension<LiveOpsDevOptions>::require();
                if ( devOptions.has_value() == false )
                    return unexpected( devOptions.error() );
                _pFakeProvider = ( *devOptions )->_pCreateFakePushProvider();
                if ( _dispatcher.registerProvider( _pFakeProvider.get() ) )
                    return {};
                return unexpected( Error{ ErrorKind::InvalidArgument, "the fake push provider could not be registered" } );
            }

            /** @brief 설정 파일을 읽어 실제 푸시 제공자를 만들고 발송기에 등록합니다. 파일을 정하지 않았으면 실제 제공자가 없습니다. */
            [[nodiscard]] expected<void> createPushProviders( const optional<AbsolutePath>& providersFile, const optional<AbsolutePath>& trustFile, const IClock& clock )
            {
                vector<PushProviderConfig> listSettings;
                if ( expected<void> read = OnlineProviderFile::read<PushProviderConfig>( providersFile, trustFile, "_pushProvidersFile", "_pushTrustFile", &PushProviderSet::readEntry, listSettings );
                     read.has_value() == false )
                    return read;
                if ( expected<void> providers = _providerSet.initialize( listSettings, OnlineProviderFile::getTrustText( trustFile ), clock ); providers.has_value() == false )
                    return providers;
                for ( int32 index = 0; index < _providerSet.getProviderCount(); ++index )
                {
                    if ( _dispatcher.registerProvider( _providerSet.getProvider( index ) ) == false )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "push provider ids must be unique" } );
                }
                return {};
            }

        private:
            LiveOpsService                                                        _service;
            LiveOpsServer                                                         _server;
            PushNotificationDispatcher                                            _dispatcher;
            LiveOpsOfflinePush                                                    _offlinePush;   ///< 발송기를 빌려 쓰므로 뒤에 선언한다
            unique_ptr<IPushNotificationProvider>                                 _pFakeProvider; ///< `_bFakePush` 의 가짜 제공자(DevOnline 단위가 만든다). 발송기가 빌려 쓴다
            PushProviderSet                                                       _providerSet;
            AfterDeleteAccountFunction<&PushNotificationDispatcher::eraseAccount> _afterDeleteAccount;
        };

        struct LiveOpsDevOnlineInternal
        {
            static unique_ptr<IPushNotificationProvider> createFakePushProvider()
            {
                return make_unique<FakePushProvider>();
            }

            static constexpr LiveOpsDevOptions kDevOptions{ &createFakePushProvider };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_DEFINE_DEV_ONLINE_EXTENSION( LiveOpsDevOptions, "_bFakePush" )

    SW_REGISTER_ONLINE_SERVER_KIT( LiveOpsOnlineServerKit, OnlineServerKitName::kLiveOps );

    SW_REGISTER_DEV_ONLINE_EXTENSION( LiveOpsDevOptions, LiveOpsDevOnlineInternal::kDevOptions );
} // namespace sw
