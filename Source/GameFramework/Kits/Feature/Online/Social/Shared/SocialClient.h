/**
 * @file SocialClient.h
 * @brief 친구 키트의 클라이언트 — `OnlineServiceClient` 에 올리는 `IOnlineClientService`. 친구 · 차단 · 접속 상태 · 길드 요청을 보내고 알림을 쌓습니다.
 * @details - 요청마다 결과 델리게이트를 정확히 한 번(`OnlineServiceClient::tick` 스레드) — 업무 결과는 `_reply._result`, 전송 · 공통 오류는 `_errorCode`
 *            (그때 `_reply._result` 는 `SocialProtocol::fromErrorCode` 로 맞춘다).
 *          - 관계 · 길드를 바꾸는 요청에는 멱등 키를 싣는다(다시 연결한 뒤의 재전송이 두 번 적용되지 않는다).
 *          - 로그인 뒤 `listLinks` 를 먼저 부른다 — 서버가 그때 이 계정의 관계를 메모리에 올려 친구의 접속 상태 알림이 온다.
 *          언리얼 Online Services 의 비동기 호출 + 완료 델리게이트와 같은 모양이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "GameFramework/Base/Foundation/Utility/EventBuffer.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"
#include "GameFramework/Base/Online/Service/ServiceClientCallTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialProtocol.h"

namespace sw
{
    /** @brief 클라이언트 응답 하나입니다. */
    struct SocialClientReply
    {
        SocialReply _reply{};
        uint64      _requestId{ 0 };
        uint16      _method{ 0 };
        uint16      _errorCode{ 0 }; ///< 전송 · 공통 오류(`OnlineError`)
    };

    using SocialReplyDelegate = Delegate<void( const SocialClientReply& )>;
} // namespace sw

namespace sw
{
    /**
     * @class SocialClient
     * @brief 연결 하나의 친구 · 길드 창구입니다.
     */
    class SW_GF_API SocialClient final : public IOnlineClientService
    {
    public:
        SocialClient();

        /** @brief @p pClient 는 빌려 쓴다 — `registerClientService( this )` 는 부르는 쪽이(초기화 전에). */
        void initialize( OnlineServiceClient* pClient );

        // 친구
        uint64 requestFriend( AccountId otherId, const SocialReplyDelegate& onReply );
        uint64 requestFriendByName( string_view displayName, const SocialReplyDelegate& onReply );
        uint64 respondFriend( AccountId requesterId, bool bAccept, const SocialReplyDelegate& onReply );
        uint64 removeFriend( AccountId otherId, const SocialReplyDelegate& onReply );
        uint64 block( AccountId otherId, const SocialReplyDelegate& onReply );
        uint64 unblock( AccountId otherId, const SocialReplyDelegate& onReply );
        uint64 listLinks( const SocialReplyDelegate& onReply );
        uint64 setPresence( SocialPresenceStatus status, string_view activity, const SocialReplyDelegate& onReply );
        uint64 queryFriendPresence( const SocialReplyDelegate& onReply );
        // 길드
        uint64 createGuild( string_view name, const SocialReplyDelegate& onReply );
        uint64 inviteToGuild( AccountId targetId, const SocialReplyDelegate& onReply );
        uint64 acceptGuildInvite( uint64 guildId, const SocialReplyDelegate& onReply );
        uint64 leaveGuild( const SocialReplyDelegate& onReply );
        uint64 kickFromGuild( AccountId targetId, const SocialReplyDelegate& onReply );
        uint64 setGuildRole( AccountId targetId, GuildRole role, const SocialReplyDelegate& onReply );
        uint64 setGuildNotice( string_view notice, const SocialReplyDelegate& onReply );
        uint64 requestGuild( const SocialReplyDelegate& onReply );

        /** @brief 쌓인 알림을 꺼냅니다. */
        void drainNotifications( vector<SocialNotification>& outListNotification ) { _notificationBuffer.drainTo( outListNotification ); }

        // IOnlineClientService
        uint16 getMethodRange() const override { return OnlineMethodRange::kSocial; }
        uint32 getProtocolVersion() const override { return SocialProtocol::kVersion; }
        void   onServicePush( uint16 kind, BitReader& body ) override;

    private:
        struct PendingCall
        {
            SocialReplyDelegate _onReply{};
            uint16              _method{ 0 };
        };

        uint64 send( uint16 method, const SocialRequest& request, const SocialReplyDelegate& onReply );
        void   onResponse( const OnlineResponse& response );

        ServiceClientCallTable<PendingCall> _callTable;
        EventBuffer<SocialNotification>     _notificationBuffer;
        OnlineServiceClient*                _pClient;
    };
} // namespace sw
