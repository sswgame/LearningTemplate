#include "pch.h"

#include "Core/Container/StringUtil.h"

#include "GameFramework/Base/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Base/Online/Server/OnlineServerKit.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeServer.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeService.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStateMachine.h"

// 거래 키트의 서버 조립 — 거래 서비스(양쪽 확정 → 원장 분개 하나로 정산)와 바인딩을 만든다. 상대 찾기는 계정 키트(필수), 다른 서버의 상대는 접속 상태,
// 계정 잔액 상한은 경제 키트의 원장 정책을 받는다.
// 옵션:
//   tradable        거래할 수 있는 자산 id 접두어(여러 번 적는다 — 예: `cur.gold`, `item.`). 하나는 있어야 한다.
//   maxLegsPerSide  한쪽이 내놓을 수 있는 자산 줄 수(기본 8).

namespace sw
{
    namespace
    {
        struct TradeOnlineServerKitInternal
        {
            static constexpr int32 kDefaultMaxLegsPerSide = 8;
        };

        /** @brief 설정의 접두어로 거래 가능 여부를 답하는 거래 정책입니다. 게임이 더 세밀한 규칙을 원하면 자기 정책으로 조립합니다. */
        class PrefixTradePolicy final : public ITradePolicy
        {
        public:
            PrefixTradePolicy()
                : _listTradablePrefix{}
                , _maxLegsPerSide{ TradeOnlineServerKitInternal::kDefaultMaxLegsPerSide }
            {
            }

            bool isTradable( string_view assetID ) const override
            {
                for ( const string& prefix : _listTradablePrefix )
                {
                    if ( StringUtil::startsWith( assetID, prefix ) )
                        return true;
                }
                return false;
            }
            int32 getMaxLegsPerSide() const override { return _maxLegsPerSide; }

            vector<string> _listTradablePrefix;
            int32          _maxLegsPerSide;
        };

        /**
         * @class TradeOnlineServerKit
         * @brief 거래 키트의 서버 조립입니다.
         */
        class TradeOnlineServerKit final : public IOnlineServerKit
        {
        public:
            TradeOnlineServerKit()
                : _policy{}
                , _service{}
                , _server{}
                , _bStarted{ false }
            {
            }

            bool initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry, string& outError ) override
            {
                if ( entry.readOptions( {
                                            {      "tradable", _policy._listTradablePrefix},
                                            {"maxLegsPerSide",     _policy._maxLegsPerSide}
                },
                                        outError ) == false )
                    return false;
                if ( _policy._listTradablePrefix.empty() )
                {
                    outError = "the trade kit needs at least one tradable option (an asset id prefix)";
                    return false;
                }
                if ( registerHostService( context, entry, &_server, outError ) == false )
                    return false;
                return true;
            }

            bool start( OnlineServerKitContext& context, string& outError ) override
            {
                if ( context._pAccountDirectory == nullptr )
                {
                    outError = "trade needs the account kit (finding the other trader)";
                    return false;
                }
                _service.initialize( context._pStore, &_policy, context._pLedgerPolicy, context._serverID, TradeServiceConfig{} );
                _server.initialize( &_service, context._pAccountDirectory, context._pPresence );
                _bStarted = true;
                return true;
            }

            void stop() override
            {
                if ( _bStarted )
                    _server.shutdown();
            }

            void shutdown() override
            {
                // 거래 로직은 맡긴 정산 일을 회수한 뒤(저장소를 종료한 뒤) 종료한다 — 정산 중 거래는 재시작 복구가 마무리한다.
                if ( _bStarted )
                    _service.shutdown();
                _bStarted = false;
            }

        private:
            PrefixTradePolicy _policy;
            TradeService      _service;
            TradeServer       _server;
            bool              _bStarted;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( TradeOnlineServerKit, "trade", OnlineServerKitOrder::kTrade );
} // namespace sw
