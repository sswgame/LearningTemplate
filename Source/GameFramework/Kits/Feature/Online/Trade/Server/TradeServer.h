/**
 * @file TradeServer.h
 * @brief 거래 서비스의 스트림 바인딩입니다. 프로토콜 표(`OnlineProtocolService`, 영역 `kTrade`)로 섭니다.
 * @details 요청을 `TradeService` 에 맡기고 완료는 응답으로, 바뀐 거래는 두 당사자에게 알림으로 보낸다.
 *          - 상대는 표시 이름으로 찾는다. 이 프로세스에 접속한 계정은 `IAccountDirectory`(계정 키트)에서 찾고, 없으면 접속 상태 관리자(`IAccountPresence`)에 묻는다. 서버가 여러 대일 때 쓰며 비동기이다.
 *          - 상대가 이 프로세스에 없으면 알림을 접속 상태 관리자에 맡긴다(버스). 상태가 저장소에 있으므로 상대 서버의 바인딩이 같은 레코드를 바꾼다. 주인 서버를 거치지 않는다.
 *          - 원격 설정 `feature.trade_enabled`(기본값은 켬)가 꺼져 있으면 모든 요청이 `kFeatureDisabled` 로 끝난다. 계정이 떠나면 그 계정의 열린 거래를 닫는다(PartyLeft).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresenceTracker.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Ledger/IBalanceChangedListener.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/OnlineProtocolService.h"
#include "GameFramework/Kits/Feature/Online/Trade/Runtime/TradeSessionRecord.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeService.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

namespace sw
{
    class MetricRegistry;

    /**
     * @class TradeServer
     * @brief 거래 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API TradeServer final : public OnlineProtocolService<TradeServer, TradeSessionRecord>
    {
    public:
        static constexpr const utf8* kFeatureFlag = "feature.trade_enabled";
        TradeServer();

        /** @brief 넘긴 객체는 빌려 씁니다. @p pPresence 는 서버가 여러 대일 때만 넘기며, 없으면 nullptr 이고 서버가 한 대입니다. */
        void initialize( TradeService* pTradeService, const IAccountDirectory* pDirectory, IAccountPresence* pPresence, MetricRegistry* pMetricRegistry = nullptr );
        void shutdown();
        /** @brief 정산으로 바뀐 잔액을 알릴 창구(경제 키트)를 겁니다. 빌려 쓰며 없으면 알리지 않는다. */
        void setBalanceNotice( IBalanceChangedListener* pBalanceNotice ) { _pBalanceNotice = pBalanceNotice; }

        // 프로토콜 표의 메서드 - 결과는 호출 번호로 `takeCompletions` 에서 받는다.
        void handleCall( OnlineServerCall<TradeMethod::Invite> call, const OnlineCallContext& context, const TradeInviteRequest& request );
        void handleCall( OnlineServerCall<TradeMethod::Respond> call, const OnlineCallContext& context, const TradeRespondRequest& request );
        void handleCall( OnlineServerCall<TradeMethod::SetOffer> call, const OnlineCallContext& context, const TradeOfferRequest& request );
        void handleCall( OnlineServerCall<TradeMethod::Lock> call, const OnlineCallContext& context, const TradeIDRequest& request );
        void handleCall( OnlineServerCall<TradeMethod::Confirm> call, const OnlineCallContext& context, const TradeConfirmRequest& request );
        void handleCall( OnlineServerCall<TradeMethod::Cancel> call, const OnlineCallContext& context, const TradeIDRequest& request );

        // IOnlineServerService
        void onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        void onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;

    protected:
        /** @brief 원격 설정의 기능 플래그가 꺼져 있으면 본문을 읽기 전에 `kFeatureDisabled` 로 끝냅니다. */
        uint16 checkRequest( OnlineServiceHost& host, const OnlineCallContext& context ) override;

    private:
        struct PendingLookup
        {
            uint64           _callID{ 0 };
            AccountID        _accountID{ kInvalidAccountID };
            UnixMilliseconds _nowMs{ 0 };
        };

        /** @brief 이름 찾기가 답한 신청입니다. 다음 서비스 틱에 초대로 잇는다. */
        struct FoundLookup
        {
            PendingLookup _lookup{};
            AccountID     _peerID{ kInvalidAccountID }; ///< 찾은 상대(못 찾았으면 무효)
        };

        void        pushSnapshot( OnlineServiceHost& host, const TradeSnapshot& snapshot );
        void        respondImmediately( uint64 callID, TradeServiceResult result );
        static void collectBalances( const LedgerTransferResult& ledger, AccountID accountID, vector<TradeBalance>& outListBalance );
        /** @brief 정산으로 바뀐 잔액을 계정마다 알립니다(경제 키트의 `BalanceChanged`). */
        void notifySettledBalances( const LedgerTransferResult& ledger );

    private:
        unordered_map<uint64, AccountID>            _mapCallToAccount; ///< 응답을 기다리는 호출의 계정이다. 정산 잔액을 그 계정 몫만 싣는다
        unordered_map<uint64, LedgerTransferResult> _mapTradeToLedger; ///< 이번 틱에 정산된 거래의 이동 뒤 잔액이다. 알림에 싣는다
        vector<TradeCompletion>                     _listCompletionScratch;
        vector<TradeSnapshot>                       _listUpdateScratch;
        vector<FoundLookup>                         _listFound; ///< 이름 찾기가 답한 신청이다. 다음 서비스 틱에 초대로 이어 간다
        vector<FoundLookup>                         _listFoundScratch;
        TradeService*                               _pTradeService;
        const IAccountDirectory*                    _pDirectory;
        IBalanceChangedListener*                    _pBalanceNotice; ///< 정산 잔액을 두 당사자의 지갑에 알린다(`setBalanceNotice`)
        AccountPresenceTracker                      _presenceFinds;  ///< 상대 이름 찾기 · 다른 서버로의 알림(서버 여러 대일 때만 묶인다)
        UnixMilliseconds                            _nowMs;
    };
} // namespace sw
