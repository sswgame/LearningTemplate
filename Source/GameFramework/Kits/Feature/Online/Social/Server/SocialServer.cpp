#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Social/Server/SocialServer.h"

#include "Core/Serialization/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Base/Online/Identity/OnlinePushRelay.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialLinkRules.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialProtocol.h"

namespace sw
{
    namespace
    {
        struct SocialServerInternal
        {
            static SocialLinkOperation toLinkOperation( uint16 method, bool bAccept )
            {
                switch ( method )
                {
                    case SocialMethod::kRemoveFriend:
                        return SocialLinkOperation::Remove;
                    case SocialMethod::kBlock:
                        return SocialLinkOperation::Block;
                    case SocialMethod::kUnblock:
                        return SocialLinkOperation::Unblock;
                    case SocialMethod::kRespondFriend:
                        return bAccept ? SocialLinkOperation::Accept : SocialLinkOperation::Decline;
                    default:
                        return SocialLinkOperation::Request;
                }
            }

            static GuildOperation toGuildOperation( uint16 method )
            {
                switch ( method )
                {
                    case SocialMethod::kGuildCreate:
                        return GuildOperation::Create;
                    case SocialMethod::kGuildInvite:
                        return GuildOperation::Invite;
                    case SocialMethod::kGuildAccept:
                        return GuildOperation::Accept;
                    case SocialMethod::kGuildLeave:
                        return GuildOperation::Leave;
                    case SocialMethod::kGuildKick:
                        return GuildOperation::Kick;
                    case SocialMethod::kGuildSetRole:
                        return GuildOperation::SetRole;
                    case SocialMethod::kGuildSetNotice:
                        return GuildOperation::SetNotice;
                    case SocialMethod::kGuildDeposit:
                        return GuildOperation::Deposit;
                    case SocialMethod::kGuildWithdraw:
                        return GuildOperation::Withdraw;
                    case SocialMethod::kGuildStorage:
                        return GuildOperation::ListStorage;
                    case SocialMethod::kGuildSetJoinPolicy:
                        return GuildOperation::SetJoinPolicy;
                    case SocialMethod::kGuildApply:
                        return GuildOperation::Apply;
                    case SocialMethod::kGuildApplications:
                        return GuildOperation::ListApplications;
                    case SocialMethod::kGuildApprove:
                        return GuildOperation::Approve;
                    case SocialMethod::kGuildReject:
                        return GuildOperation::Reject;
                    default:
                        return GuildOperation::Get;
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SocialServer::SocialServer()
        : _pendingTable{}
        , _listSocialScratch{}
        , _listGuildScratch{}
        , _listNotificationScratch{}
        , _listJoinedAccount{}
        , _pSocialService{ nullptr }
        , _pGuildService{ nullptr }
        , _pPresence{ nullptr }
        , _pHost{ nullptr }
        , _listBusSubscription{}
    {
    }

    void SocialServer::initialize( SocialService* pSocialService, GuildService* pGuildService, IAccountPresence* pPresence )
    {
        _pSocialService = pSocialService;
        _pGuildService  = pGuildService;
        _pPresence      = pPresence;
    }

    void SocialServer::shutdown()
    {
        _listBusSubscription.clear();
        _pendingTable.clear();
        _pHost          = nullptr;
        _pSocialService = nullptr;
        _pGuildService  = nullptr;
        _pPresence      = nullptr;
    }

    void SocialServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _listBusSubscription.clear();
        _pHost = nullptr;
    }

    const OnlineProtocolEntry& SocialServer::getProtocol() const
    {
        return SocialProtocol::kOnlineEntry;
    }

    void SocialServer::onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body )
    {
        using Internal = SocialServerInternal;
        if ( _pSocialService == nullptr || _pGuildService == nullptr )
        {
            (void)host.respondError( context._token, OnlineError::kUnavailable );
            return;
        }
        SocialRequest request;
        const bool    bKnownMethod = SocialMethod::isLinkChange( context._method ) || SocialMethod::isGuild( context._method ) ||
                                  context._method == SocialMethod::kListLinks || context._method == SocialMethod::kSetPresence ||
                                  context._method == SocialMethod::kFriendPresence;
        if ( bKnownMethod == false )
        {
            (void)host.respondError( context._token, OnlineError::kNotFound );
            return;
        }
        if ( SocialProtocol::MessageTypes::read( body, request ) != FixedReadResult::Ok )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        const uint64           requestTag = _pendingTable.add( context._token );
        const AccountID        selfID     = context._accountID;
        const UnixMilliseconds nowMs      = context._nowMs;
        if ( context._method == SocialMethod::kRequestFriendByName )
        {
            _pSocialService->requestFriendByName( selfID, request._text, nowMs, requestTag );
            return;
        }
        if ( SocialMethod::isLinkChange( context._method ) )
        {
            _pSocialService->changeLink( Internal::toLinkOperation( context._method, request._bAccept == SW_TRUE ), selfID, request._otherID, nowMs, requestTag );
            return;
        }
        if ( SocialMethod::isGuild( context._method ) )
        {
            GuildRequest guildRequest;
            guildRequest._text      = std::move( request._text );
            guildRequest._accountID = selfID;
            guildRequest._targetID  = request._otherID;
            guildRequest._guildID   = request._guildID;
            guildRequest._nowMs     = nowMs;
            guildRequest._role      = request._role;
            guildRequest._operation = Internal::toGuildOperation( context._method );
            guildRequest._assetID   = std::move( request._assetID );
            guildRequest._amount    = request._amount;
            guildRequest._bOpen     = request._bOpen;
            if ( context._idempotencyKey.isValid() ) // 창고 이동의 분개 키이다. 없으면 길드 로직이 Invalid 로 끝낸다
                guildRequest._journalKey = LedgerJournalKey::makeFromIdempotency( LedgerJournalKey::makeAccountScope( selfID ), context._idempotencyKey._high,
                                                                                  context._idempotencyKey._low );
            _pGuildService->submit( guildRequest, requestTag );
            return;
        }
        switch ( context._method )
        {
            case SocialMethod::kListLinks:
            {
                _pSocialService->listLinks( selfID, requestTag );
                break;
            }
            case SocialMethod::kSetPresence:
            {
                _pSocialService->setPresence( selfID, request._status, request._text, nowMs, requestTag );
                break;
            }
            default:
            {
                _pSocialService->queryFriendPresence( selfID, requestTag );
                break;
            }
        }
    }

