/**
 * @file SocialServer.h
 * @brief 친구와 길드의 스트림 바인딩입니다. 프로토콜 표(`OnlineProtocolService`, 영역 `kSocial`)로 섭니다.
 * @details 요청은 호출 번호를 요청 태그로 삼아 두 로직에 맡기고, 완료는 응답으로, 알림은 받는 계정에게 보낸다.
 *          - 받는 계정이 이 프로세스에 접속해 있지 않으면 알림을 접속 상태 관리자(`IAccountPresence::sendRemotePush`)에 맡긴다. 거래 키트도 같은 방식이다.
 *          - 버스 `social.links` 와 `social.presence` 를 친구 로직에 넘기며 자기 서버가 낸 메시지는 거른다. 구독은 첫 서비스 틱에서 하며 호스트가 버스를 받은 뒤이다.
 *          - 계정이 떠나면 친구 로직에서 뺀다. 그때 오프라인 알림이 나간다. 호스트에 등록하는 일은 호출하는 쪽이 한다(`host.registerServerService( &server )`, 호스트 `initialize` 전에).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Service/OnlineProtocolService.h"
#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialProtocol.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialService.h"
#include "GameFramework/Runtime/Online/Service/Server/OnlineServiceHost.h"

namespace sw
{
    class IAccountPresence;
    class MetricRegistry;

    /**
     * @class SocialServer
     * @brief 친구와 길드 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API SocialServer final : public OnlineProtocolService<SocialServer, SocialProtocol>
    {
    public:
        SocialServer();

        /** @brief 넘긴 객체는 빌려 씁니다. @p pPresence 는 서버가 여러 대일 때만 넘기며, 없으면 nullptr 이고 서버가 한 대입니다. */
        void initialize( SocialService* pSocialService, GuildService* pGuildService, IAccountPresence* pPresence, MetricRegistry* pMetricRegistry = nullptr );
        /** @brief 버스 구독을 해제합니다. 로직은 호출하는 쪽이 종료합니다. */
        void shutdown();

        // 프로토콜 표의 메서드 - 결과는 호출 번호로 두 로직의 `takeCompletions` 에서 받는다.
        void handleCall( OnlineServerCall<SocialMethod::RequestFriend> call, const OnlineCallContext& context, const SocialAccountRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::RequestFriendByName> call, const OnlineCallContext& context, const SocialTextRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::RespondFriend> call, const OnlineCallContext& context, const SocialRespondRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::RemoveFriend> call, const OnlineCallContext& context, const SocialAccountRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::Block> call, const OnlineCallContext& context, const SocialAccountRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::Unblock> call, const OnlineCallContext& context, const SocialAccountRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::ListLinks> call, const OnlineCallContext& context, const OnlineNoBody& request );
        void handleCall( OnlineServerCall<SocialMethod::SetPresence> call, const OnlineCallContext& context, const SocialPresenceRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::FriendPresence> call, const OnlineCallContext& context, const OnlineNoBody& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildCreate> call, const OnlineCallContext& context, const SocialTextRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildInvite> call, const OnlineCallContext& context, const SocialAccountRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildAccept> call, const OnlineCallContext& context, const GuildIDRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildLeave> call, const OnlineCallContext& context, const OnlineNoBody& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildKick> call, const OnlineCallContext& context, const SocialAccountRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildSetRole> call, const OnlineCallContext& context, const GuildRoleRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildSetNotice> call, const OnlineCallContext& context, const SocialTextRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildGet> call, const OnlineCallContext& context, const OnlineNoBody& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildDeposit> call, const OnlineCallContext& context, const GuildAssetRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildWithdraw> call, const OnlineCallContext& context, const GuildAssetRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildStorage> call, const OnlineCallContext& context, const OnlineNoBody& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildSetJoinPolicy> call, const OnlineCallContext& context, const GuildJoinPolicyRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildApply> call, const OnlineCallContext& context, const GuildIDRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildApplications> call, const OnlineCallContext& context, const OnlineNoBody& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildApprove> call, const OnlineCallContext& context, const SocialAccountRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildReject> call, const OnlineCallContext& context, const SocialAccountRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildGrantWithdrawal> call, const OnlineCallContext& context, const GuildWithdrawalGrantRequest& request );
        void handleCall( OnlineServerCall<SocialMethod::GuildRevokeWithdrawal> call, const OnlineCallContext& context, const GuildWithdrawalGrantRequest& request );

        // IOnlineServerService
        void onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs ) override;
        void onHostShutdown( OnlineServiceHost& host ) override;
        void onAccountJoined( OnlineServiceHost& host, AccountID accountID ) override;
        void onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;
        void onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

        // 완료에서 응답으로 — 응답 타입은 호출의 메서드가 고른다(`OnlineProtocolService::completeByMethod`). 완료 형식마다 답할 수 있는 응답 타입만 있다
        void fillReply( SocialResultReply& reply, SocialCompletion& completion );
        void fillReply( SocialLinkReply& reply, SocialCompletion& completion );
        void fillReply( SocialLinksReply& reply, SocialCompletion& completion );
        void fillReply( SocialPresenceReply& reply, SocialCompletion& completion );
        void fillReply( SocialResultReply& reply, GuildCompletion& completion );
        void fillReply( GuildInfoReply& reply, GuildCompletion& completion );
        void fillReply( GuildStorageReply& reply, GuildCompletion& completion );
        void fillReply( GuildApplicationsReply& reply, GuildCompletion& completion );

    private:
        void attachHost( OnlineServiceHost& host );
        /** @brief 길드 요청에 호출 공통 칸(계정, 시각, 창고 분개 키)을 채워 로직에 맡깁니다. */
        void submitGuild( uint64 callID, const OnlineCallContext& context, GuildOperation operation, GuildRequest& inoutRequest );
        void sendNotifications( OnlineServiceHost& host );

    private:
        vector<SocialCompletion>   _listSocialScratch;
        vector<GuildCompletion>    _listGuildScratch;
        vector<SocialNotification> _listNotificationScratch;
        vector<AccountID>          _listJoinedAccount; ///< 다음 틱에 길드를 붙일 계정이다(붙이기에 시각이 필요하다)
        SocialService*             _pSocialService;
        GuildService*              _pGuildService;
        IAccountPresence*          _pPresence;
        OnlineServiceHost*         _pHost;               ///< 붙은 호스트이다(첫 서비스 틱에 붙는다). 붙었는지만 본다
        vector<Subscription>       _listBusSubscription; ///< 버스 주제 둘의 구독. 서버가 한 대면 비어 있다
    };
} // namespace sw
