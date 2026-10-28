#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminService.h"

// GM 키트의 서버 조립 — GM 서비스를 만든다. 표시 이름 · 접속 여부와 세션 끊기는 계정 키트, 지급 상한은 경제 키트의 원장 정책, 제재 알림은 버스를 쓴다.
// GM 포트는 플레이어 포트와 따로 사설망에 둔다 — 서버 설정 `_adminServicePort` · `_adminListenAddress` 를 주면 GM 서비스는 그 리슨의 연결만 받는다(`AdminService::getExposure`).
// 0 이면 서비스 포트 하나가 GM 서비스도 받는다. 그때 격리하려면 GM 키트만 적은 온라인 설정으로 서버를 하나 더 띄운다.
// 옵션:
//   seedAdmin  `<계정 id>:<등급>`(viewer|support|operator|super). 레코드가 없을 때만 넣는 첫 관리자(여러 번 적는다).

namespace sw
{
    namespace
    {
        struct AdminOnlineServerKitInternal
        {
            /** @brief 첫 관리자 하나입니다. */
            struct SeedAdmin
            {
                AccountID _accountID{ kInvalidAccountID };
                AdminRole _role{ AdminRole::None };
            };

            [[nodiscard]] static bool parseRole( string_view text, AdminRole& outRole )
            {
                if ( text == "viewer" )
                    outRole = AdminRole::Viewer;
                else if ( text == "support" )
                    outRole = AdminRole::Support;
                else if ( text == "operator" )
                    outRole = AdminRole::Operator;
                else if ( text == "super" )
                    outRole = AdminRole::Super;
                else
                    return false;
                return true;
            }

            [[nodiscard]] static bool parseSeed( string_view text, SeedAdmin& outSeed )
            {
                const size_t colon = text.find( ':' );
                if ( colon == string_view::npos )
                    return false;
                int64 accountID = 0;
                if ( StringUtil::parseInt64( text.substr( 0, colon ), accountID ) == false || accountID <= 0 )
                    return false;
                outSeed._accountID = static_cast<AccountID>( accountID );
                return parseRole( text.substr( colon + 1 ), outSeed._role );
            }
        };

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

            [[nodiscard]] bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                vector<string> listSeedText;
                if ( entry.readOptions( {
                                            { "seedAdmin", listSeedText }
                },
                                        outError ) == false )
                    return false;
                for ( const string& seedText : listSeedText )
                {
                    AdminOnlineServerKitInternal::SeedAdmin seed;
                    if ( AdminOnlineServerKitInternal::parseSeed( seedText, seed ) == false )
                    {
                        outError = "seedAdmin '" + seedText + "' must be <account id>:<viewer|support|operator|super>";
                        return false;
                    }
                    _listSeed.push_back( seed );
                }
                if ( registerServerService( context, entry, &_service, outError ) == false )
                    return false;
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                AdminServiceConfig settings;
                SubsystemScope&    kits     = context._kits;
                settings._pDirectory        = kits.find<IAccountDirectory>();
                settings._pSessionControl   = kits.find<IAccountSessionControl>();
                settings._pBus              = context._pBus;
                settings._pPolicy           = kits.find<const ILedgerPolicy>();
                settings._pBalanceNotice    = kits.find<IBalanceNotice>(); // 경제 키트(지급 · 회수 뒤 잔액 변경 알림)
                settings._pMetricRegistry   = context._pMetricRegistry;
                settings._pDirectoryControl = kits.find<IServerDirectoryControl>();
                settings._pNameIndex        = kits.find<const IAccountNameIndex>(); // 접속하지 않은 계정도 이름으로 찾는다
                settings._pLiveEventControl = kits.find<ILiveEventControl>();
                if ( _service.initialize( context._pStore, settings ) == false )
                {
                    outError = "the admin service needs a store";
                    return false;
                }
                for ( const AdminOnlineServerKitInternal::SeedAdmin& seed : _listSeed )
                {
                    _service.seedRole( seed._accountID, seed._role, context._nowMs );
                }
                return true;
            }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 끝나지 않은 GM 호출. */
            int32 getOpenWorkCount() const override { return _service.getPendingCallCount(); }

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
            AdminService                                    _service;
            vector<AdminOnlineServerKitInternal::SeedAdmin> _listSeed;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( AdminOnlineServerKit, OnlineServerKitName::kAdmin );
} // namespace sw
