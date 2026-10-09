#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Server/Social/SocialServer.h"

#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/Base/Online/Identity/AccountPresence.h"
#include "GameFramework/Kits/Feature/Online/Social/SocialProtocol.h"

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
        , _pSocialService{ nullptr }
        , _pGuildService{ nullptr }
        , _pPresence{ nullptr }
        , _pHost{ nullptr }
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
        if ( _pHost != nullptr )
        {
            _pHost->unsubscribeServerBus( SocialBus::kLinksTopic, this );
            _pHost->unsubscribeServerBus( SocialBus::kPresenceTopic, this );
        }
        _pendingTable.clear();
        _pHost          = nullptr;
        _pSocialService = nullptr;
        _pGuildService  = nullptr;
        _pPresence      = nullptr;
    }

    void SocialServer::onHostShutdown( OnlineServiceHost& host )
    {
        (void)host;
        _pHost = nullptr; // 구독은 호스트가 이어서 모두 푼다
    }

    uint32 SocialServer::getProtocolVersion() const { return SocialProtocol::kVersion; }

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
        if ( SocialProtocol::readRequest( body, request ) == false )
        {
            (void)host.respondError( context._token, OnlineError::kInvalidRequest );
            return;
        }
        const uint64    requestTag = _pendingTable.add( context._token );
        const AccountId selfId     = context._accountId;
        const int64     nowMs      = context._nowMs;
        if ( context._method == SocialMethod::kRequestFriendByName )
        {
            _pSocialService->requestFriendByName( selfId, request._text, nowMs, requestTag );
            return;
        }
        if ( SocialMethod::isLinkChange( context._method ) )
        {
            _pSocialService->changeLink( Internal::toLinkOperation( context._method, request._bAccept == SW_TRUE ), selfId, request._otherId, nowMs, requestTag );
            return;
        }
        if ( SocialMethod::isGuild( context._method ) )
        {
            GuildRequest guildRequest;
            guildRequest._text      = std::move( request._text );
            guildRequest._accountId = selfId;
            guildRequest._targetId  = request._otherId;
            guildRequest._guildId   = request._guildId;
            guildRequest._nowMs     = nowMs;
            guildRequest._role      = request._role;
            guildRequest._operation = Internal::toGuildOperation( context._method );
            _pGuildService->submit( guildRequest, requestTag );
            return;
        }
        switch ( context._method )
        {
            case SocialMethod::kListLinks:
            {
                _pSocialService->listLinks( selfId, requestTag );
                break;
            }
            case SocialMethod::kSetPresence:
            {
                _pSocialService->setPresence( selfId, request._status, request._text, nowMs, requestTag );
                break;
            }
            default:
            {
                _pSocialService->queryFriendPresence( selfId, requestTag );
                break;
            }
        }
    }

    void SocialServer::onServiceTick( OnlineServiceHost& host, int64 nowMs )
    {
        if ( _pHost == nullptr )
            attachHost( host );
        if ( _pSocialService == nullptr || _pGuildService == nullptr )
            return;
        _pSocialService->tick( nowMs );

        _listSocialScratch.clear();
        _pSocialService->drainCompletions( _listSocialScratch );
        for ( SocialCompletion& completion : _listSocialScratch )
        {
            SocialReply reply;
            reply._result       = completion._result;
            reply._otherId      = completion._otherId;
            reply._listLink     = std::move( completion._listLink );
            reply._listPresence = std::move( completion._listPresence );
            respond( host, completion._requestTag, reply );
        }

        _listGuildScratch.clear();
        _pGuildService->drainCompletions( _listGuildScratch );
        for ( GuildCompletion& completion : _listGuildScratch )
        {
            SocialReply reply;
            reply._result = completion._result;
            reply._guild  = std::move( completion._info );
            respond( host, completion._requestTag, reply );
        }
        sendNotifications( host );
    }

    void SocialServer::onAccountLeft( OnlineServiceHost& host, AccountId accountId )
    {
        if ( _pSocialService == nullptr )
            return;
        _pSocialService->removeAccount( accountId );
        sendNotifications( host ); // 이 서버 친구들에게 오프라인을 바로
    }

    void SocialServer::onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message )
    {
        const IServerBus* pBus = host.getServerBus();
        if ( _pSocialService == nullptr || ( pBus != nullptr && message._originServerId == pBus->getServerId() ) )
            return; // 이 서버가 낸 것 — 바꾼 자리에서 이미 처리했다
        _pSocialService->handleBusMessage( message._topic, message._bytes );
    }

    void SocialServer::attachHost( OnlineServiceHost& host )
    {
        _pHost = &host;
        host.subscribeServerBus( SocialBus::kLinksTopic, this );
        host.subscribeServerBus( SocialBus::kPresenceTopic, this );
    }

    void SocialServer::respond( OnlineServiceHost& host, uint64 requestTag, const SocialReply& reply )
    {
        NetRequestToken token;
        if ( _pendingTable.take( requestTag, token ) == false )
            return; // 꼬리표 0(안에서 건 일) · 이미 답함
        BitWriter body;
        SocialProtocol::writeReply( body, reply );
        (void)host.respondOk( token, body ); // 닫힌 연결이면 호스트가 버린다
    }

    void SocialServer::sendNotifications( OnlineServiceHost& host )
    {
        _listNotificationScratch.clear();
        _pSocialService->drainNotifications( _listNotificationScratch );
        _pGuildService->drainNotifications( _listNotificationScratch );
        for ( const SocialNotification& notification : _listNotificationScratch )
        {
            BitWriter body;
            SocialProtocol::writeNotification( body, notification );
            if ( host.sendPush( notification._recipientId, SocialMethod::kPushNotification, body ) == false && _pPresence != nullptr )
                (void)_pPresence->sendRemotePush( notification._recipientId, SocialMethod::kPushNotification, body ); // 다른 서버에 붙어 있다
        }
        _listNotificationScratch.clear();
    }
} // namespace sw
