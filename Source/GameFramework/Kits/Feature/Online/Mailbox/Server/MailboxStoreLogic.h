/**
 * @file MailboxStoreLogic.h
 * @brief 우편함의 저장 부분 — 목록 · 읽음 · 수령 · 모두 받기 · 지우기 · 캠페인 수령 · 만료 쓸기. 저장소 스레드의 동기 함수입니다.
 * @details - 수령 = (재원 → 계정) 원장 이동 + 우편 상태 "받음" + 만료 색인 지우기, 한 트랜잭션. 분개 키 `mail.claim/<우편 토큰>` 이 두 번째 수령을 막는다
 *            (두 기기에서 동시에 눌러도 원장은 한 번). 상한을 넘으면 우편이 그대로 남는다(`CapExceeded`).
 *          - 지우기는 받았거나 첨부가 없는 우편만(첨부가 남은 우편을 지우면 재화가 증발한다).
 *          - 만료 쓸기: 만료 색인을 시각 순으로 — 발행 재원은 지우기만, 맡긴 첨부는 우편의 만료 처리대로 버림(맡김 → 소각) · 돌려줌(맡김 → 보낸 계정, 상한 무시).
 *            기본 처리는 넣을 때 정해진다(`ServiceMail::getDefaultExpiryAction` — 운영 · 보상 우편은 소멸, 플레이어 우편은 반환, 보관 30 일).
 *          @p pPolicy 는 계정 상한(게임이 CurrencyCatalog 를 넘긴다 — 기반 계약 ILedgerPolicy 라 키트끼리 몰라도 된다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Mail/ServiceMailCampaign.h"
#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Shared/MailboxProtocol.h"

namespace sw
{
    class ILedgerPolicy;

    /** @brief 수령 입력입니다. */
    struct MailboxClaimInput
    {
        string               _mailKey{};
        const ILedgerPolicy* _pPolicy{ nullptr };
        uint64               _accountId{ 0 };
        int64                _nowMs{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 만료 쓸기 한 번의 수입니다. */
    struct MailboxSweepStats
    {
        int32 _discardedCount{ 0 };
        int32 _returnedCount{ 0 };
        int32 _removedCount{ 0 }; ///< 발행 재원 · 이미 받은 것 · 없는 우편 — 지우기만
        int32 _failedCount{ 0 };  ///< 다음 쓸기가 다시 한다(서버 둘이 같은 우편을 쓸면 진 쪽)
        uint8 _bMore{ SW_FALSE }; ///< 상한까지 채웠다 — 곧 다시
    };
} // namespace sw

namespace sw
{
    /** @brief 우편함 저장 함수입니다(저장소 스레드). */
    struct SW_GF_API MailboxStoreLogic
    {
        static constexpr int32 kMaxClaimAllCount = 16;

        /** @brief 개인 우편을 최근 것부터 + 활성이고 안 받은 캠페인(첫 쪽에만, 맨 앞)을 @p outReply 에 채웁니다. 만료된 것은 뺀다. */
        static MailboxResult listMail( IServiceStoreConnection& connection, uint64 accountId, int64 nowMs, const vector<ServiceMailCampaign>& listActiveCampaign,
                                       const MailboxRequest& request, MailboxReply& outReply );
        static MailboxResult markRead( IServiceStoreConnection& connection, uint64 accountId, string_view mailKey );
        static MailboxResult claim( IServiceStoreConnection& connection, const MailboxClaimInput& input, MailboxReply& outReply );
        /** @brief 첨부 있는 안 받은 우편을 `kMaxClaimAllCount` 통까지, 우편마다 따로 커밋합니다(하나가 상한에 걸려도 나머지는 받는다). */
        static MailboxResult claimAll( IServiceStoreConnection& connection, uint64 accountId, int64 nowMs, const ILedgerPolicy* pPolicy, MailboxReply& outReply );
        static MailboxResult deleteMail( IServiceStoreConnection& connection, uint64 accountId, string_view mailKey );
        /** @brief 만료 색인을 시각 순으로 @p maxCount 개까지 처리합니다. */
        static void sweepExpired( IServiceStoreConnection& connection, int64 nowMs, int32 maxCount, MailboxSweepStats& outStats );
    };
} // namespace sw
