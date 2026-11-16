/**
 * @file MailboxClient.h
 * @brief 우편함 키트의 클라이언트입니다. 요청은 `call<MailboxMethod::Claim>( request, onReply )` 이고, 이 클래스는 첫 페이지 목록 캐시와 안 읽은 수를 보관합니다.
 * @details - 수령 응답의 잔액(`_listBalance`)은 게임이 경제 클라이언트로 넘긴다. 키트끼리는 서로 모르며 원장이 원본이다.
 *          - 커서 없는 목록 응답이 캐시를 교체한다. 수령, 읽음, 지우기가 성공하면 그 키의 우편을 바로 고친다. 모두 받기는 다음 목록으로 맞춘다.
 *          - 새 우편 알림(`MailboxMethod::NewMail`)은 `pollNewMail` 로 넘긴다. 게임은 받으면 목록을 다시 읽는다.
 *          - 수령을 재시도할 때는 같은 멱등 키(결과의 `_idempotencyKey`)를 쓴다. 요청 · 응답 규칙은 `OnlineProtocolClient` 에 있다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Event/ConsumerLock.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Runtime/MailboxProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolClient.h"

namespace sw
{
    /**
     * @class MailboxClient
     * @brief 우편함 클라이언트입니다.
     */
    class SW_KIT_API MailboxClient final : public OnlineProtocolClient<MailboxClient, MailboxProtocol>
    {
    public:
        MailboxClient();

        /** @brief 마지막 첫 페이지 목록에 그 뒤의 상태 변화를 반영한 것입니다. */
        const vector<MailView>& getMails() const { return _listMail; }
        int32                   countUnread() const;
        uint32                  getRevision() const { return _revision; }
        /** @brief 새 우편 알림 버퍼를 꺼낼 권한을 @p outLease 에 받습니다. 클라이언트마다 한 곳만 받을 수 있어 이미 다른 표가 있으면 false 입니다(알림을 서로 가져가는 일을 막는다). */
        [[nodiscard]] bool claimNewMail( ConsumerLease& outLease ) { return _newMailConsumerLock.claim( outLease ); }
        /** @brief 받은 새 우편 알림을 @p outListNewMail 끝에 넘깁니다. @p lease 는 `claimNewMail` 로 받은 표여야 합니다(아니면 어서션이고 꺼내지 않습니다). */
        void pollNewMail( const ConsumerLease& lease, vector<MailboxNewMail>& outListNewMail )
        {
            SW_ASSERT( _newMailConsumerLock.isHeldBy( lease ) );
            if ( _newMailConsumerLock.isHeldBy( lease ) )
                _newMailBuffer.moveAllTo( outListNewMail );
        }

        // OnlineProtocolClient 의 훅 - 캐시를 고치는 메서드만 오버로드한다(모두 받기는 다음 목록으로 맞춘다)
        using OnlineProtocolClient::onCallSucceeded;
        void onCallSucceeded( MailboxMethod::List, const MailboxListRequest& request, const MailboxListReply& reply );
        void onCallSucceeded( MailboxMethod::MarkRead, const MailboxMailRequest& request, const MailboxResultReply& reply );
        void onCallSucceeded( MailboxMethod::Claim, const MailboxMailRequest& request, const MailboxClaimReply& reply );
        void onCallSucceeded( MailboxMethod::Delete, const MailboxMailRequest& request, const MailboxResultReply& reply );
        void onPush( MailboxMethod::NewMail, const MailboxNewMail& body );

    private:
        /** @brief 캐시에서 그 키의 우편 번호입니다. 없으면 음수입니다. */
        [[nodiscard]] optional<int32> findMailIndex( const string& mailKey ) const;

    private:
        vector<MailView>            _listMail;
        ConsumerLock                _newMailConsumerLock;
        EventBuffer<MailboxNewMail> _newMailBuffer;
        uint32                      _revision;
    };
} // namespace sw
