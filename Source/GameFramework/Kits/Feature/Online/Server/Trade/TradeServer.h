/**
 * @file TradeServer.h
 * @brief 거래 서비스의 스트림 바인딩 — `IOnlineService`(영역 `kTrade`). 요청을 `TradeService` 에 맡기고 완료를 응답으로, 바뀐 거래를 두 당사자에게 알림으로 보냅니다.
 * @details - 상대는 표시 이름으로 찾는다 — 이 프로세스에 붙은 계정은 `IAccountDirectory`(계정 키트), 없으면 접속 상태 창구(`IAccountPresence` — 서버 여럿, 비동기)에 묻는다.
 *          - 상대가 이 프로세스에 없으면 알림을 접속 상태 창구에 맡긴다(버스). 상태가 저장소에 있으니 상대 서버의 바인딩이 같은 레코드를 바꾼다(주인 서버를 거치지 않는다).
 *          - 원격 설정 `feature.trade_enabled`(기본 켬)가 꺼져 있으면 모든 요청이 `kFeatureDisabled`. 계정이 떠나면 그 계정의 열린 거래를 닫는다(PartyLeft).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Server/Trade/TradeService.h"
#include "GameFramework/Kits/Feature/Online/Trade/TradeProtocol.h"

namespace sw
{
    /**
     * @class TradeServer
     * @brief 거래 바인딩입니다(호스트 `tick` 스레드).
     */
    class SW_GF_API TradeServer final : public IOnlineService
    {
    public:
        TradeServer();

        /** @brief 넘긴 것은 빌려 쓴다. @p pPresence 는 서버 여럿일 때만(없으면 nullptr — 서버 한 대). */
        void initialize( TradeService* pTradeService, const IAccountDirectory* pDirectory, IAccountPresence* pPresence );
        void shutdown();

        uint16 getMethodRange() const override { return OnlineMethodRange::kTrade; }
        uint32 getProtocolVersion() const override { return TradeProtocol::kVersion; }
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void   onAccountLeft( OnlineServiceHost& host, AccountId accountId ) override;

        static constexpr const utf8* kFeatureFlag = "feature.trade_enabled";

    private:
        struct PendingCall
        {
            NetRequestToken _token{};
            AccountId       _accountId{ kInvalidAccountId };
        };

        struct PendingLookup
        {
            NetRequestToken _token{};
            AccountId       _accountId{ kInvalidAccountId };
            int64           _nowMs{ 0 };
        };

        void        pushSnapshot( OnlineServiceHost& host, const TradeSnapshot& snapshot );
        void        respondImmediately( OnlineServiceHost& host, const NetRequestToken& token, TradeResult result );
        void        onPresenceFound( const AccountPresenceResult& found );
        static void collectBalances( const LedgerTransferOutcome& ledger, AccountId accountId, vector<TradeBalance>& outListBalance );

        unordered_map<uint64, PendingCall>           _mapTagToCall;
        unordered_map<uint64, PendingLookup>         _mapLookupToCall;  ///< 접속 상태 창구의 이름 찾기 → 신청
        unordered_map<uint64, LedgerTransferOutcome> _mapTradeToLedger; ///< 이번 틱에 정산된 거래의 이동 뒤 잔액(알림에 싣는다)
        vector<TradeCompletion>                      _listCompletionScratch;
        vector<TradeSnapshot>                        _listUpdateScratch;
        vector<AccountPresenceResult>                _listFound; ///< 접속 상태 창구가 알린 찾기 결과 — 다음 서비스 틱에 신청으로
        vector<AccountPresenceResult>                _listFoundScratch;
        TradeService*                                _pTradeService;
        const IAccountDirectory*                     _pDirectory;
        IAccountPresence*                            _pPresence;
        uint64                                       _nextTag;
        int64                                        _nowMs;
    };
} // namespace sw
