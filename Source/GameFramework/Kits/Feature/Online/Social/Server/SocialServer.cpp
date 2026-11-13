#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/SocialServer.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/AccountPresence.h"
#include "GameFramework/Kits/Feature/Online/OnlineCore/Server/Identity/OnlinePushRelay.h"
#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialProtocol.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialCounts.h"
#include "GameFramework/Runtime/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"

namespace sw
{
    SocialServer::SocialServer()
        : _listSocialScratch{}
        , _listGuildScratch{}
        , _listNotificationScratch{}
        , _listJoinedAccount{}
        , _pSocialService{ nullptr }
        , _pGuildService{ nullptr }
        , _pPresence{ nullptr }
        , _pHost{ nullptr }
        , _listBusSubscription{}
    {
        setReady( false );
    }

    void SocialServer::initialize( SocialService* pSocialService, GuildService* pGuildService, IAccountPresence* pPresence, MetricRegistry* pMetricRegistry )
    {
        _pSocialService = pSocialService;
        _pGuildService  = pGuildService;
        _pPresence      = pPresence;
        initializeCalls( pMetricRegistry, "social" );
        setReady( pSocialService != nullptr && pGuildService != nullptr );
    }

    void SocialServer::shutdown()
    {
        _listBusSubscription.clear();
        _calls.clear();
        _pHost          = nullptr;
        _pSocialService = nullptr;
        _pGuildService  = nullptr;
        _pPresence      = nullptr;
        setReady( false );
    }

