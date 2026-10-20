#include "pch.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Server/MailboxService.h"

// 우편함 키트의 서버 조립 — 우편함 서비스를 올린다. 수령은 원장 이동이라 계정 잔액 상한을 경제 키트의 원장 정책(화폐 카탈로그)으로 받는다.
// 옵션:
//   sweepIntervalMs  만료 정리 주기(밀리초, 기본 60000, 1 분 이상). 이 주기의 예약 작업(`mail.sweep`)을 등록해 서버 하나만 정리한다.

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
                , _bStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                int32 sweepIntervalMs = static_cast<int32>( _settings._sweepIntervalMs );
                if ( entry.readOptions( {
                                            { "sweepIntervalMs", sweepIntervalMs }
                },
                                        outError ) == false )
                    return false;
                _settings._sweepIntervalMs = sweepIntervalMs;
                if ( registerHostService( context, entry, &_service, outError ) == false )
                    return false;
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                _settings._pPolicy         = context._pLedgerPolicy; // 경제 키트가 없으면 원장 상한만 적용된다
                _settings._pMetricRegistry = context._pMetricRegistry;
                _settings._pPresence       = context._pPresence;    // 계정 키트(다른 서버에 붙은 계정)
                _settings._pOfflinePush    = context._pOfflinePush; // 라이브 운영 키트(접속하지 않은 계정의 기기 푸시)
                _settings._pScheduler      = context._pScheduler;   // 만료 정리를 예약 작업(`mail.sweep`)으로 서버 하나만 돌린다
                if ( _service.initialize( context._pStore, _settings ) == false )
                {
                    outError = "the mailbox service needs a store and a sweep interval of at least a minute";
                    return false;
                }
                _bStarted = true;
                return true;
            }

            void stop() override
            {
                // 맡긴 요청 · 만료 정리 · 캠페인 읽기를 회수한다 — 저장소를 종료하기 전.
                if ( _bStarted )
                    _service.shutdown();
                _bStarted = false;
            }

        private:
            MailboxService       _service;
            MailboxServiceConfig _settings;
            bool                 _bStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( MailboxOnlineServerKit, "mailbox", OnlineServerKitOrder::kMailbox );
} // namespace sw
