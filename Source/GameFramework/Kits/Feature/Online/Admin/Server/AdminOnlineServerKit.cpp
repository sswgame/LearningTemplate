#include "pch.h"

#include "Core/Foundation/Constants.h"

#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminService.h"
#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminStoreLogic.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Config/LiveEventControl.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Directory/ServerDirectoryControl.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Ledger/IBalanceChangedListener.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
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
        class AdminOnlineServerKit final : public OnlineServerKit<OnlineKitRequired<>, OnlineKitOptional<IAccountDirectory, IAccountSessionControl, const ILedgerPolicy, IBalanceChangedListener, IServerDirectoryControl, const IAccountNameIndex, ILiveEventControl>>
        {
        public:
            AdminOnlineServerKit()
                : _service{}
                , _listSeed{}
                , _sanctionLimit{}
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, const Dependencies& ) override
            {
                AdminKitConfig kitConfig;
                if ( expected<void> result = entry.readSettings( kitConfig ); result.has_value() == false )
                    return result;
                for ( const AdminSeedEntry& seed : kitConfig._listSeedAdmin )
                {
                    if ( seed._role == AdminRole::None || seed._role == AdminRole::Count )
                        return unexpected( Error{ ErrorKind::InvalidArgument, "_listSeedAdmin needs a role of Viewer, Support, Operator or Super" } );
                }
                _listSeed                     = std::move( kitConfig._listSeedAdmin );
                _sanctionLimit._supportMaxMs  = static_cast<int64>( kitConfig._supportMaxSanctionDays ) * ( constant::time::kSecondsPerDay * constant::time::kMillisecondsPerSecond );
                _sanctionLimit._operatorMaxMs = static_cast<int64>( kitConfig._operatorMaxSanctionDays ) * ( constant::time::kSecondsPerDay * constant::time::kMillisecondsPerSecond );
                if ( expected<void> result = registerServerService( context, entry, &_service ); result.has_value() == false )
                    return result;
                return {};
            }

            expected<void> start( OnlineServerKitContext& context, const Dependencies& interfaces, const UnorderedDependencies& ) override
            {
                AdminServiceConfig settings;
                settings._pDirectory           = interfaces.find<IAccountDirectory>();
                settings._pSessionControl      = interfaces.find<IAccountSessionControl>();
                settings._pBus                 = context._pBus;
                settings._pPolicy              = interfaces.find<const ILedgerPolicy>();
                settings._pBalanceNotification = interfaces.find<IBalanceChangedListener>(); // 경제 키트(지급 · 회수 뒤 잔액 변경 알림)
                settings._pMetricRegistry      = context._pMetricRegistry;
                settings._pDirectoryControl    = interfaces.find<IServerDirectoryControl>();
                settings._pNameIndex           = interfaces.find<const IAccountNameIndex>(); // 접속하지 않은 계정도 이름으로 찾는다
                settings._pLiveEventControl    = interfaces.find<ILiveEventControl>();
                settings._sanctionLimit        = _sanctionLimit;
                if ( _service.initialize( context._pStore, settings ) == false )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the admin service needs a store" } );
                for ( const AdminSeedEntry& seed : _listSeed )
                {
                    _service.seedRole( static_cast<AccountID>( seed._accountID ), seed._role, context._nowMs );
                }
                return {};
            }

            /** @brief 탈퇴한 계정의 GM 등급을 지운다(`AdminStoreLogic::eraseAccount`). */
            const IAfterDeleteAccount* getAfterDeleteAccount() const override { return &_afterDeleteAccount; }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 끝나지 않은 GM 호출. */
            int32 getOpenJobCount() const override { return _service.getOpenJobCount(); }

            void stop() override
            {
                // 맡긴 GM 명령과 첫 관리자 넣기를 회수한다 — 저장소를 종료하기 전.
                if ( isStarted() )
                    _service.shutdown();
            }

        private:
            AdminService                                               _service;
            vector<AdminSeedEntry>                                     _listSeed;
            AdminSanctionLimit                                         _sanctionLimit; ///< 키트 설정의 등급별 제재 최대 길이
            AfterDeleteAccountFunction<&AdminStoreLogic::eraseAccount> _afterDeleteAccount;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( AdminOnlineServerKit, OnlineServerKitName::kAdmin );
} // namespace sw
