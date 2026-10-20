#include "pch.h"

#include "GameFramework/Base/Online/Identity/OfflinePush.h"
#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsServer.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/LiveOpsService.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fake/FakePushProvider.h"
#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/PushNotificationDispatcher.h"

// 라이브 운영 키트의 서버 조립 — 기간 이벤트 · 원격 설정 값 · 기기 등록 · 푸시 발송 서비스와 바인딩을 올린다.
// 실제 푸시 제공자(APNs · FCM)는 외부 계정 · 비용이라 계약과 가짜까지만 둔다(결정 기록 5-9).
// 옵션:
//   fakePush  1 이면 가짜 푸시 제공자("fake")를 올린다(Dev 만).

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

            bool sendOfflinePush( AccountID accountID, string_view titleKey, string_view bodyKey, string_view deepLink, int64 nowMs ) override
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
                , _bStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                bool bFakePush = false;
                if ( entry.readOptions( {
                                            { "fakePush", bFakePush }
                },
                                        outError ) == false )
                    return false;
                _dispatcher.initialize( context._pStore, PushDispatcherConfig{} );
                if ( bFakePush )
                {
#if defined( SW_SHIPPING )
                    outError = "fakePush is a development option and is refused in Shipping";
                    return false;
#else
                    if ( _dispatcher.registerProvider( &_fakeProvider ) == false )
                    {
                        outError = "the fake push provider could not be registered";
                        return false;
                    }
#endif
                }
                if ( registerHostService( context, entry, &_server, outError ) == false )
                    return false;
                context._pOfflinePush      = &_offlinePush; // 우편함 키트가 접속하지 않은 계정의 새 우편에 쓴다
                context._pLiveEventControl = &_service;     // GM 키트의 이벤트 운영이 쓴다. 부르는 것은 모든 키트가 시작한 뒤다
                return true;
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
                _bStarted = true;
                return true;
            }

            void stop() override
            {
                if ( _bStarted == false )
                    return;
                _server.shutdown();
                _service.shutdown();
                _bStarted = false;
            }

        private:
            LiveOpsService             _service;
            LiveOpsServer              _server;
            PushNotificationDispatcher _dispatcher;
            LiveOpsOfflinePush         _offlinePush; ///< 발송기를 빌려 쓰므로 뒤에 선언한다
            FakePushProvider           _fakeProvider;
            bool                       _bStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( LiveOpsOnlineServerKit, "liveops", OnlineServerKitOrder::kLiveOps );
} // namespace sw
