/**
 * @file SocialServer.h
 * @brief 친구 · 길드의 스트림 바인딩 — `IOnlineService`(영역 `kSocial`). 요청을 두 로직에 꼬리표로 맡기고, 완료를 응답으로, 알림을 받는 계정에게 보냅니다.
 * @details - 응답 몸은 언제나 `SocialReply`(업무 결과가 첫 값) — 오류 코드는 깨진 몸(`kInvalidRequest`)뿐이다.
 *          - 받는 계정이 이 프로세스에 없으면 알림을 접속 상태 창구(`IAccountPresence::sendRemotePush`)에 맡긴다(거래 키트와 같은 자리).
 *          - 버스 `social.links` · `social.presence` 를 친구 로직에 넘긴다(자기 서버가 낸 것은 거른다). 구독은 첫 서비스 틱에(호스트가 버스를 받은 뒤).
 *          - 계정이 떠나면 친구 로직에서 뺀다(오프라인 알림). 호스트에 올리는 것은 부르는 쪽이(`host.registerService( &server )` — 호스트 `initialize` 전에).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Service/ServicePendingTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Server/Social/GuildService.h"
#include "GameFramework/Kits/Feature/Online/Server/Social/SocialService.h"

namespace sw
{
    struct SocialReply;

    class IAccountPresence;

    /**
     * @class SocialServer
     * @brief 친구 · 길드 바인딩입니다(호스트 `tick` 스레드).
     */
    class SW_GF_API SocialServer final : public IOnlineService
    {
    public:
        SocialServer();

        /** @brief 넘긴 것은 빌려 쓴다. @p pPresence 는 서버 여럿일 때만(없으면 nullptr — 서버 한 대). */
        void initialize( SocialService* pSocialService, GuildService* pGuildService, IAccountPresence* pPresence );
        /** @brief 버스 구독을 풉니다. 로직은 부르는 쪽이 내린다. */
        void shutdown();

        // IOnlineService
        uint16 getMethodRange() const override { return OnlineMethodRange::kSocial; }
        uint32 getProtocolVersion() const override;
        void   onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void   onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void   onHostShutdown( OnlineServiceHost& host ) override;
        void   onAccountLeft( OnlineServiceHost& host, AccountId accountId ) override;
        void   onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void attachHost( OnlineServiceHost& host );
        void respond( OnlineServiceHost& host, uint64 requestTag, const SocialReply& reply );
        void sendNotifications( OnlineServiceHost& host );

        ServicePendingTable        _pendingTable;
        vector<SocialCompletion>   _listSocialScratch;
        vector<GuildCompletion>    _listGuildScratch;
        vector<SocialNotification> _listNotificationScratch;
        SocialService*             _pSocialService;
        GuildService*              _pGuildService;
        IAccountPresence*          _pPresence;
        OnlineServiceHost*         _pHost; ///< 버스를 구독한 호스트(풀 때)
    };
} // namespace sw
