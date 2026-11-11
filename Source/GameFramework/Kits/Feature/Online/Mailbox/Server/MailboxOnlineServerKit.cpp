#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxService.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// 우편함 키트의 서버 조립 — 우편함 서비스를 만든다. 수령은 원장 이동이라 계정 잔액 상한을 경제 키트의 원장 정책(화폐 카탈로그)으로 받는다. 설정은 `MailboxKitConfig` 이다.

namespace sw
{
    namespace
    {
        /**
         * @class MailboxOnlineServerKit
         * @brief 우편함 키트의 서버 조립입니다.
         */
        class MailboxOnlineServerKit final : public IOnlineServerKit
        {
        public:
            MailboxOnlineServerKit()
                : _service{}
                , _settings{}
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) override
            {
                MailboxKitConfig kitConfig;
                if ( expected<void> result = entry.readSettings( kitConfig ); result.has_value() == false )
                    return result;
                _settings._sweepIntervalMs = kitConfig._sweepIntervalMs;
                if ( expected<void> result = registerServerService( context, entry, &_service ); result.has_value() == false )
                    return result;
                return {};
            }

            expected<void> start( OnlineServerKitContext& context ) override
            {
                _settings._pPolicy         = context._kits.find<const ILedgerPolicy>(); // 경제 키트가 없으면 원장 상한만 적용된다
                _settings._pMetricRegistry = context._pMetricRegistry;
                _settings._pPresence       = context._kits.find<IAccountPresence>();        // 계정 키트(다른 서버에 붙은 계정)
                _settings._pBalanceNotice  = context._kits.find<IBalanceChangedListener>(); // 경제 키트(수령 · 만료 반환의 잔액 변경 알림)
                _settings._pOfflinePush    = context._kits.find<IOfflinePushSender>();      // 라이브 운영 키트(접속하지 않은 계정의 기기 푸시)
                _settings._pScheduler      = context._pScheduler;                           // 만료 정리를 예약 작업(`mail.sweep`)으로 서버 하나만 돌린다
                if ( _service.initialize( context._pStore, _settings ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the mailbox service needs a store and a sweep interval of at least a minute" } );
                return {};
            }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 끝나지 않은 우편 호출. */
            int32 getOpenJobCount() const override { return _service.getOpenJobCount(); }

            void stop() override
            {
                // 맡긴 요청 · 만료 정리 · 캠페인 읽기를 회수한다 — 저장소를 종료하기 전.
                if ( isStarted() )
                    _service.shutdown();
            }

            span<const utf8* const> getOptionalKits() const override
            {
                static constexpr const utf8* kArrKit[] = { OnlineServerKitName::kAccount, OnlineServerKitName::kEconomy, OnlineServerKitName::kLiveOps };
                return kArrKit;
            }

        private:
            MailboxService       _service;
            MailboxServiceConfig _settings;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( MailboxOnlineServerKit, OnlineServerKitName::kMailbox );
} // namespace sw
