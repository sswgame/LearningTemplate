#include "pch.h"

#include "Core/File/FileUtil.h"

#include "GameFramework/Base/Online/Identity/OfflinePush.h"
#include "GameFramework/Base/Online/Security/NetSecurity.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsServer.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fake/FakePushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushProviderSet.h"

// 라이브 운영 키트의 서버 조립 — 기간 이벤트 · 원격 설정 값 · 기기 등록 · 푸시 발송 서비스와 바인딩을 만든다.
// 실제 푸시 제공자는 설정이 있을 때만 만든다 — 자격(서비스 계정 키)은 환경 변수에서 읽고, 없으면 서버 기동이 실패한다(결정 기록 5-9, OnlineKits 4절). 지금은 FCM 이다.
// 옵션:
//   fakePush           1 이면 가짜 푸시 제공자("fake")를 등록한다(Dev 만).
//   pushProvidersFile  실제 푸시 제공자 설정 JSON(프로젝트 루트 기준, 형식은 `PushProviderSet::readSettings`). 비우면 실제 제공자가 없다.
//   pushTrustFile      제공자 `https://` 서버를 검증할 CA 번들 PEM. `pushProvidersFile` 이 https 주소를 쓰면 필수다.

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
                , _fakeProvider{}
                , _providerSet{}
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                bool   bFakePush = false;
                string providersFile;
                string trustFile;
                if ( entry.readOptions( {
                                            {         "fakePush",     bFakePush},
                                            {"pushProvidersFile", providersFile},
                                            {    "pushTrustFile",     trustFile}
                },
                                        outError ) == false )
                    return false;
                _dispatcher.initialize( context._pStore, PushDispatcherConfig{} );
                // 오프라인 푸시는 우편함 키트가 접속하지 않은 계정의 새 우편에, 이벤트 운영은 GM 키트가 쓴다. 부르는 것은 모든 키트가 시작한 뒤다.
                const bool bAssembled = createPushProviders( providersFile, trustFile, outError ) && registerFakePush( bFakePush, outError ) && registerHostService( context, entry, &_server, outError ) && exposeInterface<IOfflinePushSender>( context, entry, _offlinePush, outError ) && exposeInterface<ILiveEventControl>( context, entry, _service, outError );
                if ( bAssembled == false )
                    shutdown(); // 초기화에 실패한 키트는 shutdown 을 받지 않는다 — 만든 제공자를 여기서 내린다
                return bAssembled;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                (void)outError;
                LiveOpsDependencies dependencies;
                dependencies._pStore        = context._pStore;
                dependencies._pBus          = context._pBus;
                dependencies._pRemoteConfig = context._pRemoteConfig;
                _service.initialize( dependencies );
                _server.initialize( &_service, &_dispatcher );
                return true;
            }

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
            /** @brief 옵션 `fakePush` 이면 가짜 제공자를 발송기에 올립니다(Dev 만 — Shipping 은 거절한다). */
            [[nodiscard]] bool registerFakePush( bool bFakePush, string& outError )
            {
                if ( bFakePush == false )
                    return true;
#if defined( SW_SHIPPING )
                outError = "fakePush is a development option and is refused in Shipping";
                return false;
#else
                if ( _dispatcher.registerProvider( &_fakeProvider ) )
                    return true;
                outError = "the fake push provider could not be registered";
                return false;
#endif
            }

            /** @brief 설정 파일을 읽어 실제 푸시 제공자를 만들고 발송기에 등록합니다. 파일을 정하지 않았으면 실제 제공자가 없습니다. */
            [[nodiscard]] bool createPushProviders( const string& providersFile, const string& trustFile, string& outError )
            {
                if ( providersFile.empty() )
                {
                    if ( trustFile.empty() == false )
                    {
                        outError = "pushTrustFile is set but pushProvidersFile is not";
                        return false;
                    }
                    return true;
                }
                const string providersPath = NetSecurity::makeProjectPath( providersFile );
                string       jsonText;
                if ( FileUtil::readTextFile( providersPath, jsonText ) == false )
                {
                    outError = "cannot read the push providers file " + providersPath;
                    return false;
                }
                vector<PushProviderConfig> listSettings;
                if ( PushProviderSet::readSettings( jsonText, listSettings, outError ) == false || _providerSet.initialize( listSettings, trustFile, outError ) == false )
                    return false;
                for ( int32 index = 0; index < _providerSet.getProviderCount(); ++index )
                {
                    if ( _dispatcher.registerProvider( _providerSet.getProvider( index ) ) == false )
                    {
                        outError = "push provider ids must be unique";
                        return false;
                    }
                }
                return true;
            }

            LiveOpsService             _service;
            LiveOpsServer              _server;
            PushNotificationDispatcher _dispatcher;
            LiveOpsOfflinePush         _offlinePush; ///< 발송기를 빌려 쓰므로 뒤에 선언한다
            FakePushProvider           _fakeProvider;
            PushProviderSet            _providerSet;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( LiveOpsOnlineServerKit, OnlineServerKitName::kLiveOps );
} // namespace sw