    void SocialServer::onServiceTick( OnlineServiceHost& host, UnixMilliseconds nowMs )
    {
        if ( _pHost == nullptr )
            attachHost( host );
        if ( _pSocialService == nullptr || _pGuildService == nullptr )
            return;
        _pSocialService->update( nowMs );
        for ( const AccountID accountID : _listJoinedAccount )
        {
            _pGuildService->attachAccount( accountID, nowMs ); // 길드 멤버면 MemberAttached 이벤트가 길드 채널에 넣는다
        }
        _listJoinedAccount.clear();

        _listSocialScratch.clear();
        _pSocialService->drainCompletions( _listSocialScratch );
        for ( SocialCompletion& completion : _listSocialScratch )
        {
            SocialReply reply;
            reply._result       = completion._result;
            reply._otherID      = completion._otherID;
            reply._listLink     = std::move( completion._listLink );
            reply._listPresence = std::move( completion._listPresence );
            respond( host, completion._requestTag, reply );
        }

        _listGuildScratch.clear();
        _pGuildService->drainCompletions( _listGuildScratch );
        for ( GuildCompletion& completion : _listGuildScratch )
        {
            SocialReply reply;
            reply._result          = completion._result;
            reply._guild           = std::move( completion._info );
            reply._listStorage     = std::move( completion._listStorage );
            reply._listApplication = std::move( completion._listApplication );
            respond( host, completion._requestTag, reply );
        }
        sendNotifications( host );
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

    void SocialServer::respond( OnlineServiceHost& host, uint64 requestTag, const SocialReply& reply )
    {
        NetRequestToken token;
        if ( _pendingTable.take( requestTag, token ) == false )
            return;                                                              // 요청 태그가 0이거나(내부에서 건 작업) 이미 응답했다
        (void)host.respondMessage<SocialProtocol::MessageTypes>( token, reply ); // 연결이 닫혔으면 호스트가 버린다
    }

    void SocialServer::sendNotifications( OnlineServiceHost& host )
    {
        _listNotificationScratch.clear();
        _pSocialService->drainNotifications( _listNotificationScratch );
        _pGuildService->drainNotifications( _listNotificationScratch );
        for ( const SocialNotification& notification : _listNotificationScratch )
        {
            BitWriter body;
            if ( SocialProtocol::MessageTypes::write( body, notification ) == false )
                continue; // write 가 범위 밖 값을 로그에 남겼다. 알림은 다음 조회로 대신한다
            if ( host.sendPush( notification._recipientID, SocialMethod::kPushNotification, body ) == false && _pPresence != nullptr )
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
