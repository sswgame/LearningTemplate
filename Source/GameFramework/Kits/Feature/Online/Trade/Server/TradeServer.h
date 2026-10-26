/**
 * @file TradeServer.h
 * @brief 거래 서비스의 스트림 바인딩입니다. `IOnlineHostService`(영역 `kTrade`)를 구현합니다.
 * @details 요청을 `TradeService` 에 맡기고 완료는 응답으로, 바뀐 거래는 두 당사자에게 알림으로 보낸다.
 *          - 상대는 표시 이름으로 찾는다. 이 프로세스에 접속한 계정은 `IAccountDirectory`(계정 키트)에서 찾고, 없으면 접속 상태 관리자(`IAccountPresence`)에 묻는다. 서버가 여러 대일 때 쓰며 비동기이다.
 *          - 상대가 이 프로세스에 없으면 알림을 접속 상태 관리자에 맡긴다(버스). 상태가 저장소에 있으므로 상대 서버의 바인딩이 같은 레코드를 바꾼다. 주인 서버를 거치지 않는다.
 *          - 원격 설정 `feature.trade_enabled`(기본값은 켬)가 꺼져 있으면 모든 요청이 `kFeatureDisabled` 로 끝난다. 계정이 떠나면 그 계정의 열린 거래를 닫는다(PartyLeft).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Trade/Server/TradeService.h"
#include "GameFramework/Kits/Feature/Online/Trade/Shared/TradeProtocol.h"

namespace sw
{
    /**
     * @class TradeServer
     * @brief 거래 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API TradeServer final : public IOnlineHostService
    {
    public:
        TradeServer();

        /** @brief 넘긴 객체는 빌려 씁니다. @p pPresence 는 서버가 여러 대일 때만 넘기며, 없으면 nullptr 이고 서버가 한 대입니다. */
        void initialize( TradeService* pTradeService, const IAccountDirectory* pDirectory, IAccountPresence* pPresence );
        void shutdown();

        const OnlineProtocolEntry& getProtocol() const override { return TradeProtocol::kOnlineEntry; }
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void                       onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void                       onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;

        static constexpr const utf8* kFeatureFlag = "feature.trade_enabled";

    private:
        struct PendingCall
        {
            NetRequestToken _token{};
            AccountID       _accountID{ kInvalidAccountID };
        };

        struct PendingLookup
        {
            NetRequestToken _token{};
            AccountID       _accountID{ kInvalidAccountID };
            int64           _nowMs{ 0 };
        };

        void pushSnapshot( OnlineServiceHost& host, const TradeSnapshot& snapshot );
        void respondImmediately( OnlineServiceHost& host, const NetRequestToken& token, TradeResult result );
        /** @brief 응답을 씁니다. 범위 밖 값이 있어 쓰지 못하면(로그가 남는다) `kInternal` 로 답합니다. */
        void        respond( OnlineServiceHost& host, const NetRequestToken& token, const TradeReply& reply );
        void        onPresenceFound( const AccountPresenceResult& found );
        static void collectBalances( const LedgerTransferOutcome& ledger, AccountID accountID, vector<TradeBalance>& outListBalance );

        unordered_map<uint64, PendingCall>           _mapTagToCall;
        unordered_map<uint64, PendingLookup>         _mapLookupToCall;  ///< 접속 상태 관리자의 이름 찾기에서 신청으로 가는 맵이다
        unordered_map<uint64, LedgerTransferOutcome> _mapTradeToLedger; ///< 이번 틱에 정산된 거래의 이동 뒤 잔액이다. 알림에 싣는다
        vector<TradeCompletion>                      _listCompletionScratch;
        vector<TradeSnapshot>                        _listUpdateScratch;
        vector<AccountPresenceResult>                _listFound; ///< 접속 상태 관리자가 알린 찾기 결과이다. 다음 서비스 틱에 신청으로 이어 간다
        vector<AccountPresenceResult>                _listFoundScratch;
        TradeService*                                _pTradeService;
        const IAccountDirectory*                     _pDirectory;
        IAccountPresence*                            _pPresence;
        Subscription                                 _findOwner; ///< 접속 상태 찾기의 주인 몫(`initialize` 가 열고 `shutdown` 이 놓는다)
        uint64                                       _nextTag;
        int64                                        _nowMs;
    };
} // namespace sw
