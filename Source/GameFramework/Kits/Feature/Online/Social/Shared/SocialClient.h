/**
 * @file SocialClient.h
 * @brief 친구 키트의 클라이언트입니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며, 친구, 차단, 접속 상태, 길드 요청을 보내고 알림을 쌓아 둡니다.
 * @details - 요청마다 결과 델리게이트를 `OnlineServiceClient::update` 스레드에서 정확히 한 번 부른다. 업무 결과는 `_reply._result` 에, 전송 오류와 공통 오류는 `_errorCode` 에 있으며
 *            이때 `_reply._result` 는 `SocialProtocol::fromErrorCode` 로 맞춘다.
 *          - 관계나 길드를 바꾸는 요청에는 멱등 키를 싣는다. 다시 연결한 뒤 재전송해도 두 번 적용되지 않게 하려는 것이다.
 *          - 로그인한 뒤 `listLinks` 를 먼저 부른다. 서버가 그때 이 계정의 관계를 메모리에 로드해야 친구의 접속 상태 알림이 온다.
 *
 *          언리얼 Online Services 의 비동기 호출과 완료 델리게이트에 해당한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/DrainGate.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineKitClient.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialProtocol.h"

namespace sw
{
    /**
     * @class SocialClient
     * @brief 연결 하나의 친구와 길드 클라이언트입니다.
     */
    class SW_KIT_API SocialClient final : public OnlineKitClient<SocialProtocol, SocialReply>
    {
    public:
        SocialClient();

        // 친구
        uint64 requestFriend( AccountID otherID, const ReplyDelegate& onReply );
        uint64 requestFriendByName( string_view displayName, const ReplyDelegate& onReply );
        uint64 respondFriend( AccountID requesterID, bool bAccept, const ReplyDelegate& onReply );
        uint64 removeFriend( AccountID otherID, const ReplyDelegate& onReply );
        uint64 block( AccountID otherID, const ReplyDelegate& onReply );
        uint64 unblock( AccountID otherID, const ReplyDelegate& onReply );
        uint64 listLinks( const ReplyDelegate& onReply );
        uint64 setPresence( SocialPresenceStatus status, string_view activity, const ReplyDelegate& onReply );
        uint64 queryFriendPresence( const ReplyDelegate& onReply );
        // 길드
        uint64 createGuild( string_view name, const ReplyDelegate& onReply );
        uint64 inviteToGuild( AccountID targetID, const ReplyDelegate& onReply );
        uint64 acceptGuildInvite( uint64 guildID, const ReplyDelegate& onReply );
        uint64 leaveGuild( const ReplyDelegate& onReply );
        uint64 kickFromGuild( AccountID targetID, const ReplyDelegate& onReply );
        uint64 setGuildRole( AccountID targetID, GuildRole role, const ReplyDelegate& onReply );
        uint64 setGuildNotice( string_view notice, const ReplyDelegate& onReply );
        uint64 requestGuild( const ReplyDelegate& onReply );
        /** @brief 내 계정의 @p assetID 를 @p amount 만큼 길드 창고로 옮깁니다. 응답의 `_listStorage` 에 그 자산의 창고 잔액이 들어갑니다. */
        uint64 depositToGuild( string_view assetID, int64 amount, const ReplyDelegate& onReply );
        /** @brief 길드 창고의 @p assetID 를 @p amount 만큼 내 계정으로 옮깁니다. 임원 이상만 할 수 있습니다. */
        uint64 withdrawFromGuild( string_view assetID, int64 amount, const ReplyDelegate& onReply );
        uint64 requestGuildStorage( const ReplyDelegate& onReply );

        /** @brief 알림 버퍼를 꺼낼 권한을 @p outLease 에 받습니다. 클라이언트마다 한 곳만 받을 수 있어 이미 다른 표가 있으면 false 입니다(알림을 서로 가져가는 일을 막는다). */
        [[nodiscard]] bool claimNotifications( DrainLease& outLease ) { return _notificationGate.claim( outLease ); }
        /** @brief 쌓인 알림을 꺼냅니다. @p lease 는 `claimNotifications` 로 받은 표여야 합니다(아니면 어서션이고 꺼내지 않습니다). */
        void drainNotifications( const DrainLease& lease, vector<SocialNotification>& outListNotification )
        {
            SW_ASSERT( _notificationGate.isHeldBy( lease ) );
            if ( _notificationGate.isHeldBy( lease ) )
                _notificationBuffer.drainTo( outListNotification );
        }

        // IOnlineClientService
        const OnlineProtocolEntry& getProtocol() const override { return SocialProtocol::kOnlineEntry; }
        void                       onServicePush( uint16 kind, BitReader& body ) override;

    private:
        uint64 send( uint16 method, const SocialRequest& request, const ReplyDelegate& onReply );

        DrainGate                       _notificationGate;
        EventBuffer<SocialNotification> _notificationBuffer;
    };
} // namespace sw