    void SocialServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _listBusSubscription.clear();
        _pHost = nullptr;
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::RequestFriend> call, const OnlineCallContext& context, const SocialAccountRequest& request )
    {
        _pSocialService->changeLink( SocialLinkOperation::Request, context._accountID, request._otherID, context._nowMs, call._callID );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::RequestFriendByName> call, const OnlineCallContext& context, const SocialTextRequest& request )
    {
        _pSocialService->requestFriendByName( context._accountID, request._text, context._nowMs, call._callID );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::RespondFriend> call, const OnlineCallContext& context, const SocialRespondRequest& request )
    {
        const SocialLinkOperation operation = request._bAccept == SW_TRUE ? SocialLinkOperation::Accept : SocialLinkOperation::Decline;
        _pSocialService->changeLink( operation, context._accountID, request._otherID, context._nowMs, call._callID );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::RemoveFriend> call, const OnlineCallContext& context, const SocialAccountRequest& request )
    {
        _pSocialService->changeLink( SocialLinkOperation::Remove, context._accountID, request._otherID, context._nowMs, call._callID );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::Block> call, const OnlineCallContext& context, const SocialAccountRequest& request )
    {
        _pSocialService->changeLink( SocialLinkOperation::Block, context._accountID, request._otherID, context._nowMs, call._callID );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::Unblock> call, const OnlineCallContext& context, const SocialAccountRequest& request )
    {
        _pSocialService->changeLink( SocialLinkOperation::Unblock, context._accountID, request._otherID, context._nowMs, call._callID );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::ListLinks> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        _pSocialService->listLinks( context._accountID, call._callID );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::SetPresence> call, const OnlineCallContext& context, const SocialPresenceRequest& request )
    {
        _pSocialService->setPresence( context._accountID, request._status, request._text, context._nowMs, call._callID );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::FriendPresence> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        (void)request;
        _pSocialService->queryFriendPresence( context._accountID, call._callID );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildCreate> call, const OnlineCallContext& context, const SocialTextRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._text = request._text;
        submitGuild( call._callID, context, GuildOperation::Create, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildInvite> call, const OnlineCallContext& context, const SocialAccountRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._targetID = request._otherID;
        submitGuild( call._callID, context, GuildOperation::Invite, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildAccept> call, const OnlineCallContext& context, const GuildIDRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._guildID = request._guildID;
        submitGuild( call._callID, context, GuildOperation::Accept, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildLeave> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        GuildRequest guildRequest;
        (void)request;
        submitGuild( call._callID, context, GuildOperation::Leave, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildKick> call, const OnlineCallContext& context, const SocialAccountRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._targetID = request._otherID;
        submitGuild( call._callID, context, GuildOperation::Kick, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildSetRole> call, const OnlineCallContext& context, const GuildRoleRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._targetID = request._otherID;
        guildRequest._role     = request._role;
        submitGuild( call._callID, context, GuildOperation::SetRole, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildSetAnnouncement> call, const OnlineCallContext& context, const SocialTextRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._text = request._text;
        submitGuild( call._callID, context, GuildOperation::SetNotice, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildGet> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        GuildRequest guildRequest;
        (void)request;
        submitGuild( call._callID, context, GuildOperation::Get, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildDeposit> call, const OnlineCallContext& context, const GuildAssetRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._assetID     = request._assetID;
        guildRequest._amount      = request._amount;
        guildRequest._depositorID = request._depositorID; // 비어 있지 않으면 길드 로직이 Invalid 로 끝낸다
        submitGuild( call._callID, context, GuildOperation::Deposit, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildWithdraw> call, const OnlineCallContext& context, const GuildAssetRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._assetID     = request._assetID;
        guildRequest._amount      = request._amount;
        guildRequest._depositorID = request._depositorID;
        submitGuild( call._callID, context, GuildOperation::Withdraw, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildStorage> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        GuildRequest guildRequest;
        (void)request;
        submitGuild( call._callID, context, GuildOperation::ListStorage, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildSetJoinPolicy> call, const OnlineCallContext& context, const GuildJoinPolicyRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._bOpen = request._bOpen;
        submitGuild( call._callID, context, GuildOperation::SetJoinPolicy, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildApply> call, const OnlineCallContext& context, const GuildIDRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._guildID = request._guildID;
        submitGuild( call._callID, context, GuildOperation::Apply, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildApplications> call, const OnlineCallContext& context, const OnlineNoBody& request )
    {
        GuildRequest guildRequest;
        (void)request;
        submitGuild( call._callID, context, GuildOperation::ListApplications, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildApprove> call, const OnlineCallContext& context, const SocialAccountRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._targetID = request._otherID;
        submitGuild( call._callID, context, GuildOperation::Approve, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildReject> call, const OnlineCallContext& context, const SocialAccountRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._targetID = request._otherID;
        submitGuild( call._callID, context, GuildOperation::Reject, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildGrantWithdrawal> call, const OnlineCallContext& context, const GuildWithdrawalGrantRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._targetID = request._otherID;
        guildRequest._assetID  = request._assetID;
        guildRequest._amount   = request._amount;
        submitGuild( call._callID, context, GuildOperation::GrantWithdrawal, guildRequest );
    }

    void SocialServer::handleCall( OnlineServerCall<SocialMethod::GuildRevokeWithdrawal> call, const OnlineCallContext& context, const GuildWithdrawalGrantRequest& request )
    {
        GuildRequest guildRequest;
        guildRequest._targetID = request._otherID;
        guildRequest._assetID  = request._assetID;
        submitGuild( call._callID, context, GuildOperation::RevokeWithdrawal, guildRequest );
    }

    void SocialServer::submitGuild( uint64 callID, const OnlineCallContext& context, GuildOperation operation, GuildRequest& inoutRequest )
    {
        inoutRequest._accountID      = context._accountID;
        inoutRequest._nowMs          = context._nowMs;
        inoutRequest._operation      = operation;
        inoutRequest._idempotencyKey = context._idempotencyKey; // 허용 부여 · 회수의 멱등 기록 키이다
        if ( context._idempotencyKey.isValid() )                // 창고 이동 · 허용 · 몫을 돌려주는 떠나기와 내보내기의 분개 키이다. 필요한데 없으면 길드 로직이 Invalid 로 끝낸다
            inoutRequest._journalKey = LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAccountScope( context._accountID ), context._idempotencyKey._high,
                                                                              context._idempotencyKey._low );
        _pGuildService->submit( inoutRequest, callID );
    }

    void SocialServer::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        if ( _pHost == nullptr )
            attachHost( host );
        if ( _pSocialService == nullptr || _pGuildService == nullptr )
            return;
        _pSocialService->update( nowMs );
        _calls.publishPendingCount();
        for ( const AccountID accountID : _listJoinedAccount )
        {
            _pGuildService->attachAccount( accountID, nowMs ); // 길드 멤버면 MemberAttached 이벤트가 길드 채널에 넣는다
        }
        _listJoinedAccount.clear();

        _listSocialScratch.clear();
        _pSocialService->takeCompletions( _listSocialScratch );
        for ( SocialCompletion& completion : _listSocialScratch )
        {
            (void)completeByMethod( completion._requestTag, completion ); // 응답 타입은 호출의 메서드가 정한다(`fillReply`). 요청 태그가 0이면(내부에서 건 작업) 버린다
        }

        _listGuildScratch.clear();
        _pGuildService->takeCompletions( _listGuildScratch );
        for ( GuildCompletion& completion : _listGuildScratch )
        {
            (void)completeByMethod( completion._requestTag, completion ); // 연결이 닫혔으면 호스트가 버린다
        }
        sendNotifications( host );
    }

    void SocialServer::fillReply( SocialResultReply& reply, SocialCompletion& completion )
    {
        reply._result = completion._result._result;
    }

    void SocialServer::fillReply( SocialLinkReply& reply, SocialCompletion& completion )
    {
        reply._otherID = completion._otherID;
        reply._result  = completion._result._result;
    }

    void SocialServer::fillReply( SocialLinksReply& reply, SocialCompletion& completion )
    {
        reply._listLink = std::move( completion._listLink );
    }

    void SocialServer::fillReply( SocialPresenceReply& reply, SocialCompletion& completion )
    {
        reply._listPresence = std::move( completion._listPresence );
    }

    void SocialServer::fillReply( SocialResultReply& reply, GuildCompletion& completion )
    {
        reply._result = completion._result._result;
    }

    void SocialServer::fillReply( GuildInfoReply& reply, GuildCompletion& completion )
    {
        reply._guild  = std::move( completion._info );
        reply._result = completion._result._result;
    }

    void SocialServer::fillReply( GuildStorageReply& reply, GuildCompletion& completion )
    {
        reply._listStorage = std::move( completion._listStorage );
        reply._result      = completion._result._result;
    }

    void SocialServer::fillReply( GuildApplicationsReply& reply, GuildCompletion& completion )
    {
        reply._listApplication = std::move( completion._listApplication );
        reply._result          = completion._result._result;
    }

    void SocialServer::onAccountJoined( OnlineServiceHost& host, AccountID accountID )
    {
        (void)host;
        _listJoinedAccount.push_back( accountID );
    }

    void SocialServer::onAccountLeft( OnlineServiceHost& host, AccountID accountID )
    {
        if ( _pSocialService == nullptr )
            return;
        _pSocialService->removeAccount( accountID );
        sendNotifications( host ); // 이 서버의 친구들에게 오프라인을 바로 알린다
    }

    void SocialServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        const ServerBus* pBus = host.getServerBus();
        if ( _pSocialService == nullptr || ( pBus != nullptr && message.getOriginServerID() == pBus->getServerID() ) )
            return; // 이 서버가 낸 메시지이다. 바꾼 시점에 이미 처리했다
        _pSocialService->handleBusMessage( message );
    }

    void SocialServer::attachHost( OnlineServiceHost& host )
    {
        _pHost = &host;
        if ( host.getServerBus() == nullptr )
            return; // 서버가 한 대이다
        _listBusSubscription.push_back( host.subscribeServerBus( SocialBus::kLinksTopic.getName(), this ) );
        _listBusSubscription.push_back( host.subscribeServerBus( SocialBus::kPresenceTopic.getName(), this ) );
    }

    void SocialServer::sendNotifications( OnlineServiceHost& host )
    {
        _listNotificationScratch.clear();
        _pSocialService->pollNotifications( _listNotificationScratch );
        _pGuildService->pollNotifications( _listNotificationScratch );
        for ( const SocialNotification& notification : _listNotificationScratch )
        {
            BitWriter body;
            if ( SocialProtocol::MessageTypes::write( body, notification ) == false )
                continue; // write 가 범위 밖 값을 로그에 남겼다. 알림은 다음 조회로 대신한다
            if ( host.sendPush( notification._recipientID, SocialMethod::Notification::kKind, body ) == false && _pPresence != nullptr )
                (void)_pPresence->sendRemotePush( notification._recipientID, SocialPushName::Notification, notification ); // 다른 서버에 접속해 있다
        }
        _listNotificationScratch.clear();
    }
} // namespace sw

namespace sw
{
    // 다른 서버에 접속한 계정에게 가는 알림이다. 받는 서버가 이 항목으로 자기 빌드의 번호와 B 를 만든다(OnlinePushRelay.h).
    const OnlinePushRelayEntry kSocialNotificationRelay{ "Social.Notification", SocialMethod{}, SocialPushName::Notification, SocialProtocol::MessageTypes{}, OnlinePushBody<SocialNotification>{} };
} // namespace sw
