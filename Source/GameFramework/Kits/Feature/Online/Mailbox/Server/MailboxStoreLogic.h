/**
 * @file MailboxStoreLogic.h
 * @brief 우편함의 저장 부분입니다. 목록, 읽음, 수령, 모두 받기, 지우기, 캠페인 수령, 만료 정리를 처리하는 저장소 스레드의 동기 함수입니다.
 * @details - 수령은 (재원에서 계정으로) 원장 이동, 우편 상태를 "받음" 으로 바꾸기, 만료를 "받은 뒤 보관 기간" 으로 다시 걸기(우편의 만료 시각과 만료 인덱스)를 한 트랜잭션으로 처리한다. 분개 키 `mail.claim/<우편 토큰>` 이 두 번째 수령을 막으므로 두 기기에서 동시에 눌러도 원장은 한 번만 움직인다.
 *            상한을 넘으면 우편이 그대로 남는다(`CapExceeded`).
 *          - 지우기는 받았거나 첨부가 없는 우편만 지울 수 있다. 첨부가 남은 우편을 지우면 재화가 사라지기 때문이다.
 *          - 받은 우편도 받은 뒤 보관 기간(`MailboxKitConfig::_claimedRetentionDays`)이 지나면 만료 정리가 지운다. PlayFab · 국내 MMO 우편함처럼 받은 우편을 서버가 영원히 들고 있지 않는다.
 *            목록은 만료된 우편을 숨기므로 받은 우편은 사라지는 그때 함께 보이지 않는다. 우편을 지워도 원장 분개(`mail.claim/<토큰>`)는 남아 같은 우편의 두 번째 수령은 우편이 없어 NotFound 이다.
 *            보낸 기록(`mail_sent`, 보내기 멱등)은 우편과 따로 보존 규칙(`OnlineServerConfig::_retentionMailSentDays`)이 지운다 — 우편을 지운 뒤에도 보내는 쪽의 재시도를 가려야 하기 때문이다.
 *          - 만료 정리는 만료 인덱스를 시각 순서로 처리한다. 발행한 재원은 지우기만 한다. 맡긴 첨부는 우편의 만료 처리를 따른다. 버림은 맡긴 것을 소각하고, 돌려줌은 보낸 계정에 돌려주며 상한을 무시한다.
 *            기본 처리는 우편을 넣을 때 정해진다(`ServiceMail::getDefaultExpiryAction`). 운영 우편과 보상 우편은 소멸이고, 플레이어 우편은 반환이며 보관 기간은 30일이다.
 *          - @p pPolicy 는 계정 상한이다. 게임이 CurrencyCatalog 를 넘기며, 기반 계층의 계약 ILedgerPolicy 라서 키트끼리 몰라도 된다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Mail/ServiceMailCampaign.h"
#include "GameFramework/Runtime/Online/Identity/Server/AccountDirectory.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    class ILedgerPolicy;

    /** @brief 저장 함수의 결과입니다. 업무 결과이거나 공통 범주(충돌 `kBusy` · 일시 장애 `kUnavailable`)입니다. */
    using MailboxServiceResult = OnlineServiceResult<MailboxResult>;

    /** @brief 수령 입력입니다. */
    struct MailboxClaimInput
    {
        string               _mailKey{};
        const ILedgerPolicy* _pPolicy{ nullptr };
        uint64               _accountID{ 0 };
        int64                _nowMs{ 0 };
        int64                _claimedRetentionMs{ 0 }; ///< 필수. 받은 우편을 남기는 기간이며 0 이하면 수령을 거절한다(`kInvalidRequest`)
    };
} // namespace sw

namespace sw
{
    /** @brief 만료 반환으로 잔액이 바뀐 계정 하나와 그 자산의 잔액입니다. */
    struct MailboxBalanceChange
    {
        AccountID     _accountID{ kInvalidAccountID };
        LedgerBalance _balance{};
    };
} // namespace sw

namespace sw
{
    /** @brief 만료 정리 한 번의 처리 수입니다. */
    struct MailboxSweepStats
    {
        vector<MailboxBalanceChange> _listChange{}; ///< 보낸 쪽에 돌려준 재화로 잔액이 바뀐 계정이다. 서비스가 그 계정에 알린다
        int32                        _discardedCount{ 0 };
        int32                        _returnedCount{ 0 };
        int32                        _removedCount{ 0 }; ///< 발행 재원, 받은 뒤 보관 기간이 지난 우편, 없는 우편이며 지우기만 한 수이다
        int32                        _failedCount{ 0 };  ///< 다음 정리가 다시 한다. 서버 둘이 같은 우편을 처리하면 진 쪽이 여기에 포함된다
        uint8                        _bMore{ SW_FALSE }; ///< 상한까지 채웠으므로 곧 다시 정리한다
    };
} // namespace sw

namespace sw
{
    /** @brief 우편함 저장 함수 모음입니다. 저장소 스레드에서 부릅니다. */
    struct SW_KIT_SERVER_API MailboxStoreLogic
    {
        static constexpr int32 kMaxClaimAllCount = 16;

        /** @brief 개인 우편을 최근 것부터 @p outReply 에 채우고, 활성이고 받지 않은 캠페인은 첫 페이지의 맨 앞에 채웁니다. 만료된 것은 뺍니다. */
        static MailboxServiceResult listMail( IServiceStoreConnection& connection, uint64 accountID, int64 nowMs, const vector<ServiceMailCampaign>& listActiveCampaign,
                                              const MailboxListRequest& request, MailboxListReply& outReply );
        static MailboxServiceResult markRead( IServiceStoreConnection& connection, uint64 accountID, string_view mailKey );
        static MailboxServiceResult claim( IServiceStoreConnection& connection, const MailboxClaimInput& input, MailboxClaimReply& outReply );
        /** @brief 첨부가 있고 받지 않은 우편을 `kMaxClaimAllCount` 개까지 우편마다 따로 커밋합니다. 하나가 상한에 걸려도 나머지는 받습니다. */
        static MailboxServiceResult claimAll( IServiceStoreConnection& connection, uint64 accountID, int64 nowMs, int64 claimedRetentionMs, const ILedgerPolicy* pPolicy,
                                              MailboxClaimReply& outReply );
        static MailboxServiceResult deleteMail( IServiceStoreConnection& connection, uint64 accountID, string_view mailKey );
        /** @brief 만료 인덱스를 시각 순서로 @p maxCount 개까지 처리합니다. 돌려줄 보낸 계정이 탈퇴로 지워졌으면 첨부를 소각합니다. */
        static void sweepExpired( IServiceStoreConnection& connection, int64 nowMs, int32 maxCount, MailboxSweepStats& outStats );
        /**
         * @brief 탈퇴한 계정 @p accountID 의 우편 레코드를 모두 지웁니다(`IAfterDeleteAccount` — 키트 조립이 훅으로 내놓는다).
         * @details 받은 우편(`mail_item`)과 그 만료 색인 · 알림, 보낸 기록(`mail_sent`), 캠페인 수령(`mail_campaign_claim`)이다. 받지 않은 맡긴 첨부는 소각한다
         *          (분개 키는 만료와 같은 `mail.expire/<우편 토큰>` 이라 만료 정리와 겹쳐도 한 번만 움직인다). 우편마다 커밋하고 멱등이다.
         * @return 다 지웠으면 Ok, 우편이 그사이 바뀌었으면 Conflict(다음 회차), 저장소 문제면 Unavailable 입니다.
         */
        [[nodiscard]] static ServiceStoreResult eraseAccount( IServiceStoreConnection& connection, AccountID accountID, int64 nowMs );
    };
} // namespace sw
