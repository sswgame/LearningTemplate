/**
 * @file MailboxStoreLogic.h
 * @brief 우편함의 저장 부분입니다. 목록, 읽음, 수령, 모두 받기, 지우기, 캠페인 수령, 만료 정리를 처리하는 저장소 스레드의 동기 함수입니다.
 * @details - 수령은 (재원에서 계정으로) 원장 이동, 우편 상태를 "받음" 으로 바꾸기, 만료 인덱스 지우기를 한 트랜잭션으로 처리한다. 분개 키 `mail.claim/<우편 토큰>` 이 두 번째 수령을 막으므로 두 기기에서 동시에 눌러도 원장은 한 번만 움직인다.
 *            상한을 넘으면 우편이 그대로 남는다(`CapExceeded`).
 *          - 지우기는 받았거나 첨부가 없는 우편만 지울 수 있다. 첨부가 남은 우편을 지우면 재화가 사라지기 때문이다.
 *          - 만료 정리는 만료 인덱스를 시각 순서로 처리한다. 발행한 재원은 지우기만 한다. 맡긴 첨부는 우편의 만료 처리를 따른다. 버림은 맡긴 것을 소각하고, 돌려줌은 보낸 계정에 돌려주며 상한을 무시한다.
 *            기본 처리는 우편을 넣을 때 정해진다(`ServiceMail::getDefaultExpiryAction`). 운영 우편과 보상 우편은 소멸이고, 플레이어 우편은 반환이며 보관 기간은 30일이다.
 *          - @p pPolicy 는 계정 상한이다. 게임이 CurrencyCatalog 를 넘기며, 기반 계층의 계약 ILedgerPolicy 라서 키트끼리 몰라도 된다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Mail/ServiceMailCampaign.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"

namespace sw
{
    class ILedgerPolicy;

    /** @brief 수령 입력입니다. */
    struct MailboxClaimInput
    {
        string               _mailKey{};
        const ILedgerPolicy* _pPolicy{ nullptr };
        uint64               _accountID{ 0 };
        int64                _nowMs{ 0 };
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
        int32                        _removedCount{ 0 }; ///< 발행 재원, 이미 받은 우편, 없는 우편이며 지우기만 한 수이다
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
        static MailboxResult listMail( IServiceStoreConnection& connection, uint64 accountID, int64 nowMs, const vector<ServiceMailCampaign>& listActiveCampaign,
                                       const MailboxRequest& request, MailboxReply& outReply );
        static MailboxResult markRead( IServiceStoreConnection& connection, uint64 accountID, string_view mailKey );
        static MailboxResult claim( IServiceStoreConnection& connection, const MailboxClaimInput& input, MailboxReply& outReply );
        /** @brief 첨부가 있고 받지 않은 우편을 `kMaxClaimAllCount` 개까지 우편마다 따로 커밋합니다. 하나가 상한에 걸려도 나머지는 받습니다. */
        static MailboxResult claimAll( IServiceStoreConnection& connection, uint64 accountID, int64 nowMs, const ILedgerPolicy* pPolicy, MailboxReply& outReply );
        static MailboxResult deleteMail( IServiceStoreConnection& connection, uint64 accountID, string_view mailKey );
        /** @brief 만료 인덱스를 시각 순서로 @p maxCount 개까지 처리합니다. */
        static void sweepExpired( IServiceStoreConnection& connection, int64 nowMs, int32 maxCount, MailboxSweepStats& outStats );
    };
} // namespace sw
