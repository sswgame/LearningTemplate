/**
 * @file MailboxClient.h
 * @brief 우편함 키트의 클라이언트입니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며, 목록, 읽음, 수령, 모두 받기, 지우기를 보내고 첫 페이지 목록 캐시와 안 읽은 수를 보관합니다.
 * @details - 수령 응답의 잔액(`_listBalance`)은 게임이 경제 클라이언트로 넘긴다. 키트끼리는 서로 모르며 원장이 원본이다.
 *          - 커서 없는 목록 응답이 캐시를 교체한다. 수령, 읽음, 지우기가 성공하면 그 키의 우편을 바로 고친다. 모두 받기는 다음 목록으로 맞춘다.
 *          - 수령을 재시도할 때는 같은 멱등 키(응답의 `_idempotencyKey`)를 쓴다. 콜백은 `OnlineServiceClient::tick` 스레드에서 정확히 한 번 불린다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Mailbox/Shared/MailboxProtocol.h"

namespace sw
{
    /** @brief 우편함 클라이언트 응답 하나입니다. */
    struct MailboxClientReply
    {
        MailboxReply      _reply{};
        NetIdempotencyKey _idempotencyKey{}; ///< 수령을 재시도할 때 그대로 다시 넘기는 키이다
        uint64            _requestID{ 0 };
        uint16            _method{ 0 };
        uint16            _errorCode{ 0 }; ///< 전송 오류나 공통 오류(`OnlineError`)이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class MailboxClient
     * @brief 우편함 클라이언트입니다.
     */
    class SW_GF_API MailboxClient final : public IOnlineClientService
    {
    public:
        using ReplyDelegate = Delegate<void( const MailboxClientReply& )>;

        MailboxClient();

        /** @brief @p pClient 는 빌려 씁니다. `registerClientService( this )` 는 호출하는 쪽이 부릅니다. */
        void initialize( OnlineServiceClient* pClient );

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

        // IOnlineClientService
        uint16 getMethodRange() const override { return OnlineMethodRange::kMailbox; }
        uint32 getProtocolVersion() const override { return MailboxProtocol::kVersion; }
        void   onServicePush( uint16 kind, BitReader& body ) override;

    private:
        struct PendingCall
        {
            ReplyDelegate     _onReply{};
            string            _mailKey{};
            NetIdempotencyKey _key{};
            uint64            _requestID{ 0 };
            uint16            _method{ 0 };
            uint8             _bFirstPage{ SW_FALSE };
        };

        uint64 send( uint16 method, const MailboxRequest& request, const NetIdempotencyKey& key, ReplyDelegate onReply );
        void   onResponse( const OnlineResponse& response );
        void   applyToCache( const PendingCall& call, const MailboxReply& reply );

        ServiceClientCallTable<PendingCall> _callTable;
        vector<MailView>                    _listMail;
        OnlineServiceClient*                _pClient;
        uint64                              _nextRequestID;
        uint32                              _revision;
    };
} // namespace sw
