/**
 * @file SocialServer.h
 * @brief 친구와 길드의 스트림 바인딩입니다. `IOnlineHostService`(영역 `kSocial`)를 구현합니다.
 * @details 요청은 요청 태그와 함께 두 로직에 맡기고, 완료는 응답으로, 알림은 받는 계정에게 보낸다.
 *          - 응답 본문은 항상 `SocialReply` 이며 업무 결과가 첫 값이다. 오류 코드는 손상된 본문(`kInvalidRequest`)뿐이다.
 *          - 받는 계정이 이 프로세스에 접속해 있지 않으면 알림을 접속 상태 관리자(`IAccountPresence::sendRemotePush`)에 맡긴다. 거래 키트도 같은 방식이다.
 *          - 버스 `social.links` 와 `social.presence` 를 친구 로직에 넘기며 자기 서버가 낸 메시지는 거른다. 구독은 첫 서비스 틱에서 하며 호스트가 버스를 받은 뒤이다.
 *          - 계정이 떠나면 친구 로직에서 뺀다. 그때 오프라인 알림이 나간다. 호스트에 등록하는 일은 호출하는 쪽이 한다(`host.registerHostService( &server )`, 호스트 `initialize` 전에).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineServiceHost.h"
#include "GameFramework/Base/Online/Service/ServicePendingTable.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/GuildService.h"
#include "GameFramework/Kits/Feature/Online/Social/Server/SocialService.h"

namespace sw
{
    struct SocialReply;

    class IAccountPresence;

    /**
     * @class SocialServer
     * @brief 친구와 길드 바인딩입니다. 호스트 `tick` 스레드에서 씁니다.
     */
    class SW_KIT_SERVER_API SocialServer final : public IOnlineHostService
    {
    public:
        SocialServer();

        /** @brief 넘긴 객체는 빌려 씁니다. @p pPresence 는 서버가 여러 대일 때만 넘기며, 없으면 nullptr 이고 서버가 한 대입니다. */
        void initialize( SocialService* pSocialService, GuildService* pGuildService, IAccountPresence* pPresence );
        /** @brief 버스 구독을 해제합니다. 로직은 호출하는 쪽이 종료합니다. */
        void shutdown();

        // IOnlineHostService
        const OnlineProtocolEntry& getProtocol() const override;
        void                       onServiceRequest( OnlineServiceHost& host, const OnlineCallContext& context, BitReader& body ) override;
        void                       onServiceTick( OnlineServiceHost& host, int64 nowMs ) override;
        void                       onHostShutdown( OnlineServiceHost& host ) override;
        void                       onAccountJoined( OnlineServiceHost& host, AccountID accountID ) override;
        void                       onAccountLeft( OnlineServiceHost& host, AccountID accountID ) override;
        void                       onServerBusMessage( OnlineServiceHost& host, const ServerBusMessage& message ) override;

    private:
        void attachHost( OnlineServiceHost& host );
        void respond( OnlineServiceHost& host, uint64 requestTag, const SocialReply& reply );
        void sendNotifications( OnlineServiceHost& host );

        ServicePendingTable        _pendingTable;
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
