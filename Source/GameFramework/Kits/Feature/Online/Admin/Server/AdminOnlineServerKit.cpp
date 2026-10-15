#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Admin/Server/AdminService.h"

// GM 키트의 서버 조립 — GM 서비스를 올린다. 표시 이름 · 접속 여부와 세션 끊기는 계정 키트, 지급 상한은 경제 키트의 원장 정책, 제재 알림은 버스를 쓴다.
// GM 포트는 플레이어 포트와 따로 사설망에 두기를 권한다 — GM 키트만 적은 온라인 설정으로 서버를 하나 더 띄운다.
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
                , _bStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
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
                if ( registerHostService( context, entry, &_service, outError ) == false )
                    return false;
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                AdminServiceSettings settings;
                settings._pDirectory      = context._pAccountDirectory;
                settings._pSessionControl = context._pSessionControl;
                settings._pBus            = context._pBus;
                settings._pPolicy         = context._pLedgerPolicy;
                settings._pMetricRegistry = context._pMetricRegistry;
                if ( _service.initialize( context._pStore, settings ) == false )
                {
                    outError = "the admin service needs a store";
                    return false;
                }
                _bStarted = true;
                for ( const AdminOnlineServerKitInternal::SeedAdmin& seed : _listSeed )
                {
                    _service.seedRole( seed._accountID, seed._role, context._nowMs );
                }
                return true;
            }

            void stop() override
            {
                // 맡긴 GM 명령과 첫 관리자 넣기를 거둔다 — 저장소를 내리기 전.
                if ( _bStarted )
                    _service.shutdown();
                _bStarted = false;
            }

        private:
            AdminService                                    _service;
            vector<AdminOnlineServerKitInternal::SeedAdmin> _listSeed;
            bool                                            _bStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( AdminOnlineServerKit, "admin", OnlineServerKitOrder::kAdmin );
} // namespace sw
