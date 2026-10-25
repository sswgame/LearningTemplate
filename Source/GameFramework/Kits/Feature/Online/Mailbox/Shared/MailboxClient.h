/**
 * @file MailboxClient.h
 * @brief 우편함 키트의 클라이언트입니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며, 목록, 읽음, 수령, 모두 받기, 지우기를 보내고 첫 페이지 목록 캐시와 안 읽은 수를 보관합니다.
 * @details - 수령 응답의 잔액(`_listBalance`)은 게임이 경제 클라이언트로 넘긴다. 키트끼리는 서로 모르며 원장이 원본이다.
 *          - 커서 없는 목록 응답이 캐시를 교체한다. 수령, 읽음, 지우기가 성공하면 그 키의 우편을 바로 고친다. 모두 받기는 다음 목록으로 맞춘다.
 *          - 새 우편 알림(`kPushNewMail`)은 `drainNewMail` 로 넘긴다. 게임은 받으면 목록을 다시 읽는다.
 *          - 수령을 재시도할 때는 같은 멱등 키(응답의 `_idempotencyKey`)를 쓴다. 요청 · 응답 규칙은 `OnlineKitClient` 에 있다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineDrainGate.h"
#include "GameFramework/Base/Online/Service/OnlineKitClient.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Shared/MailboxProtocol.h"

namespace sw
{
    /** @brief 응답을 받을 때 캐시를 고치는 데 쓰는 요청 쪽 정보입니다. */
    struct MailboxCallContext
    {
        string _mailKey{};
        uint8  _bFirstPage{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /**
     * @class MailboxClient
     * @brief 우편함 클라이언트입니다.
     */
    class SW_KIT_API MailboxClient final : public OnlineKitClient<MailboxProtocol, MailboxReply, MailboxCallContext>
    {
    public:
        MailboxClient();

        uint64 requestList( string_view cursor, int32 maxCount, ReplyDelegate onReply );
        uint64 requestMarkRead( string_view mailKey, ReplyDelegate onReply );
        /** @brief @p key 가 비어 있으면 새로 만듭니다. 재시도할 때는 응답의 키를 다시 넘깁니다. */
        uint64 requestClaim( string_view mailKey, const NetIdempotencyKey& key, ReplyDelegate onReply );
        uint64 requestClaimAll( const NetIdempotencyKey& key, ReplyDelegate onReply );
        uint64 requestDelete( string_view mailKey, ReplyDelegate onReply );

        /** @brief 마지막 첫 페이지 목록에 그 뒤의 상태 변화를 반영한 것입니다. */
        const vector<MailView>& getMails() const { return _listMail; }
        int32                   countUnread() const;
        uint32                  getRevision() const { return _revision; }
        /** @brief 새 우편 알림 버퍼를 꺼낼 권한을 @p outLease 에 받습니다. 클라이언트마다 한 곳만 받을 수 있어 이미 다른 표가 있으면 false 입니다(알림을 서로 가져가는 일을 막는다). */
        [[nodiscard]] bool claimNewMail( OnlineDrainLease& outLease ) { return _newMailGate.claim( outLease ); }
        /** @brief 받은 새 우편 알림을 @p outListNewMail 끝에 넘깁니다. @p lease 는 `claimNewMail` 로 받은 표여야 합니다(아니면 어서션이고 꺼내지 않습니다). */
        void drainNewMail( const OnlineDrainLease& lease, vector<MailboxNewMail>& outListNewMail )
        {
            SW_ASSERT( _newMailGate.isHeldBy( lease ) );
            if ( _newMailGate.isHeldBy( lease ) )
                _newMailBuffer.drainTo( outListNewMail );
        }

        // IOnlineClientService
        const OnlineProtocolEntry& getProtocol() const override { return MailboxProtocol::kOnlineEntry; }
        void                       onServicePush( uint16 kind, BitReader& body ) override;

    private:
        uint64 send( uint16 method, const MailboxRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply );
        void   onCallSucceeded( const Call& call, const MailboxReply& reply ) override;

        vector<MailView>            _listMail;
        OnlineDrainGate             _newMailGate;
        EventBuffer<MailboxNewMail> _newMailBuffer;
        uint32                      _revision;
    };
} // namespace sw
