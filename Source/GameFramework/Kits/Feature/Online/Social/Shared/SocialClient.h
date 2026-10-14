/**
 * @file SocialClient.h
 * @brief 친구 키트의 클라이언트입니다. `OnlineServiceClient` 에 등록하는 `IOnlineClientService` 이며, 친구, 차단, 접속 상태, 길드 요청을 보내고 알림을 쌓아 둡니다.
 * @details - 요청마다 결과 델리게이트를 `OnlineServiceClient::tick` 스레드에서 정확히 한 번 부른다. 업무 결과는 `_reply._result` 에, 전송 오류와 공통 오류는 `_errorCode` 에 있으며
 *            이때 `_reply._result` 는 `SocialProtocol::fromErrorCode` 로 맞춘다.
 *          - 관계나 길드를 바꾸는 요청에는 멱등 키를 싣는다. 다시 연결한 뒤 재전송해도 두 번 적용되지 않게 하려는 것이다.
 *          - 로그인한 뒤 `listLinks` 를 먼저 부른다. 서버가 그때 이 계정의 관계를 메모리에 올려야 친구의 접속 상태 알림이 온다.
 *
 *          언리얼 Online Services 의 비동기 호출과 완료 델리게이트에 해당한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/EventBuffer.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

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
        uint64      _requestID{ 0 };
        uint16      _method{ 0 };
        uint16      _errorCode{ 0 }; ///< 전송 오류나 공통 오류(`OnlineError`)이다
    };

    using SocialReplyDelegate = Delegate<void( const SocialClientReply& )>;
} // namespace sw

namespace sw
{
    /**
     * @class SocialClient
     * @brief 연결 하나의 친구와 길드 클라이언트입니다.
     */
    class SW_KIT_API SocialClient final : public IOnlineClientService
    {
    public:
        SocialClient();

        /** @brief @p pClient 는 빌려 씁니다. `registerClientService( this )` 는 호출하는 쪽이 초기화하기 전에 부릅니다. */
        void initialize( OnlineServiceClient* pClient );

        // 친구
        uint64 requestFriend( AccountID otherID, const SocialReplyDelegate& onReply );
        uint64 requestFriendByName( string_view displayName, const SocialReplyDelegate& onReply );
        uint64 respondFriend( AccountID requesterID, bool bAccept, const SocialReplyDelegate& onReply );
        uint64 removeFriend( AccountID otherID, const SocialReplyDelegate& onReply );
        uint64 block( AccountID otherID, const SocialReplyDelegate& onReply );
        uint64 unblock( AccountID otherID, const SocialReplyDelegate& onReply );
        uint64 listLinks( const SocialReplyDelegate& onReply );
        uint64 setPresence( SocialPresenceStatus status, string_view activity, const SocialReplyDelegate& onReply );
        uint64 queryFriendPresence( const SocialReplyDelegate& onReply );
        // 길드
        uint64 createGuild( string_view name, const SocialReplyDelegate& onReply );
        uint64 inviteToGuild( AccountID targetID, const SocialReplyDelegate& onReply );
        uint64 acceptGuildInvite( uint64 guildID, const SocialReplyDelegate& onReply );
        uint64 leaveGuild( const SocialReplyDelegate& onReply );
        uint64 kickFromGuild( AccountID targetID, const SocialReplyDelegate& onReply );
        uint64 setGuildRole( AccountID targetID, GuildRole role, const SocialReplyDelegate& onReply );
        uint64 setGuildNotice( string_view notice, const SocialReplyDelegate& onReply );
        uint64 requestGuild( const SocialReplyDelegate& onReply );
        /** @brief 내 계정의 @p assetID 를 @p amount 만큼 길드 창고로 옮깁니다. 응답의 `_listStorage` 에 그 자산의 창고 잔액이 들어갑니다. */
        uint64 depositToGuild( string_view assetID, int64 amount, const SocialReplyDelegate& onReply );
        /** @brief 길드 창고의 @p assetID 를 @p amount 만큼 내 계정으로 옮깁니다. 임원 이상만 할 수 있습니다. */
        uint64 withdrawFromGuild( string_view assetID, int64 amount, const SocialReplyDelegate& onReply );
        uint64 requestGuildStorage( const SocialReplyDelegate& onReply );

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
