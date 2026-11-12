#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/Constants.h"
#include "Core/String/StringUtil.h"

#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeKitConfig.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeServer.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeService.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStateMachine.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeStoreLogic.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerConfig.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

// 거래 키트의 서버 조립 — 거래 서비스(양쪽 확정 → 원장 분개 하나로 정산)와 바인딩을 만든다. 상대 찾기는 계정 키트(필수), 다른 서버의 상대는 접속 상태,
// 계정 잔액 상한은 경제 키트의 원장 정책을 받는다.
// 설정은 `TradeKitConfig` 이다.

namespace sw
{
    namespace
    {
        /** @brief 설정의 접두어로 거래 가능 여부를 답하는 거래 정책입니다. 게임이 더 세밀한 규칙을 원하면 자기 정책으로 조립합니다. */
        class PrefixTradePolicy final : public ITradePolicy
        {
        public:
            vector<string> _listTradablePrefix;
            int32          _maxLegsPerSide;
            PrefixTradePolicy()
                : _listTradablePrefix{}
                , _maxLegsPerSide{ 0 }
            {
            }

            bool isTradable( string_view assetID ) const override
            {
                return VectorUtil::containsIf( _listTradablePrefix, [&]( const string& prefix )
                { return StringUtil::startsWith( assetID, prefix ); } );
            }
            int32 getMaxLegsPerSide() const override { return _maxLegsPerSide; }
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
            {
            }

            [[nodiscard]] expected<void> initialize( OnlineServerKitContext& context, const OnlineServerKitEntry& entry ) override
            {
                TradeKitConfig kitConfig;
                if ( expected<void> result = entry.readSettings( kitConfig ); result.has_value() == false )
                    return result;
                if ( kitConfig._listTradablePrefix.empty() )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "the trade kit needs at least one _listTradablePrefix entry (an asset id prefix)" } );
                _policy._listTradablePrefix = std::move( kitConfig._listTradablePrefix );
                _policy._maxLegsPerSide     = kitConfig._maxLegsPerSide;
                if ( expected<void> result = registerServerService( context, entry, &_server ); result.has_value() == false )
                    return result;
                return addTTLRule( context, entry,
                                   TradeStoreLogic::makeTTLRule( kitConfig._closedRetentionDays * constant::time::kSecondsPerDay * constant::time::kMillisecondsPerSecond ) );
            }

            expected<void> start( OnlineServerKitContext& context ) override
            {
                _service.initialize( context._pStore, &_policy, context._kits.find<const ILedgerPolicy>(), context._serverID, TradeServiceConfig{} );
                // 상대 찾기는 필수 키트라 정렬이 없으면 기동 오류로 막았다.
                _server.initialize( &_service, &context._kits.get<IAccountDirectory>(), context._kits.find<IAccountPresence>(), context._pMetricRegistry );
                _server.setBalanceNotice( context._kits.find<IBalanceChangedListener>() ); // 경제 키트(정산 잔액 변경 알림)
                return {};
            }

            /** @brief 종료 드레인이 기다릴 진행 중인 일 — 정산을 맡긴 요청과 열려 있는 거래(재시작 복구가 마무리하지만 서버가 비는 동안 끝나는 편이 낫다). */
            int32 getOpenJobCount() const override { return _service.getPendingCount() + _service.getOpenTradeCount(); }

            void beginShutdown( const ServerShutdownNotice& notice ) override
            {
                // 새 일을 막는 길은 없고(클라이언트는 `kShutdownNotice` 로 먼저 사유를 받는다) 드레인이 끝나기를 기다리는 일의 수만 남긴다 — 운영자가 왜 오래 걸리는지 로그로 본다.
                SW_LOG_INFO_ID( "TradeOnlineServerKit.TradeKitShutdownDrain", "Trade kit: shutdown drain starts with %# open job(s), grace %# s", getOpenJobCount(), notice._graceSeconds );
            }

            void stop() override
            {
                if ( isStarted() )
                    _server.shutdown();
            }

            void shutdown() override
            {
                // 거래 로직은 맡긴 정산 일을 회수한 뒤(저장소를 종료한 뒤) 종료한다 — 정산 중 거래는 재시작 복구가 마무리한다.
                if ( isStarted() )
                    _service.shutdown();
            }

            span<const utf8* const> getRequiredKits() const override
            {
                static constexpr const utf8* kArrKit[] = { OnlineServerKitName::kAccount }; // 상대 찾기
                return kArrKit;
            }
            span<const utf8* const> getOptionalKits() const override
            {
                static constexpr const utf8* kArrKit[] = { OnlineServerKitName::kEconomy }; // 원장 정책
                return kArrKit;
            }

        private:
            PrefixTradePolicy _policy;
            TradeService      _service;
            TradeServer       _server;
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_REGISTER_ONLINE_SERVER_KIT( TradeOnlineServerKit, OnlineServerKitName::kTrade );
} // namespace sw
