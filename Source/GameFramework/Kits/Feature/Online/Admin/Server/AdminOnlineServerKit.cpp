#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminService.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// GM 키트의 서버 조립 — GM 서비스를 만든다. 표시 이름 · 접속 여부와 세션 끊기는 계정 키트, 지급 상한은 경제 키트의 원장 정책, 제재 알림은 버스를 쓴다.
// GM 포트는 플레이어 포트와 따로 사설망에 둔다 — 서버 설정 `_adminServicePort` · `_adminListenAddress` 를 주면 GM 서비스는 그 리슨의 연결만 받는다(`AdminService::getExposure`).
// 0 이면 서비스 포트 하나가 GM 서비스도 받는다. 그때 격리하려면 GM 키트만 적은 온라인 설정으로 서버를 하나 더 띄운다.
// 설정은 `AdminKitConfig` 이다.

namespace sw
{
    namespace
    {
        /**
         * @class AdminOnlineServerKit
         * @brief GM 키트의 서버 조립입니다.
         */
        class AdminOnlineServerKit final : public IOnlineServerKit
        {
        public:
            AdminOnlineServerKit()
                : _service{}
                , _listSeed{}
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) override
            {
                AdminKitConfig kitConfig;
                if ( expected<void> result = entry.readSettings( kitConfig ); result.has_value() == false )
                    return result;
                for ( const AdminSeedEntry& seed : kitConfig._listSeedAdmin )
                {
                    if ( seed._role == AdminRole::None || seed._role == AdminRole::Count )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "_listSeedAdmin needs a role of Viewer, Support, Operator or Super" } );
                }
                _listSeed = std::move( kitConfig._listSeedAdmin );
                if ( expected<void> result = registerServerService( context, entry, &_service ); result.has_value() == false )
                    return result;
                return {};
            }

            expected<void> start( OnlineServerKitContext& context ) override
            {
                AdminServiceConfig settings;
                SubsystemScope&    kits     = context._kits;
                settings._pDirectory        = kits.find<IAccountDirectory>();
                settings._pSessionControl   = kits.find<IAccountSessionControl>();
                settings._pBus              = context._pBus;
                settings._pPolicy           = kits.find<const ILedgerPolicy>();
                settings._pBalanceNotice    = kits.find<IBalanceChangedListener>(); // 경제 키트(지급 · 회수 뒤 잔액 변경 알림)
                settings._pMetricRegistry   = context._pMetricRegistry;
                settings._pDirectoryControl = kits.find<IServerDirectoryControl>();
                settings._pNameIndex        = kits.find<const IAccountNameIndex>(); // 접속하지 않은 계정도 이름으로 찾는다
                settings._pLiveEventControl = kits.find<ILiveEventControl>();
                if ( _service.initialize( context._pStore, settings ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the admin service needs a store" } );
                for ( const AdminSeedEntry& seed : _listSeed )
                {
                    _service.seedRole( static_cast<AccountID>( seed._accountID ), seed._role, context._nowMs );
                }
                return {};
            }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 끝나지 않은 GM 호출. */
            int32 getOpenJobCount() const override { return _service.getOpenJobCount(); }

            void stop() override
            {
                // 맡긴 GM 명령과 첫 관리자 넣기를 회수한다 — 저장소를 종료하기 전.
                if ( isStarted() )
                    _service.shutdown();
            }

            span<const utf8* const> getOptionalKits() const override
            {
                static constexpr const utf8* kArrKit[] = { OnlineServerKitName::kAccount, OnlineServerKitName::kDirectory, OnlineServerKitName::kEconomy,
                                                           OnlineServerKitName::kLiveOps };
                return kArrKit;
            }

        private:
            AdminService           _service;
            vector<AdminSeedEntry> _listSeed;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( AdminOnlineServerKit, OnlineServerKitName::kAdmin );
} // namespace sw
